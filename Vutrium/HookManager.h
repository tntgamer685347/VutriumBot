#ifndef HOOK_MANAGER_H
#define HOOK_MANAGER_H

#include <windows.h>
#include "kiero/minhook/include/MinHook.h" // <-- RESTORE MinHook include
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <stdexcept>

#include "Objects.hpp"
#include "GObjectsTable.h"
#include "GNameTable.h"
#include "EventManager.h"
#include "MemoryManager.h"

struct HookInfo {
    std::string FunctionName;
    uintptr_t TargetAddress = 0; // Address we intended to hook
    LPVOID pTrampoline = nullptr;
    bool IsEnabled = false;
    HookInfo(std::string name) : FunctionName(std::move(name)) {}
};

class HookManager {
public:
    // Restore original constructor signature
    HookManager(MemoryManager* memMgr, GObjectsTable* gobjects, GNameTable* gnames, EventManager* eventMgr);
    ~HookManager();
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;
    HookManager(HookManager&&) = delete;
    HookManager& operator=(HookManager&&) = delete;

    bool Initialize(); // Restore original functionality
    void Shutdown();   // Restore original functionality

    bool CreateAndEnableHook(const std::string& functionName, LPVOID pDetour);
    bool CreateAndEnableHook(const std::string& hookName, LPVOID pDetour, uintptr_t targetAddress);
    bool DisableHook(const std::string& functionName);
    bool EnableHook(const std::string& functionName);
    template <typename T> T GetTrampoline(const std::string& functionName) const;

    bool IsInitialized() const { return isMinHookInitialized_; } // <-- RESTORE
    MemoryManager& GetMemoryManager() const;

private:
    MemoryManager* memManager_ = nullptr;
    GObjectsTable* gobjectsTable_ = nullptr;
    GNameTable* gnameTable_ = nullptr;
    EventManager* eventManager_ = nullptr;
    // FridaHookManager* fridaMgr_ = nullptr; // <-- REMOVE

    bool isMinHookInitialized_ = false; // <-- RESTORE
    std::unordered_map<std::string, HookInfo> activeHooks_;
    // std::unordered_map<uintptr_t, LPVOID> trampolines_; // This wasn't used before, keep removed

    HookInfo* FindHookByName(const std::string& functionName);
    const HookInfo* FindHookByName(const std::string& functionName) const;
};

// Restore original GetTrampoline implementation
template <typename T>
T HookManager::GetTrampoline(const std::string& functionName) const {
    auto it = activeHooks_.find(functionName);
    if (it != activeHooks_.end() && it->second.pTrampoline != nullptr) {
        return reinterpret_cast<T>(it->second.pTrampoline);
    }
    return nullptr;
}

// Hook Function Declarations (Remain the same)
// ... (typedefs and declarations for hook functions like Hook_ProcessEvent) ...
typedef void(__fastcall* tProcessEvent)(SDK::UObject* pThis, SDK::UFunction* pFunction, void* pParms);
void __fastcall Hook_ProcessEvent(SDK::UObject* pThis, SDK::UFunction* pFunction, void* pParms);


#endif