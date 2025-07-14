#ifndef RLSDK_H
#define RLSDK_H

#include <string>
#include <memory>
#include <cstdint>
#include <functional>
#include <optional>
#include "MemoryManager.h"
#include "GNameTable.h"
#include "GObjectsTable.h"
#include "EventManager.h"
#include "HookManager.h"
#include "Objects.hpp"
#include "EventData.h"

const std::wstring DEFAULT_PROCESS_NAME = L"RocketLeague.exe";

class RLSDK {
public:
    explicit RLSDK(const std::wstring& processName = DEFAULT_PROCESS_NAME, bool hookPlayerTick = false);
    ~RLSDK();
    RLSDK(const RLSDK&) = delete; RLSDK& operator=(const RLSDK&) = delete; RLSDK(RLSDK&&) = delete; RLSDK& operator=(RLSDK&&) = delete;

    bool IsInitialized() const;
    MemoryManager& GetMemoryManager(); const MemoryManager& GetMemoryManager() const;
    GNameTable& GetGNameTable(); const GNameTable& GetGNameTable() const;
    GObjectsTable& GetGObjectsTable(); const GObjectsTable& GetGObjectsTable() const;
    EventManager& GetEventManager(); const EventManager& GetEventManager() const;
    HookManager& GetHookManager(); const HookManager& GetHookManager() const;
    uintptr_t GetModuleBaseAddress() const; DWORD GetProcessID() const;
    const std::wstring& GetModuleName() const; const std::string& GetBuildType() const;
    std::string GetName(int32_t index) const;
    SDK::UClass FindStaticClass(const std::string& fullName) const;
    SDK::UFunction FindStaticFunction(const std::string& fullName) const;
    SDK::AGameEvent GetCurrentGameEvent() const;
    void Subscribe(const std::string& eventType, EventManager::Callback callback);
    void Unsubscribe(const std::string& eventType, const EventManager::Callback& callback);
    void Shutdown();
    void UpdateCurrentGameEvent(uintptr_t gameEventAddress);
    void NotifyGameEventDestroyed(uintptr_t gameEventAddress);
    int GetPing() const;
    std::optional<SDK::UObject> GetPlayerInput(std::optional<SDK::APlayerReplicationInfo> priOpt) const;

private:
    MemoryManager memManager_; GNameTable gnames_; GObjectsTable gobjects_; EventManager eventMgr_; HookManager hookMgr_;
    bool initialized_ = false; bool shouldHookPlayerTick_ = false; std::wstring moduleName_; uintptr_t moduleBase_ = 0; DWORD processId_ = 0;
    uintptr_t gnamesOffset_ = 0; uintptr_t gobjectsOffset_ = 0; std::string buildType_; SDK::AGameEvent currentGameEvent_;
    std::string DetectBuildType(); bool ResolveOffsets(size_t moduleSize); bool SetupHooks();
};

#endif