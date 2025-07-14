#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <windows.h>
#include <string>
#include <vector>
#include <tlhelp32.h>
#include <cstdint>
#include <optional> // For std::optional

class MemoryManager {
public:
    // --- Constructors & Destructor ---
    MemoryManager();
    explicit MemoryManager(const std::wstring& processName);
    explicit MemoryManager(DWORD processId);
    ~MemoryManager();

    // Disable copy semantics
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // --- Attachment ---
    bool Attach(const std::wstring& processName);
    bool Attach(DWORD processId);
    void Detach();
    bool IsAttached() const;

    // --- Memory Operations ---
    bool ReadBytes(uintptr_t address, void* pBuffer, SIZE_T size) const;
    bool WriteBytes(uintptr_t address, const void* pBuffer, SIZE_T size);

    template<typename T>
    std::optional<T> Read(uintptr_t address) const {
        T value{};
        if (ReadBytes(address, &value, sizeof(T))) {
            return value;
        }
        return std::nullopt;
    }

    template<typename T>
    bool Write(uintptr_t address, const T& value) {
        return WriteBytes(address, &value, sizeof(T));
    }

    // --- Utility ---
    uintptr_t GetModuleBaseAddress(const std::wstring& moduleName) const;
    DWORD GetProcessId() const;
    HANDLE GetProcessHandle() const;
    uintptr_t FindPattern(uintptr_t startAddress, size_t searchSize, const unsigned char* pattern, const char* mask) const; // Added FindPattern declaration

private:
    // --- Private Members ---
    HANDLE processHandle = nullptr;
    DWORD  processId = 0;

    // --- Private Helpers ---
    static DWORD FindProcessIdByName(const std::wstring& processName);
    bool OpenProcessHandle(DWORD pid);
    void CloseProcessHandle();
};

#endif // MEMORY_MANAGER_H