#include "RLSDK.h"
#include "EventData.h"
#include "Logger.h"      // Include Logger
#include <iostream>
#include <stdexcept>     // For throwing exceptions
#include <windows.h>
#include <psapi.h>       // For GetModuleFileNameEx, GetModuleInformation
#include <algorithm>     // For std::transform, std::max
#include <vector>
#include <map>
#include <sstream>       // Needed for Logger::to_hex

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
    inline constexpr const char* GameViewportClient = "Engine.GameViewportClient";
    // Define a name for the ProcessEvent hook
    inline constexpr const char* ProcessEvent = "Engine.Object.ProcessEvent";
} // namespace ClassName

// REMOVED 'static' to give external linkage
// ADDED: Static global to store the main viewport client address
uintptr_t g_GameViewportClientAddress = 0;

namespace {

    // Define patterns (ensure these are correct for your target)
    const std::map<std::string, std::vector<uint8_t>> SCAN_PATTERNS = {
        {"GNames_1",   {0x75, 0x05, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x85, 0xDB, 0x75, 0x31}},
        {"GObjects_1", {0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x5D, 0xBF, 0x48}},
        {"GNames_2",   {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00}},
        {"GObjects_2", {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00}},
    };

    std::optional<uintptr_t> PatternScan(const MemoryManager& pm, uintptr_t begin, size_t size, const std::vector<uint8_t>& pattern, const std::string& patternName = "Unknown")
    {
        if (pattern.empty() || size == 0 || size < pattern.size()) return std::nullopt;
        // Logger::Info("RLSDK Scan: Searching for " + patternName + "..."); // Logging removed

        std::vector<uint8_t> buffer;
        try { buffer.resize(size); }
        catch (const std::bad_alloc&) {
            Logger::Error("RLSDK Scan Error: Failed to allocate buffer of size " + std::to_string(size) + " for pattern " + patternName);
            return std::nullopt;
        }

        if (!pm.ReadBytes(begin, buffer.data(), size)) {
            Logger::Error("RLSDK Scan Error: Failed to read memory for pattern " + patternName);
            return std::nullopt;
        }

        for (size_t i = 0; i <= size - pattern.size(); ++i) {
            bool found = true;
            for (size_t j = 0; j < pattern.size(); ++j) {
                if (pattern[j] != 0x00 && pattern[j] != buffer[i + j]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                uintptr_t foundAddress = begin + i;
                // Logger::Info("RLSDK Scan: Found " + patternName + " at " + Logger::to_hex(foundAddress)); // Logging removed
                return foundAddress;
            }
        }
        Logger::Warning("RLSDK Scan Warning: Pattern " + patternName + " not found.");
        return std::nullopt;
    }

    // Calculate address based on Method 1 (relative calls and offsets)
    std::optional<uintptr_t> CalculateMethod1(const MemoryManager& pm, uintptr_t patternAddr, bool isGNames) {
        try {
            uintptr_t step1_offset_addr = patternAddr + (isGNames ? 3 : 1);
            auto relOffset1Opt = pm.Read<int32_t>(step1_offset_addr);
            if (!relOffset1Opt) { /* Logger::Warning(...); */ return std::nullopt; } // Logging removed
            uintptr_t afterCall1 = step1_offset_addr + sizeof(int32_t);
            uintptr_t intermediateAddr = afterCall1 + *relOffset1Opt;
            intermediateAddr += (isGNames ? 0x27 : 0x65); // Apply specific hardcoded adjustments
            uintptr_t step2_offset_addr = intermediateAddr + 3;
            auto relOffset2Opt = pm.Read<int32_t>(step2_offset_addr);
            if (!relOffset2Opt) { /* Logger::Warning(...); */ return std::nullopt; } // Logging removed
            uintptr_t afterLeaOffset = step2_offset_addr + sizeof(int32_t);
            uintptr_t finalAddr = afterLeaOffset + *relOffset2Opt;
            return finalAddr;
        }
        catch (const std::exception& e) {
            Logger::Error("Scan Calc1 Exception for " + std::string(isGNames ? "GNames" : "GObjects") + ": " + e.what());
            return std::nullopt;
        }
        // return std::nullopt; // Unreachable
    }

    // Calculate address based on Method 2 (simple RIP relative - matches Python GNames_2/GObjects_2)
    std::optional<uintptr_t> CalculateMethod2(const MemoryManager& pm, uintptr_t patternAddr) {
        try {
            uintptr_t offset_addr = patternAddr + 3; // Offset is 3 bytes into the pattern
            auto relOffsetOpt = pm.Read<int32_t>(offset_addr);
            if (!relOffsetOpt) { /* Logger::Warning(...); */ return std::nullopt; } // Logging removed
            uintptr_t instruction_end = patternAddr + 7; // Instruction is 7 bytes long (e.g., 48 8B 05 XX XX XX XX)
            uintptr_t finalAddr = instruction_end + *relOffsetOpt; // RIP-relative calculation
            return finalAddr;
        }
        catch (const std::exception& e) {
            Logger::Error("Scan Calc2 Exception: " + std::string(e.what()));
            return std::nullopt;
        }
    }

} // end anonymous namespace



// Constructor
RLSDK::RLSDK(const std::wstring& processName, bool hookPlayerTick)
    : hookMgr_(&memManager_, &gobjects_, &gnames_, &eventMgr_), 
    initialized_(false),
    shouldHookPlayerTick_(hookPlayerTick),
    moduleName_(processName),
    moduleBase_(0),
    processId_(0),
    gnamesOffset_(0),
    gobjectsOffset_(0),
    buildType_("unknown"),
    currentGameEvent_(0) // Initialize with invalid address
{
    Logger::Info("RLSDK: Initializing...");
    try {
        // 1. Attach to Process
        Logger::Info("RLSDK: Attaching to process: " + std::string(moduleName_.begin(), moduleName_.end()));
        if (!memManager_.Attach(moduleName_)) {
            throw std::runtime_error("Failed to attach to process: " + std::string(moduleName_.begin(), moduleName_.end()));
        }
        processId_ = memManager_.GetProcessId();

        // Get Module Info (Base Address and Size)
        MODULEINFO moduleInfo = { 0 };
        HMODULE hModule = reinterpret_cast<HMODULE>(memManager_.GetModuleBaseAddress(moduleName_));
        bool gotModuleInfo = hModule && GetModuleInformation(memManager_.GetProcessHandle(), hModule, &moduleInfo, sizeof(moduleInfo));
        moduleBase_ = reinterpret_cast<uintptr_t>(hModule); // Use base address regardless

        if (!gotModuleInfo) {
            if (moduleBase_ == 0) throw std::runtime_error("Failed to get module base address for: " + std::string(moduleName_.begin(), moduleName_.end()));
            Logger::Warning("Failed to get module size information. Using estimated scan size.");
            moduleInfo.SizeOfImage = 150 * 1024 * 1024; // Estimate size (adjust if needed)
        }
        else {
            moduleBase_ = reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll); // Use precise base if available
        }
        if (moduleBase_ == 0) throw std::runtime_error("Failed to obtain module base address.");

        Logger::Info("RLSDK: Attached - PID: " + std::to_string(processId_) +
            ", Base: " + Logger::to_hex(moduleBase_) +
            ", Scan Size: " + Logger::to_hex(moduleInfo.SizeOfImage));

        // 2. Detect Build Type
        buildType_ = DetectBuildType();
        Logger::Info("RLSDK: Detected build type: " + buildType_);

        // 3. Determine Offsets (Always resolve now)
        Logger::Info("RLSDK: Resolving offsets via pattern scanning...");
        if (!ResolveOffsets(moduleInfo.SizeOfImage)) { // Call the overloaded ResolveOffsets
            throw std::runtime_error("Failed to resolve required GNames/GObjects offsets via pattern scanning.");
        }
        if (gnamesOffset_ == 0 || gobjectsOffset_ == 0) {
            throw std::runtime_error("Resolved GNames/GObjects offsets are invalid (zero).");
        }

        // 4. Initialize GNameTable
        Logger::Info("RLSDK: Initializing GNameTable...");
        if (!gnames_.Initialize(memManager_, moduleBase_, gnamesOffset_)) {
            throw std::runtime_error("Failed to initialize GNameTable.");
        }
        Logger::Info("RLSDK: GNameTable Initialized. Loaded " + std::to_string(gnames_.GetNameCount()) + " names.");

        // 5. Initialize GObjectsTable
        Logger::Info("RLSDK: Initializing GObjectsTable...");
        if (!gobjects_.Initialize(memManager_, gnames_, moduleBase_, gobjectsOffset_)) {
            throw std::runtime_error("Failed to initialize GObjectsTable.");
        }
        Logger::Info("RLSDK: GObjectsTable Initialized. Mapped "
            + std::to_string(gobjects_.GetMappedClassCount()) + " classes, "
            + std::to_string(gobjects_.GetMappedFunctionCount()) + " functions.");

        // --- ADDED: Find the main UGameViewportClient instance ---
        Logger::Info("RLSDK: Searching for main UGameViewportClient instance...");
        int32_t objectCount = gobjects_.GetObjectCount(memManager_);
        bool foundViewport = false;
        for (int32_t i = 0; i < objectCount; ++i) {
            SDK::UObject obj = gobjects_.GetObjectByIndex(memManager_, i);
            if (obj.IsValid()) {
                std::string objName = obj.GetName(memManager_, gnames_); // Get simple name first
                if (objName == "GameViewportClient") { // Check simple name
                     // Optional: Check full name or class name for confirmation
                     std::string objFullName = obj.GetFullName(memManager_, gnames_);
                     if (objFullName.find(ClassName::GameViewportClient) != std::string::npos) {
                          g_GameViewportClientAddress = obj.Address;
                          Logger::Info("RLSDK: Found main GameViewportClient instance at: " + Logger::to_hex(g_GameViewportClientAddress));
                          foundViewport = true;
                          break; // Assume the first one found is the main one
                     }
                }
            }
        }
        if (!foundViewport) {
             Logger::Warning("RLSDK: Could not find the main UGameViewportClient instance in GObjects! Tick hook might not work correctly.");
             // Proceeding anyway, the hook will just filter everything if address is 0
        }
        // --- END Find UGameViewportClient ---

        // 6. Initialize HookManager (MinHook)
        Logger::Info("RLSDK: Initializing HookManager...");
        if (!hookMgr_.Initialize()) {
            throw std::runtime_error("Failed to initialize HookManager (MinHook)."); // Specific error logged inside
        }

        // 7. Setup Hooks
        Logger::Info("RLSDK: Setting up hooks...");
        if (!SetupHooks()) {
            Logger::Warning("Failed to set up one or more function hooks.");
        }

        // 8. Subscribe internal handlers AFTER hooks are set
        // REMOVED OLD GameEventStarted/Destroyed subscriptions
        /*
        eventMgr_.Subscribe(EventType::OnGameEventStarted, [this](const EventData& data) {
            if (const auto* startData = dynamic_cast<const EventGameEventStartedData*>(&data)) {
                this->UpdateCurrentGameEvent(startData->GameEventAddress);
            }
        });
        eventMgr_.Subscribe(EventType::OnGameEventDestroyed, [this](const EventData& data) {
            if (const auto* destroyData = dynamic_cast<const EventGameEventDestroyedData*>(&data)) {
                this->NotifyGameEventDestroyed(destroyData->GameEventAddress);
            }
        });
        */

        // --- REMOVED Subscribers for Started/Destroyed/Tick --- 
        /*
        // --- Subscribe to GameEventStarted (potentially from heuristic) --- 
        eventMgr_.Subscribe(EventType::OnGameEventStarted, [this](const EventData& data) {
            const auto* startData = dynamic_cast<const EventGameEventStartedData*>(&data);
            if (!startData) return;

            uintptr_t previousGEAddress = this->currentGameEvent_.Address;
            uintptr_t newGEAddress = startData->GameEventAddress;

            // Only process if the address is actually new OR if we currently have no event
            if (newGEAddress != 0 && newGEAddress != previousGEAddress) {
                Logger::Info("RLSDK: OnGameEventStarted received. Updating pointer. Prev: " + 
                           Logger::to_hex(previousGEAddress) + ", New: " + Logger::to_hex(newGEAddress));
                // If there was a different valid previous event, notify destroyed
                if (previousGEAddress != 0) {
                    this->NotifyGameEventDestroyed(previousGEAddress);
                    EventGameEventDestroyedData destroyEvent(previousGEAddress);
                    this->eventMgr_.Fire(EventType::OnGameEventDestroyed, destroyEvent); // Fire public destroyed
                }
                this->UpdateCurrentGameEvent(newGEAddress); // Update internal pointer
                // Note: The public OnGameEventStarted was already fired by the hook's heuristic
            }
            // else: Ignore if address is 0 or same as current
        });

        // --- Subscribe to GameEventDestroyed --- 
        // We still need to handle explicit destroyed events if they occur
        eventMgr_.Subscribe(EventType::OnGameEventDestroyed, [this](const EventData& data) {
             const auto* destroyData = dynamic_cast<const EventGameEventDestroyedData*>(&data);
             if (!destroyData) return;
             if (this->currentGameEvent_.Address == destroyData->GameEventAddress) { // Only clear if it matches current
                Logger::Info("RLSDK: OnGameEventDestroyed received. Clearing pointer for: " + Logger::to_hex(destroyData->GameEventAddress));
                this->NotifyGameEventDestroyed(destroyData->GameEventAddress); // Update internal pointer
                // Note: Public OnGameEventDestroyed was already fired
             }
         });

        // --- Subscribe to ViewportTick (for frame-by-frame update) --- 
        eventMgr_.Subscribe(EventType::OnViewportTick, [this](const EventData& data) {
            // Logger::Info("**** RLSDK OnViewportTick Handler ENTERED ****"); // Can be noisy
            const auto* tickData = dynamic_cast<const EventViewportTickData*>(&data);
            if (!tickData) {
                 Logger::Warning("RLSDK OnViewportTick Handler: Failed dynamic_cast to EventViewportTickData!");
                 return; 
            }

            uintptr_t previousGEAddress = this->currentGameEvent_.Address;
            uintptr_t currentGEAddressFromTick = tickData->CurrentGameEventAddress;

            if (currentGEAddressFromTick != previousGEAddress) {
                 // Log the change for debugging
                Logger::Info("RLSDK: Detected GameEvent change via Viewport Tick. Prev: " + 
                           Logger::to_hex(previousGEAddress) + ", CurrFromTick: " + Logger::to_hex(currentGEAddressFromTick));

                // If previous was valid and current is null -> Destroyed
                if (previousGEAddress != 0 && currentGEAddressFromTick == 0) { 
                    this->NotifyGameEventDestroyed(previousGEAddress); 
                    EventGameEventDestroyedData destroyEvent(previousGEAddress);
                    this->eventMgr_.Fire(EventType::OnGameEventDestroyed, destroyEvent);
                }
                 // If current is valid (and different from previous) -> Started
                else if (currentGEAddressFromTick != 0) { 
                     // If previous was also valid (but different), fire destroyed first
                     if (previousGEAddress != 0) {
                         this->NotifyGameEventDestroyed(previousGEAddress); 
                         EventGameEventDestroyedData destroyEvent(previousGEAddress);
                         this->eventMgr_.Fire(EventType::OnGameEventDestroyed, destroyEvent);
                     }
                     // Update internal pointer and fire started event
                     this->UpdateCurrentGameEvent(currentGEAddressFromTick); 
                     EventGameEventStartedData startEvent(currentGEAddressFromTick);
                     this->eventMgr_.Fire(EventType::OnGameEventStarted, startEvent);
                 }
                 // Case: previous was 0 and current is 0 - do nothing
                 // Case: previous was valid and current is valid but different handled above
                 // Case: previous was 0 and current is valid handled above
            }
            // else: Address from tick is same as current, no change needed.
        });
        */

        // --- Subscribe ONLY to OnProbableGameEventFound --- 
        eventMgr_.Subscribe(EventType::OnProbableGameEventFound, [this](const EventData& data) {
            const auto* geFoundData = dynamic_cast<const EventProbableGameEventFoundData*>(&data);
            if (!geFoundData) {
                Logger::Warning("RLSDK OnProbableGameEventFound Handler: Failed dynamic_cast!");
                return;
            }

            uintptr_t previousGEAddress = this->currentGameEvent_.Address;
            uintptr_t probableGEAddress = geFoundData->ProbableGameEventAddress;

            // --- ADD Verification Step --- 
            bool verified = false;
            if (probableGEAddress != 0) { // Only try to verify non-null addresses
                SDK::AGameEvent probableGE(probableGEAddress);
                // Use the specific Soccar game event type for verification
                if (probableGE.IsA(this->GetMemoryManager(), this->GetGNameTable(), "GameEvent_Soccar_TA")) {
                    verified = true;
                }
                else {
                     Logger::Info("RLSDK: Heuristic found candidate 0x" + Logger::to_hex(probableGEAddress) + " but IsA(GameEvent_Soccar_TA) failed verification.");
                }
            }
            else { // If probable address is 0, consider it 'verified' as the end state
                verified = true; 
            }
            // --- END Verification --- 

            // Only update state if the address is different AND verified
            if (verified && probableGEAddress != previousGEAddress) {
                Logger::Info("RLSDK: Verified GameEvent address changed. Prev: " + 
                           Logger::to_hex(previousGEAddress) + ", New: " + Logger::to_hex(probableGEAddress));

                // If previous was valid, fire destroyed event
                if (previousGEAddress != 0) {
                    this->NotifyGameEventDestroyed(previousGEAddress); // Update internal pointer first (sets to 0)
                    EventGameEventDestroyedData destroyEvent(previousGEAddress);
                    this->eventMgr_.Fire(EventType::OnGameEventDestroyed, destroyEvent);
                    Logger::Info("RLSDK: Fired OnGameEventDestroyed for " + Logger::to_hex(previousGEAddress));
                }
                
                // Update internal pointer regardless of whether new address is valid or 0
                this->UpdateCurrentGameEvent(probableGEAddress); 

                // If new address is valid, fire started event
                if (probableGEAddress != 0) {
                    EventGameEventStartedData startEvent(probableGEAddress);
                    this->eventMgr_.Fire(EventType::OnGameEventStarted, startEvent);
                    Logger::Info("RLSDK: Fired OnGameEventStarted for " + Logger::to_hex(probableGEAddress));
                }
            }
            // else: Probable address is the same as current, or failed verification. Do nothing.
        });

        initialized_ = true;
        Logger::Info("RLSDK: Initialization successful.");

    }
    catch (const std::exception& e) {
        Logger::Error("RLSDK Initialization Error: " + std::string(e.what()));
        Shutdown(); // Ensure partial cleanup
        throw; // Re-throw
    }
    catch (...) {
        Logger::Error("RLSDK Initialization Error: An unknown exception occurred.");
        Shutdown(); // Ensure partial cleanup
        throw std::runtime_error("Unknown error during RLSDK initialization.");
    }
}

// --- Destructor ---
RLSDK::~RLSDK() {
    Shutdown();
}

// --- Shutdown ---
void RLSDK::Shutdown() {
    // Ensure graceful shutdown even if called multiple times or before full init
    if (!initialized_ && !memManager_.IsAttached()) return;
    Logger::Info("RLSDK: Shutting down...");
    hookMgr_.Shutdown();      // Shutdown hooks first
    memManager_.Detach();     // Detach from process
    initialized_ = false;     // Mark as not initialized
    currentGameEvent_ = SDK::AGameEvent(0); // Reset game event pointer
    Logger::Info("RLSDK: Shutdown complete.");
}

// --- Initialization Status ---
bool RLSDK::IsInitialized() const {
    return initialized_;
}

// --- Accessors ---
MemoryManager& RLSDK::GetMemoryManager() { return memManager_; }
const MemoryManager& RLSDK::GetMemoryManager() const { return memManager_; }
GNameTable& RLSDK::GetGNameTable() { return gnames_; }
const GNameTable& RLSDK::GetGNameTable() const { return gnames_; }
GObjectsTable& RLSDK::GetGObjectsTable() { return gobjects_; }
const GObjectsTable& RLSDK::GetGObjectsTable() const { return gobjects_; }
EventManager& RLSDK::GetEventManager() { return eventMgr_; }
const EventManager& RLSDK::GetEventManager() const { return eventMgr_; }
HookManager& RLSDK::GetHookManager() { return hookMgr_; }
const HookManager& RLSDK::GetHookManager() const { return hookMgr_; }
uintptr_t RLSDK::GetModuleBaseAddress() const { return moduleBase_; }
DWORD RLSDK::GetProcessID() const { return processId_; }
const std::wstring& RLSDK::GetModuleName() const { return moduleName_; }
const std::string& RLSDK::GetBuildType() const { return buildType_; }

// --- SDK Helper Functions ---
std::string RLSDK::GetName(int32_t index) const {
    return gnames_.GetName(index);
}
SDK::UClass RLSDK::FindStaticClass(const std::string& fullName) const {
    return gobjects_.FindStaticClass(fullName);
}
SDK::UFunction RLSDK::FindStaticFunction(const std::string& fullName) const {
    return gobjects_.FindStaticFunction(fullName);
}
SDK::AGameEvent RLSDK::GetCurrentGameEvent() const {
    return currentGameEvent_;
}
void RLSDK::Subscribe(const std::string& eventType, EventManager::Callback callback) {
    eventMgr_.Subscribe(eventType, callback);
}
void RLSDK::Unsubscribe(const std::string& eventType, const EventManager::Callback& callback) {
    eventMgr_.Unsubscribe(eventType, callback);
}

// --- Update Callbacks for Hooks ---
void RLSDK::UpdateCurrentGameEvent(uintptr_t gameEventAddress) {
    // ADDED LOG
    Logger::Info("**** RLSDK::UpdateCurrentGameEvent called with address: " + Logger::to_hex(gameEventAddress) + " ****"); 
    if (currentGameEvent_.Address != gameEventAddress) {
        currentGameEvent_ = SDK::AGameEvent(gameEventAddress);
        Logger::Info("RLSDK: Current GameEvent pointer updated to " + Logger::to_hex(gameEventAddress));
    }
}
void RLSDK::NotifyGameEventDestroyed(uintptr_t gameEventAddress) {
    // ADDED LOG
    Logger::Info("**** RLSDK::NotifyGameEventDestroyed called for address: " + Logger::to_hex(gameEventAddress) + " ****");
    if (currentGameEvent_.Address == gameEventAddress) {
        currentGameEvent_ = SDK::AGameEvent(0);
        Logger::Info("RLSDK: Current GameEvent pointer cleared (Destroyed: " + Logger::to_hex(gameEventAddress) + ")");
    }
}

// --- Private Initialization Helpers ---
// LoadConfig/SaveConfig removed

std::string RLSDK::DetectBuildType() {
    char processPathRaw[MAX_PATH] = { 0 };
    if (GetModuleFileNameExA(memManager_.GetProcessHandle(), NULL, processPathRaw, MAX_PATH) == 0) {
        DWORD error = GetLastError();
        Logger::Warning("Could not get process executable path (Error: " + std::to_string(error) + "). Assuming 'epic'.");
        return "epic";
    }
    std::string processPath(processPathRaw);
    std::transform(processPath.begin(), processPath.end(), processPath.begin(), ::tolower);
    if (processPath.find("steam") != std::string::npos) return "steam";
    else if (processPath.find("epic games") != std::string::npos || processPath.find("epicgames") != std::string::npos) return "epic";
    else { Logger::Warning("Could not determine build type from path '" + std::string(processPathRaw) + "'. Assuming 'epic'."); return "epic"; }
}

bool RLSDK::ResolveOffsets(size_t moduleSize) {
    std::optional<uintptr_t> gnamesAddrOpt;
    std::optional<uintptr_t> gobjectsAddrOpt;

    Logger::Info("RLSDK Resolve: Trying offset method 1...");
    auto gnamesPattern1Opt = PatternScan(memManager_, moduleBase_, moduleSize, SCAN_PATTERNS.at("GNames_1"), "GNames_1");
    if (gnamesPattern1Opt) {
        gnamesAddrOpt = CalculateMethod1(memManager_, *gnamesPattern1Opt, true);
        if (gnamesAddrOpt) Logger::Info("Found GNames via Method 1 at: " + Logger::to_hex(*gnamesAddrOpt));
    }

    auto gobjectsPattern1Opt = PatternScan(memManager_, moduleBase_, moduleSize, SCAN_PATTERNS.at("GObjects_1"), "GObjects_1");
    if (gobjectsPattern1Opt) {
        gobjectsAddrOpt = CalculateMethod1(memManager_, *gobjectsPattern1Opt, false);
        if (gobjectsAddrOpt) Logger::Info("Found GObjects via Method 1 at: " + Logger::to_hex(*gobjectsAddrOpt));
    }

    // --- Try Alternative Method 2 (Matches Python GNames_2/GObjects_2) --- 
    if (!gnamesAddrOpt) {
        Logger::Info("RLSDK Resolve: GNames Method 1 failed, trying Method 2 (GNames_2)...");
        auto gnamesPattern2Opt = PatternScan(memManager_, moduleBase_, moduleSize, SCAN_PATTERNS.at("GNames_2"), "GNames_2");
        if (gnamesPattern2Opt) {
            gnamesAddrOpt = CalculateMethod2(memManager_, *gnamesPattern2Opt);
            if (gnamesAddrOpt) Logger::Info("Found GNames via Method 2 at: " + Logger::to_hex(*gnamesAddrOpt));
        }
    }

    if (!gobjectsAddrOpt) {
        Logger::Info("RLSDK Resolve: GObjects Method 1 failed, trying Method 2 (GObjects_2)...");
        auto gobjectsPattern2Opt = PatternScan(memManager_, moduleBase_, moduleSize, SCAN_PATTERNS.at("GObjects_2"), "GObjects_2");
        if (gobjectsPattern2Opt) {
            gobjectsAddrOpt = CalculateMethod2(memManager_, *gobjectsPattern2Opt);
             if (gobjectsAddrOpt) Logger::Info("Found GObjects via Method 2 at: " + Logger::to_hex(*gobjectsAddrOpt));
        }
    }
    // Add more alternative methods here if needed, like GObjects_3

    // --- Final Check and Adjustment --- 
    if (!gnamesAddrOpt || !gobjectsAddrOpt) {
        Logger::Error("RLSDK Resolve Error: Failed to find both GNames and GObjects addresses via any method.");
        return false;
    }

    uintptr_t gnamesAddr = *gnamesAddrOpt;
    uintptr_t gobjectsAddr = *gobjectsAddrOpt;

    Logger::Info("RLSDK Resolve: Found absolute addresses: GNames=" + Logger::to_hex(gnamesAddr) +
        ", GObjects=" + Logger::to_hex(gobjectsAddr));

    // Perform the 0x48 difference check and adjustment (like Python)
    constexpr uintptr_t EXPECTED_DIFF = 0x48;
    uintptr_t currentDiff = (gobjectsAddr > gnamesAddr) ? (gobjectsAddr - gnamesAddr) : (gnamesAddr - gobjectsAddr);

    if (currentDiff != EXPECTED_DIFF) {
        Logger::Warning("RLSDK Resolve: Offset difference is " + Logger::to_hex(currentDiff) +
            ", expected " + Logger::to_hex(EXPECTED_DIFF) + ". Adjusting GNames based on GObjects.");
        if (gobjectsAddr > EXPECTED_DIFF) {
            gnamesAddr = gobjectsAddr - EXPECTED_DIFF;
            Logger::Info("RLSDK Resolve: Adjusted GNames absolute address: " + Logger::to_hex(gnamesAddr));
        }
        else {
            Logger::Error("RLSDK Resolve Error: Cannot adjust GNames, GObjects address too low (" + Logger::to_hex(gobjectsAddr) + ").");
            return false;
        }
    }

    if (gnamesAddr < moduleBase_ || gobjectsAddr < moduleBase_) {
        Logger::Error("RLSDK Resolve Error: Calculated absolute addresses lower than module base.");
        return false;
    }
    gnamesOffset_ = gnamesAddr - moduleBase_;
    gobjectsOffset_ = gobjectsAddr - moduleBase_;

    Logger::Info("RLSDK Resolve: Successfully resolved relative offsets: GNames=" + Logger::to_hex(gnamesOffset_) +
        ", GObjects=" + Logger::to_hex(gobjectsOffset_));
    return true;
}

bool RLSDK::SetupHooks() {
    bool allHooksSuccessful = true;
    Logger::Info("RLSDK: Setting up ProcessEvent hook...");

    // --- Find Core.Object Class --- 
    SDK::UClass coreObjectClass = gobjects_.FindStaticClass(ClassName::CoreObject);
    if (!coreObjectClass.IsValid()) {
        Logger::Error("SetupHooks Error: Could not find 'Class Core.Object'. Cannot find ProcessEvent.");
        return false;
    }
    // Logger::Info("Found Core.Object UClass at: " + Logger::to_hex(coreObjectClass.Address)); // Less verbose

    // --- Get VTable Address --- 
    uintptr_t vtableAddress = memManager_.Read<uintptr_t>(coreObjectClass.Address).value_or(0);
    if (vtableAddress == 0) {
        Logger::Error("SetupHooks Error: Failed to read VTable address from Core.Object UClass at offset 0.");
        return false;
    }
    // Logger::Info("Potential VTable address read: " + Logger::to_hex(vtableAddress)); // Less verbose

    // --- Get ProcessEvent Address from VTable --- 
    constexpr int processEventVTableIndex = 67; 
    uintptr_t processEventAddress = memManager_.Read<uintptr_t>(vtableAddress + (sizeof(uintptr_t) * processEventVTableIndex)).value_or(0);
    
    if (processEventAddress == 0) {
        Logger::Error("SetupHooks Error: Failed to read ProcessEvent function pointer from VTable index " + std::to_string(processEventVTableIndex));
        return false;
    }
    // Logger::Info("Found potential ProcessEvent address: " + Logger::to_hex(processEventAddress)); // Less verbose

    // --- Hook ProcessEvent --- 
    if (!hookMgr_.CreateAndEnableHook(ClassName::ProcessEvent, reinterpret_cast<LPVOID>(Hook_ProcessEvent), processEventAddress)) {
        allHooksSuccessful = false; 
    }

    // --- MODIFIED: Hook GameViewportClient::Tick via VTable --- 
    /* // REMOVE THIS BLOCK
    Logger::Info("RLSDK: Setting up GameViewportClient::Tick hook via VTable...");
    constexpr int viewportTickVTableIndex = 71; // Common index for Tick
    uintptr_t viewportTickAddr = memManager_.Read<uintptr_t>(vtableAddress + (sizeof(uintptr_t) * viewportTickVTableIndex)).value_or(0);

    if (viewportTickAddr != 0) {
         Logger::Info("Found potential GameViewportClient::Tick address via VTable: " + Logger::to_hex(viewportTickAddr));
         if (!hookMgr_.CreateAndEnableHook("Engine.GameViewportClient.Tick", reinterpret_cast<LPVOID>(Hook_GameViewportClient_Tick), viewportTickAddr)) {
             Logger::Error("SetupHooks Error: Failed to create hook for GameViewportClient::Tick via VTable.");
             // Decide if this failure is critical
             allHooksSuccessful = false; // Mark as failure if tick hook fails
         }
    } else {
        Logger::Warning("SetupHooks Warning: Could not read GameViewportClient::Tick address from VTable index "
                       + std::to_string(viewportTickVTableIndex) + ". GameEvent detection will not work.");
        allHooksSuccessful = false; // This hook is critical
    }
    */
    // --- End Modified Hook ---

    // --- REMOVE OLD INDIVIDUAL HOOKS --- 
    /*
    allHooksSuccessful &= setupHook(FunctionName::BoostPickedUp, Hook_VehiclePickup_Boost_Idle_EndState);
    allHooksSuccessful &= setupHook(FunctionName::BoostRespawn, Hook_VehiclePickup_Boost_Idle_BeginState);
    allHooksSuccessful &= setupHook(FunctionName::RoundActiveBegin, Hook_GameEvent_Active_BeginState);
    allHooksSuccessful &= setupHook(FunctionName::RoundActiveEnd, Hook_GameEvent_Active_EndState);
    allHooksSuccessful &= setupHook(FunctionName::ResetPickups, Hook_GameEvent_ResetPickups);
    allHooksSuccessful &= setupHook(FunctionName::GameEventBeginPlay, Hook_GameEvent_PostBeginPlay);
    allHooksSuccessful &= setupHook(FunctionName::HandleKeyPress, Hook_GameViewportClient_HandleKeyPress);
    allHooksSuccessful &= setupHook(FunctionName::ViewportClientTick, Hook_GameViewportClient_Tick);
    allHooksSuccessful &= setupHook(FunctionName::GameEventDestroyed, Hook_GameEvent_Destroyed);
    if (shouldHookPlayerTick_) {
        allHooksSuccessful &= setupHook(FunctionName::PlayerTick, Hook_PlayerController_PlayerTick);
    }
    */

    Logger::Info("RLSDK: Hook setup finished.");
    return allHooksSuccessful;
}

// Implementation of GetPing method
int RLSDK::GetPing() const {
    // If the SDK is not initialized or we don't have a current game event, return -1
    if (!initialized_ || !currentGameEvent_.IsValid()) {
        return -1;
    }
    
    // Try to get the PRI for the local player
    auto localPlayers = currentGameEvent_.GetLocalPlayers(memManager_);
    if (localPlayers.empty()) {
        return -1;
    }
    
    // Get the first local player's PRI
    auto playerController = localPlayers[0];
    auto pri = playerController.GetPRIActor(memManager_);
    if (!pri.IsValid()) {
        return -1; 
    }
    
    // Return the ping value
    return pri.GetPing(memManager_);
}

// Implementation of GetPlayerInput method
std::optional<SDK::UObject> RLSDK::GetPlayerInput(std::optional<SDK::APlayerReplicationInfo> priOpt) const {
    if (!initialized_) {
        return std::nullopt;
    }
    
    SDK::APlayerReplicationInfo pri = priOpt.value_or(SDK::APlayerReplicationInfo(0));
    
    // If no PRI was provided, try to get the one from the current game event
    if (!pri.IsValid() && currentGameEvent_.IsValid()) {
        auto localPlayers = currentGameEvent_.GetLocalPlayers(memManager_);
        if (!localPlayers.empty()) {
            // localPlayers already contains APlayerController objects
            pri = localPlayers[0].GetPRI(memManager_);
        }
    }
    
    if (!pri.IsValid()) {
        return std::nullopt;
    }
    
    // Find the PlayerController for this PRI
    std::vector<SDK::AController> allPlayers;
    if (currentGameEvent_.IsValid()) {
        allPlayers = currentGameEvent_.GetPlayers(memManager_);
    }
    
    for (const auto& controller : allPlayers) {
        // First check if this controller is a player controller
        if (controller.IsA(memManager_, gnames_, "PlayerController")) {
            // Cast to PlayerController
            SDK::APlayerController pc(controller.Address);
            
            // Now get the PRI and check if it matches
            auto pc_pri = pc.GetPRI(memManager_);
            if (pc_pri.IsValid() && pc_pri.Address == pri.Address) {
                // This is simplified - you would need to find the actual PlayerInput object
                // through the appropriate property of the PlayerController
                return SDK::UObject(pc.Address + 0x2B8); // Example offset - replace with actual PlayerInput offset
            }
        }
    }
    
    return std::nullopt;
}