#include "HookManager.h"
#include "EventData.h"
#include "Logger.h"
#include <iostream>
#include <stdexcept>   // Included for std::invalid_argument
#include "kiero/minhook/include/MinHook.h"
 
namespace FunctionName {
    inline constexpr const char* BoostPickedUp = "Function TAGame.VehiclePickup_Boost_TA.Idle.EndState";
    inline constexpr const char* BoostRespawn = "Function TAGame.VehiclePickup_Boost_TA.Idle.BeginState";
    inline constexpr const char* PlayerTick = "Function Engine.PlayerController.PlayerTick";
    inline constexpr const char* HandleKeyPress = "Function TAGame.GameViewportClient_TA.HandleKeyPress";
    inline constexpr const char* RoundActiveBegin = "Function TAGame.GameEvent_Soccar_TA.Active.BeginState";
    inline constexpr const char* RoundActiveEnd = "Function TAGame.GameEvent_Soccar_TA.Active.EndState";
    inline constexpr const char* ResetPickups = "Function TAGame.GameEvent_TA.ResetPickups";
    inline constexpr const char* GameEventBeginPlay = "Function TAGame.GameEvent_Soccar_TA.PostBeginPlay";
    inline constexpr const char* ViewportClientTick = "Function Engine.GameViewportClient.Tick";
    inline constexpr const char* GameEventDestroyed = "Function TAGame.GameEvent_Soccar_TA.Destroyed";
} // namespace FunctionName

namespace ClassName {
    inline constexpr const char* CoreObject = "Class Core.Object";
    // Define a name for the ProcessEvent hook
    inline constexpr const char* ProcessEvent = "Engine.Object.ProcessEvent";
} // namespace ClassName
// --- End Copied Namespaces ---

// --- Static Globals (Ensure these are set correctly in RLSDK or main) ---
// These provide access to necessary managers from within the static hook functions.
// Proper initialization and lifetime management are crucial.
static HookManager* g_HookManagerInstance = nullptr;
static GNameTable* g_GNameTableInstance = nullptr;
static EventManager* g_EventManagerInstance = nullptr;
// static MemoryManager* g_MemoryManagerInstance = nullptr; // Alternative if not using HookManager accessor

// Declare the global viewport address as extern
extern uintptr_t g_GameViewportClientAddress;

// Constructor: Restore original signature and validation
HookManager::HookManager(MemoryManager* memMgr, GObjectsTable* gobjects, GNameTable* gnames, EventManager* eventMgr)
    : memManager_(memMgr),
    gobjectsTable_(gobjects),
    gnameTable_(gnames),
    eventManager_(eventMgr),
    // fridaMgr_(fridaMgr) // REMOVE
    isMinHookInitialized_(false) // RESTORE
{
    if (!memMgr) throw std::invalid_argument("HookManager: MemoryManager pointer cannot be null.");
    if (!gobjectsTable_) throw std::invalid_argument("HookManager: GObjectsTable pointer cannot be null.");
    if (!gnameTable_) throw std::invalid_argument("HookManager: GNameTable pointer cannot be null.");
    if (!eventManager_) throw std::invalid_argument("HookManager: EventManager pointer cannot be null.");
    // if (!fridaMgr_) throw std::invalid_argument("HookManager: FridaHookManager pointer cannot be null."); // REMOVE

    g_HookManagerInstance = this;
    g_GNameTableInstance = gnames;
    g_EventManagerInstance = eventMgr;
}

// Destructor remains mostly the same, just calls Shutdown
HookManager::~HookManager() {
    Shutdown();
    if (g_HookManagerInstance == this) g_HookManagerInstance = nullptr;
    if (g_GNameTableInstance == gnameTable_) g_GNameTableInstance = nullptr;
    if (g_EventManagerInstance == eventManager_) g_EventManagerInstance = nullptr;
}

// Accessor remains the same
MemoryManager& HookManager::GetMemoryManager() const {
    return *memManager_;
}

// Restore original Initialize
bool HookManager::Initialize() {
    if (isMinHookInitialized_) {
        Logger::Info("HookManager::Initialize(): Already marked as initialized.");
        return true;
    }
    Logger::Info("HookManager: Assuming MinHook initialized by Kiero. Marking HookManager as ready.");
    isMinHookInitialized_ = true;
    return true;
}

// Restore original Shutdown
void HookManager::Shutdown() {
    if (!isMinHookInitialized_) return;
    Logger::Info("HookManager: Disabling hooks...");
    for (auto const& [name, info] : activeHooks_) {
        if (info.IsEnabled && info.TargetAddress != 0) {
            MH_STATUS disableStatus = MH_DisableHook(reinterpret_cast<LPVOID>(info.TargetAddress));
            if (disableStatus != MH_OK && disableStatus != MH_ERROR_DISABLED) Logger::MinHookError("Failed to disable hook '" + name + "'", disableStatus);
        }
    }
    activeHooks_.clear();

    // Call Uninitialize - Kiero does NOT call this.
    MH_STATUS status = MH_Uninitialize();
    if (status != MH_OK) Logger::MinHookError("Failed to uninitialize MinHook", status);
    else Logger::Info("HookManager: MinHook uninitialized.");
    isMinHookInitialized_ = false;
}

// Restore Overload 1 using MinHook logic implicitly via Overload 2
bool HookManager::CreateAndEnableHook(const std::string& functionName, LPVOID pDetour) {
    if (!isMinHookInitialized_) { Logger::Error("HookManager: MinHook not initialized."); return false; }
    if (!gobjectsTable_ || !gobjectsTable_->IsInitialized()) { Logger::Error("Cannot create hook '" + functionName + "'. GObjectsTable missing or not initialized."); return false; }

    SDK::UFunction targetFunc = gobjectsTable_->FindStaticFunction(functionName);
    if (!targetFunc.IsValid()) { Logger::Error("Could not find target function object '" + functionName + "'."); return false; }

    uintptr_t executableAddress = targetFunc.GetFuncAddress(GetMemoryManager());
    if (executableAddress == 0) {
        Logger::Error("Failed to read executable function pointer for '" + functionName + "'.");
        return false;
    }
    return CreateAndEnableHook(functionName, pDetour, executableAddress);
}

// Restore Overload 2 using MinHook logic
bool HookManager::CreateAndEnableHook(const std::string& hookName, LPVOID pDetour, uintptr_t targetAddress) {
    if (!isMinHookInitialized_) { Logger::Error("HookManager: MinHook not initialized."); return false; }
    if (targetAddress == 0 || !pDetour) { Logger::Error("HookManager: Invalid target or detour for '" + hookName + "'."); return false; }

    if (activeHooks_.count(hookName)) {
        Logger::Warning("Hook for '" + hookName + "' already exists, attempting to enable...");
        return EnableHook(hookName);
    }

    Logger::Info("Attempting to hook '" + hookName + "' at explicit address: " + Logger::to_hex(targetAddress));
    LPVOID pOriginal = nullptr;
    MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(targetAddress), pDetour, &pOriginal);
    if (status != MH_OK) {
        Logger::MinHookError("MH_CreateHook failed for '" + hookName + "'", status);
        return false;
    }
    if (pOriginal == nullptr) {
        Logger::Error("Trampoline is NULL after MH_CreateHook succeeded for '" + hookName + "'!");
        MH_RemoveHook(reinterpret_cast<LPVOID>(targetAddress));
        return false;
    }

    activeHooks_.emplace(hookName, HookInfo(hookName));
    HookInfo& newHook = activeHooks_.at(hookName);
    newHook.TargetAddress = targetAddress;
    newHook.pTrampoline = pOriginal;
    newHook.IsEnabled = false;

    status = MH_EnableHook(reinterpret_cast<LPVOID>(targetAddress));
    if (status != MH_OK) {
        Logger::MinHookError("MH_EnableHook failed for '" + hookName + "'", status);
        activeHooks_.erase(hookName);
        MH_RemoveHook(reinterpret_cast<LPVOID>(targetAddress));
        return false;
    }
    newHook.IsEnabled = true;
    Logger::Info("Successfully created and enabled hook for '" + hookName + "'.");
    return true;
}

// Restore DisableHook using MinHook
bool HookManager::DisableHook(const std::string& functionName) {
    if (!isMinHookInitialized_) { Logger::Error("HookManager: MinHook not initialized."); return false; }
    HookInfo* pHook = FindHookByName(functionName);
    if (!pHook) { Logger::Warning("Cannot disable hook '" + functionName + "', not found."); return false; }
    if (!pHook->IsEnabled) return true;
    if (pHook->TargetAddress == 0) { Logger::Error("Cannot disable hook '" + functionName + "', target address is zero."); return false; }

    MH_STATUS status = MH_DisableHook(reinterpret_cast<LPVOID>(pHook->TargetAddress));
    if (status != MH_OK) {
        Logger::MinHookError("MH_DisableHook failed for '" + functionName + "'", status);
        return false;
    }
    pHook->IsEnabled = false;
    Logger::Info("Disabled hook for '" + functionName + "'.");
    return true;
}

// Restore EnableHook using MinHook
bool HookManager::EnableHook(const std::string& functionName) {
     if (!isMinHookInitialized_) { Logger::Error("HookManager: MinHook not initialized."); return false; }
    HookInfo* pHook = FindHookByName(functionName);
    if (!pHook) { Logger::Warning("Cannot enable hook '" + functionName + "', not found."); return false; }
    if (pHook->IsEnabled) return true;
    if (pHook->TargetAddress == 0) { Logger::Error("Cannot enable hook '" + functionName + "', target address is zero."); return false; }

    MH_STATUS status = MH_EnableHook(reinterpret_cast<LPVOID>(pHook->TargetAddress));
    if (status != MH_OK) {
        Logger::MinHookError("MH_EnableHook failed for '" + functionName + "'", status);
        return false;
    }
    pHook->IsEnabled = true;
    Logger::Info("Enabled hook for '" + functionName + "'.");
    return true;
}

// FindHookByName remains the same
HookInfo* HookManager::FindHookByName(const std::string& functionName) {
    auto it = activeHooks_.find(functionName);
    if (it != activeHooks_.end()) {
        return &(it->second);
    }
    return nullptr;
}
const HookInfo* HookManager::FindHookByName(const std::string& functionName) const {
    auto it = activeHooks_.find(functionName);
    if (it != activeHooks_.end()) {
        return &(it->second);
    }
    return nullptr;
}


// GetOriginal helper remains the same (uses the local HookManager::GetTrampoline)
template <typename T>
T GetOriginal(const std::string& funcName) {
    if (g_HookManagerInstance) {
        return g_HookManagerInstance->GetTrampoline<T>(funcName);
    }
    Logger::Error("GetOriginal called but g_HookManagerInstance is null!");
    return nullptr;
}

// ProcessEvent Hook Implementation
void __fastcall Hook_ProcessEvent(SDK::UObject* pThis, SDK::UFunction* pFunction, void* pParms) {
    static uint32_t callCounter = 0;
    static uint32_t lastLoggedCounter = 0;

    auto pOriginal = GetOriginal<tProcessEvent>(ClassName::ProcessEvent);
    if (!pOriginal) {
        static bool trampolineErrorLogged = false;
        if (!trampolineErrorLogged) { Logger::Error("Trampoline not found for ProcessEvent (MinHook)!"); trampolineErrorLogged = true; }
        return;
    }

    bool shouldLog = (callCounter % 10000 == 0) || (callCounter - lastLoggedCounter > 100000); // Log samples less frequently
    callCounter++;
    
    if (!pThis || !pFunction || !g_GNameTableInstance || !g_EventManagerInstance || !g_HookManagerInstance) {
        static bool pointerErrorLogged = false;
        if (!pointerErrorLogged) { Logger::Error("Hook_ProcessEvent called with NULL pThis, pFunction, or global managers!"); pointerErrorLogged = true; }
        pOriginal(pThis, pFunction, pParms);
        return;
    }

    uintptr_t thisAddr = (pThis ? pThis->Address : 0);
    uintptr_t funcAddr = pFunction->Address;

    std::string functionFullName = "";
    try {
         MemoryManager& pm = g_HookManagerInstance->GetMemoryManager();
         functionFullName = pFunction->GetFullName(pm, *g_GNameTableInstance);
         
         // Sample logging (Still commented out, but less frequent check)
         if (shouldLog) {
             // Logger::Info("ProcessEvent Sample [" + std::to_string(callCounter) +
             //             "]: " + functionFullName + " (Obj: " + Logger::to_hex(thisAddr) + ")"); 
             lastLoggedCounter = callCounter;
         }

        // REMOVED GameEvent Begin/Destroy Matching Logic

        // +++ ADDED: Simple IsA Check for GameEvent +++
        if (pThis && pThis->IsValid()) {
            // Check if the object executing the function IsA GameEvent_Soccar_TA
            if (pThis->IsA(pm, *g_GNameTableInstance, "GameEvent_Soccar_TA")) {
                // If it IS a GameEvent, fire the event with its address
                 Logger::Info("**** HOOK: IsA(GameEvent_Soccar_TA) PASSED for Obj: " + Logger::to_hex(thisAddr) + 
                            " | Func: " + functionFullName + " ****");
                 EventProbableGameEventFoundData geFoundData(thisAddr);
                 if (g_EventManagerInstance) {
                     g_EventManagerInstance->Fire(EventType::OnProbableGameEventFound, geFoundData);
                 }
        }
    }

    }
    catch (const std::exception& e) { // <-- Catch block START
         Logger::Error("Hook_ProcessEvent: Exception processing function: " + std::string(e.what()) +
                     " on UFunc@" + Logger::to_hex(funcAddr) + " UObject@" + Logger::to_hex(thisAddr));
         // Do not call original here, call it outside the try-catch
    } // <-- Catch block END
    catch (...) { // <-- Catch (...) block START
         Logger::Error("Hook_ProcessEvent: Unknown exception processing function for UFunc@" +
                     Logger::to_hex(funcAddr) + " UObject@" + Logger::to_hex(thisAddr));
         // Do not call original here, call it outside the try-catch
    } // <-- Catch (...) block END
    
    // Call the original function regardless of exceptions during our logic
    pOriginal(pThis, pFunction, pParms);
}

// --- REMOVED GameViewportClient_Tick Hook Implementation --- 
/* // Multi-line comment to REMOVE the definition
void __fastcall Hook_GameViewportClient_Tick(SDK::UObject* pThis, float DeltaTime) {
    // ... (Original function pointer retrieval) ...
    // Logic removed
    // Always call the original function
    pOriginal(pThis, DeltaTime);
}
*/


// --- OLD HOOK DEFINITIONS Commented Out --- 
/*
void __fastcall Hook_VehiclePickup_Boost_Idle_EndState(SDK::UObject* pThisFSM) { ... }
...
*/