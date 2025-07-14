#include "MemoryManager.h"
#include <stdexcept>
#include <iostream>

MemoryManager::MemoryManager() : processHandle(nullptr), processId(0) {}

MemoryManager::MemoryManager(const std::wstring& processName) : processHandle(nullptr), processId(0) {
    Attach(processName);
}

MemoryManager::MemoryManager(DWORD pid) : processHandle(nullptr), processId(0) {
    Attach(pid);
}

MemoryManager::~MemoryManager() {
    CloseProcessHandle();
}

// --- Attachment ---
bool MemoryManager::Attach(const std::wstring& processName) {
    Detach();
    DWORD pid = FindProcessIdByName(processName);
    if (pid == 0) return false;
    return OpenProcessHandle(pid);
}

bool MemoryManager::Attach(DWORD pid) {
    Detach();
    if (pid == 0) return false;
    return OpenProcessHandle(pid);
}

void MemoryManager::Detach() {
    CloseProcessHandle();
    processId = 0;
}

bool MemoryManager::IsAttached() const {
    return processHandle != nullptr && processHandle != INVALID_HANDLE_VALUE;
}

// --- Memory Operations ---
bool MemoryManager::ReadBytes(uintptr_t address, void* pBuffer, SIZE_T size) const {
    if (!IsAttached() || pBuffer == nullptr || size == 0) return false;
    SIZE_T bytesRead = 0;
    if (ReadProcessMemory(processHandle, reinterpret_cast<LPCVOID>(address), pBuffer, size, &bytesRead)) {
        return bytesRead == size;
    }
    return false;
}

bool MemoryManager::WriteBytes(uintptr_t address, const void* pBuffer, SIZE_T size) {
    if (!IsAttached() || pBuffer == nullptr || size == 0) return false;
    SIZE_T bytesWritten = 0;
    if (WriteProcessMemory(processHandle, reinterpret_cast<LPVOID>(address), pBuffer, size, &bytesWritten)) {
        return bytesWritten == size;
    }
    return false;
}

// --- Utility ---
uintptr_t MemoryManager::GetModuleBaseAddress(const std::wstring& moduleName) const {
    if (!IsAttached() || moduleName.empty()) return 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    MODULEENTRY32W moduleEntry;
    moduleEntry.dwSize = sizeof(MODULEENTRY32W);
    uintptr_t baseAddress = 0;

    if (Module32FirstW(hSnap, &moduleEntry)) {
        do {
            if (_wcsicmp(moduleEntry.szModule, moduleName.c_str()) == 0) {
                baseAddress = reinterpret_cast<uintptr_t>(moduleEntry.modBaseAddr);
                break;
            }
        } while (Module32NextW(hSnap, &moduleEntry));
    }
    CloseHandle(hSnap);
    return baseAddress;
}

DWORD MemoryManager::GetProcessId() const {
    return processId;
}

HANDLE MemoryManager::GetProcessHandle() const {
    return processHandle;
}

// --- Private Helpers ---
DWORD MemoryManager::FindProcessIdByName(const std::wstring& processName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;

    if (Process32FirstW(hSnap, &processEntry)) {
        do {
            if (_wcsicmp(processEntry.szExeFile, processName.c_str()) == 0) {
                pid = processEntry.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &processEntry));
    }
    CloseHandle(hSnap);
    return pid;
}

bool MemoryManager::OpenProcessHandle(DWORD pid) {
    const DWORD desiredAccess = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION;
    processHandle = OpenProcess(desiredAccess, FALSE, pid);
    if (processHandle == nullptr) {
        processId = 0;
        return false;
    }
    processId = pid;
    return true;
}

void MemoryManager::CloseProcessHandle() {
    if (processHandle != nullptr && processHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(processHandle);
        processHandle = nullptr;
    }
}

// Implementation for FindPattern
uintptr_t MemoryManager::FindPattern(uintptr_t startAddress, size_t searchSize, const unsigned char* pattern, const char* mask) const {
    if (!IsAttached() || pattern == nullptr || mask == nullptr || searchSize == 0) {
        return 0;
    }

    size_t patternLength = strlen(mask);
    if (patternLength == 0 || patternLength > searchSize) {
        return 0;
    }

    std::vector<unsigned char> buffer(searchSize);
    if (!ReadBytes(startAddress, buffer.data(), searchSize)) {
        return 0; // Failed to read memory
    }

    for (size_t i = 0; i <= searchSize - patternLength; ++i) {
        bool found = true;
        for (size_t j = 0; j < patternLength; ++j) {
            if (mask[j] != '?' && buffer[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return startAddress + i;
        }
    }

    return 0; // Pattern not found
}