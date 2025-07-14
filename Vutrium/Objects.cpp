#define NOMINMAX
#include "Objects.hpp"      // Include the header with declarations
#include "MemoryManager.h"  // Include the MemoryManager definition
#include "GNameTable.h"     // Include your GNameTable definition (replace if needed)
#include "Logger.h"

#include <limits>           // For numeric_limits if needed for default returns
#include <vector>
#include <string>
#include <cmath>            // For FBox calculations, std::abs
#include <algorithm>        // For std::max in BoostPadState
#include "windows.h"


namespace SDK {

    /* // REMOVED - FName struct is gone
    std::string FName::ToString(const MemoryManager& pm, const GNameTable& gnames) const {
        // Assumes gnames handles IsInitialized check
        return gnames.GetName(this->FNameEntryId);
    }
    */


    // GetName: Reads the FNameEntryId directly from UObject and asks GNameTable for the string
    std::string UObject::GetName(const MemoryManager& pm, const GNameTable& gnames) const {
        if (!IsValid()) return "[InvalidUObject]";

        // Read the FNameEntryId directly using the updated Offset_Name
        auto entryIdOpt = pm.Read<int32_t>(this->Address + Offset_Name);
        if (!entryIdOpt) {
            return "[ReadFail:FNameEntryId]";
        }

        int32_t entryId = *entryIdOpt;
        std::string resultName = gnames.GetName(entryId); // Ask GNameTable for the name

        // REMOVED GetName Debug Logging

        return resultName; 
    }

    // GetFName: Helper to return the actual FName struct // REMOVED IMPL
    /*
    std::optional<FName> UObject::GetFName(const MemoryManager& pm) const {
         if (!IsValid()) return std::nullopt;
         return pm.Read<FName>(Address + Offset_Name);
    }
    */

    // GetIndex: Reads the ObjectInternalInteger
    int32_t UObject::GetIndex(const MemoryManager& pm) const {
        if (!IsValid()) return -1;
        // Read using the new offset for the index
        auto indexOpt = pm.Read<int32_t>(Address + Offset_InternalInteger);
        return indexOpt.value_or(-1);
    }

    // GetOuterAddress: Reads the Outer pointer
    uintptr_t UObject::GetOuterAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Read using the correct offset for Outer
        auto outerAddrOpt = pm.Read<uintptr_t>(Address + Offset_Outer);
        return outerAddrOpt.value_or(0);
    }

    // GetOuter: Wraps GetOuterAddress
    UObject UObject::GetOuter(const MemoryManager& pm) const {
        return UObject(GetOuterAddress(pm));
    }

    // GetClassAddress: Reads the Class pointer
    uintptr_t UObject::GetClassAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Read using the correct offset for Class
        uintptr_t classAddrPtr = Address + Offset_Class;
        auto classAddrOpt = pm.Read<uintptr_t>(classAddrPtr);
        uintptr_t readClassAddr = classAddrOpt.value_or(0);

        // REMOVED GetClassAddress Debug Logging

        return readClassAddr;
    }

    // GetClass: Wraps GetClassAddress
    UClass UObject::GetClass(const MemoryManager& pm) const {
        return UClass(GetClassAddress(pm));
    }

    // GetFullName: Rebuild based on GameDefines.cpp logic using new members/offsets
    std::string UObject::GetFullName(const MemoryManager& pm, const GNameTable& gnames) const {
        if (!IsValid()) return "None"; // Object pointer invalid

        // 1. Get the object's own base name using the new GetName method
        std::string baseName = GetName(pm, gnames);
        if (baseName.empty() || baseName == "None" || baseName[0] == '[') {
            // Handle error/invalid base name
            return baseName.empty() ? "[EmptyBaseName]" : baseName;
        }

        // 2. Build the Outer path string recursively (like GameDefines.cpp)
        std::string outerPath = "";
        try {
            UObject currentOuter = GetOuter(pm);
            int depth = 0;
            const int maxDepth = 16; // Safety limit
            while (currentOuter.IsValid() && depth < maxDepth) {
                // Prevent potential infinite loops
                if (currentOuter.Address == this->Address) {
                    Logger::Warning("GetFullName detected self-referential Outer loop for " + Logger::to_hex(this->Address));
                    outerPath = "[SelfLoop]." + outerPath;
                    break;
                }

                std::string currentOuterName = currentOuter.GetName(pm, gnames);
                if (currentOuterName.empty() || currentOuterName == "None" || currentOuterName[0] == '[') {
                    outerPath = "[OuterNameError]." + outerPath;
                    break;
                }

                outerPath = currentOuterName + "." + outerPath;
                currentOuter = currentOuter.GetOuter(pm);
                depth++;
            }
             if (depth >= maxDepth) {
                 Logger::Warning("GetFullName Outer loop reached max depth for " + Logger::to_hex(this->Address));
                 outerPath = "[MaxDepth]." + outerPath;
            }
        }
        catch (const std::exception& e) {
            Logger::Error("GetFullName Exc in Outer loop for " + Logger::to_hex(this->Address) + ": " + e.what());
            outerPath = "[OuterPathError].";
        }
        catch (...) {
            Logger::Error("GetFullName Unk Exc in Outer loop for " + Logger::to_hex(this->Address));
            outerPath = "[OuterPathError].";
        }

        // 3. Get the Class Name (this object's class)
        std::string className = "UnknownClass";
        try {
            UClass objClass = GetClass(pm);
            if (objClass.IsValid()) {
                className = objClass.GetName(pm, gnames); // Calls updated GetName for the class object
                if (className.empty() || className == "None" || className[0] == '[') {
                    className = "[ClassNameError]";
                }
            }
        }
        catch (const std::exception& e) {
            Logger::Error("GetFullName Exc getting Class name for " + Logger::to_hex(this->Address) + ": " + e.what());
            className = "[ClassNameError]";
        }
        catch (...) {
            Logger::Error("GetFullName Unk Exc getting Class name for " + Logger::to_hex(this->Address));
            className = "[ClassNameError]";
        }

        // 4. Combine: "ClassName OuterPath.BaseName" (GameDefines format)
            return className + " " + outerPath + baseName;
        }

    // IsA: Rebuild based on GameDefines.cpp logic using SuperField
    bool UObject::IsA(const MemoryManager& pm, const GNameTable& gnames, const std::string& simple_class_name_to_check) const {
        if (!IsValid() || simple_class_name_to_check.empty()) return false;

        // REMOVED IsA Debug Logging Entry

        // Get the target class UClass object using GObjectsTable lookup (if available) or find dynamically?
        // For simplicity, compare names for now. Direct UClass comparison is better if FindClass works.
        // TODO: Potentially use GObjectsTable::FindStaticClass if available

        const int maxHierarchyDepth = 64;
        int depth = 0;
        try {
            UClass currentClass = GetClass(pm);
        while (currentClass.IsValid() && depth < maxHierarchyDepth) {
            std::string currentSimpleName = currentClass.GetName(pm, gnames);
                
                // REMOVED IsA Debug Logging Comparison
                
                if (currentSimpleName == simple_class_name_to_check) {
                    // REMOVED IsA Debug Logging Match Found
                    return true;
                }
                // Traverse up using SuperField (needs GetSuperClass method)
            currentClass = currentClass.GetSuperClass(pm);
            depth++;
        }
             if (depth >= maxHierarchyDepth) {
                 // Avoid logging spam, maybe log once per object type?
                 // Logger::Warning("IsA check reached max depth for " + Logger::to_hex(this->Address));
             }
        }
        catch (const std::exception& e) {
            Logger::Error("IsA Exc traversing hierarchy for " + Logger::to_hex(this->Address) + ": " + e.what());
            return false;
        }
        catch (...) {
            Logger::Error("IsA Unk Exc traversing hierarchy for " + Logger::to_hex(this->Address));
            return false;
        }

        return false;
    }



    uintptr_t UField::GetNextAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Read using the verified/updated Offset_Next for UField
        auto nextAddrOpt = pm.Read<uintptr_t>(Address + Offset_Next);
        return nextAddrOpt.value_or(0);
    }
    UField UField::GetNext(const MemoryManager& pm) const {
        return UField(GetNextAddress(pm));
    }


    uintptr_t UStruct::GetSuperFieldAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Read using the verified/updated Offset_SuperField for UStruct
        auto superAddrOpt = pm.Read<uintptr_t>(Address + Offset_SuperField);
        return superAddrOpt.value_or(0);
    }

    UField UStruct::GetSuperField(const MemoryManager& pm) const {
        // Note: The SuperField might be a UStruct or UClass, returning as UField for generality
        return UField(GetSuperFieldAddress(pm));
    }

    uintptr_t UStruct::GetChildrenAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Read using the verified/updated Offset_Children for UStruct
        auto childrenAddrOpt = pm.Read<uintptr_t>(Address + Offset_Children);
        return childrenAddrOpt.value_or(0);
    }

    UField UStruct::GetChildren(const MemoryManager& pm) const {
        return UField(GetChildrenAddress(pm));
    }

    // GetSuperClass wraps GetSuperField, assuming SuperField points to a UClass
    UClass UClass::GetSuperClass(const MemoryManager& pm) const {
        // GetSuperFieldAddress is inherited from UStruct
        uintptr_t superAddr = GetSuperFieldAddress(pm);
        // Returns a UClass wrapper. If superAddr is 0, it constructs an invalid UClass.
        return UClass(superAddr);
    }

    // These methods should largely remain the same, as they inherit from UObject
    // and primarily read game-specific offsets relative to their own Address.
    // Double-check if any relied on specific old UObject internals.

    std::vector<ABall> AGameEvent::GetBalls(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<ABall>(pm, Address + Offset_Balls);
    }

    std::vector<ACar> AGameEvent::GetCars(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<ACar>(pm, Address + Offset_Cars);
    }

    std::vector<APRI> AGameEvent::GetPRIs(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<APRI>(pm, Address + Offset_PRIs);
    }

    std::vector<ATeam> AGameEvent::GetTeams(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<ATeam>(pm, Address + Offset_Teams);
    }

    std::vector<AController> AGameEvent::GetPlayers(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Note: This reads the general AController array. You might need GetPRIs for player stats.
        return GetTArrayItems<AController>(pm, Address + Offset_Players);
    }

    // ... (Keep other AGameEvent methods as they were, reading specific offsets) ...
    int32_t AGameEvent::GetGameTime(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_GameTime).value_or(0);
    }

    // ADD BACK MISSING AGAMEEVENT METHODS HERE
    int32_t AGameEvent::GetWarmupTime(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_WarmupTime).value_or(0);
    }

    int32_t AGameEvent::GetMaxScore(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_MaxScore).value_or(0);
    }

    float AGameEvent::GetTimeRemaining(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_TimeRemaining).value_or(0.0f);
    }

    int32_t AGameEvent::GetSecondsRemaining(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_SecondsRemaining).value_or(0);
    }

    float AGameEvent::GetTotalGameTimePlayed(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_TotalGameTimePlayed).value_or(0.0f);
    }

    float AGameEvent::GetOvertimePlayed(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_OvertimePlayed).value_or(0.0f);
    }

    // Internal helper macro for reading flags safely
#define GET_GAMEEVENT_FLAG_IMPL(pm, addr, offset, bit) \
    (addr != 0 ? ((pm.Read<uint32_t>(addr + offset).value_or(0) >> bit) & 1) : false)

// Bitfield flag implementations
    bool AGameEvent::IsRoundActive(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 0); }
    bool AGameEvent::IsPlayReplays(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 1); }
    bool AGameEvent::IsBallHasBeenHit(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 2); }
    bool AGameEvent::IsOvertime(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 3); }
    bool AGameEvent::IsUnlimitedTime(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 4); }
    bool AGameEvent::IsNoContest(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 5); }
    bool AGameEvent::IsDisableGoalDelay(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 6); }
    bool AGameEvent::IsShowNoScorerGoalMessage(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 7); }
    bool AGameEvent::IsMatchEnded(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 8); }
    bool AGameEvent::IsShowIntroScene(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 9); }
    bool AGameEvent::IsClubMatch(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 10); }
    bool AGameEvent::IsCanDropOnlineRewards(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 11); }
    bool AGameEvent::IsAllowHonorDuels(const MemoryManager& pm) const { return GET_GAMEEVENT_FLAG_IMPL(pm, Address, Offset_Flags, 12); }

#undef GET_GAMEEVENT_FLAG_IMPL // Undefine the helper macro

    ATeam AGameEvent::GetMatchWinner(const MemoryManager& pm) const {
        if (!IsValid()) return ATeam(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_MatchWinner);
        return ATeam(addrOpt.value_or(0));
    }

    ATeam AGameEvent::GetGameWinner(const MemoryManager& pm) const {
        // Assuming same offset as MatchWinner based on prior comments
        return GetMatchWinner(pm);
    }

    APRI AGameEvent::GetMVP(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_MVP);
        return APRI(addrOpt.value_or(0));
    }

    APRI AGameEvent::GetFastestGoalPlayer(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_FastestGoalPlayer);
        return APRI(addrOpt.value_or(0));
    }

    APRI AGameEvent::GetSlowestGoalPlayer(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_SlowestGoalPlayer);
        return APRI(addrOpt.value_or(0));
    }

    APRI AGameEvent::GetFurthestGoalPlayer(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_FurthestGoalPlayer);
        return APRI(addrOpt.value_or(0));
    }

    float AGameEvent::GetFastestGoalSpeed(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_FastestGoalSpeed).value_or(0.0f);
    }

    float AGameEvent::GetSlowestGoalSpeed(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_SlowestGoalSpeed).value_or(0.0f);
    }

    float AGameEvent::GetFurthestGoal(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_FurthestGoal).value_or(0.0f);
    }

    APRI AGameEvent::GetScoringPlayer(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_ScoringPlayer);
        return APRI(addrOpt.value_or(0));
    }

    int32_t AGameEvent::GetRoundNum(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_RoundNum).value_or(0);
    }

    APRI AGameEvent::GetGameOwner(const MemoryManager& pm) const {
        if (!IsValid()) return APRI(0);
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_GameOwner);
        return APRI(addrOpt.value_or(0));
    }

    int32_t AGameEvent::GetCountDownTime(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_CountDownTime).value_or(0);
    }

    std::vector<AGoal> AGameEvent::GetGoals(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<AGoal>(pm, Address + Offset_Goals);
    }

    std::vector<APlayerController> AGameEvent::GetLocalPlayers(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        return GetTArrayItems<APlayerController>(pm, Address + Offset_LocalPlayers);
    }

    // These should also be mostly fine, relying on game-specific offsets.

    FVectorData AActor::GetLocation(const MemoryManager& pm) const {
        if (!IsValid()) return {}; // Return default-initialized struct
        // Location might be indirect via RootComponent, verify offset validity
        // Assuming Offset_Location is correct for the game version
        return pm.Read<FVectorData>(Address + Offset_Location).value_or(FVectorData{});
    }

    FRotatorData AActor::GetRotation(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Rotation is correct
        return pm.Read<FRotatorData>(Address + Offset_Rotation).value_or(FRotatorData{});
    }

    FVectorData AActor::GetVelocity(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Velocity points directly to the FVectorData
        return pm.Read<FVectorData>(Address + Offset_Velocity).value_or(FVectorData{});
    }

    FVectorData AActor::GetAngularVelocity(const MemoryManager& pm) const {
        if (!IsValid()) return {};
       // Assuming Offset_AngularVelocity points directly to the FVectorData
        return pm.Read<FVectorData>(Address + Offset_AngularVelocity).value_or(FVectorData{});
    }

    // Need to update GetPlayerName to use the new GetFString helper (already done in header)
    // Other methods reading specific offsets should be okay.

    std::wstring APlayerReplicationInfo::GetPlayerName(const MemoryManager& pm) const {
        if (!IsValid()) return L"";
        // Use the GetFString helper defined in Objects.hpp
        return GetFString(pm, Address + Offset_PlayerName);
    }

    uintptr_t APlayerReplicationInfo::GetTeamInfoAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<uintptr_t>(Address + Offset_TeamInfo).value_or(0);
    }

    ATeamInfo APlayerReplicationInfo::GetTeamInfo(const MemoryManager& pm) const {
        return ATeamInfo(GetTeamInfoAddress(pm));
    }

    int32_t APlayerReplicationInfo::GetScore(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_Score).value_or(0);
    }

    int32_t APlayerReplicationInfo::GetDeaths(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_Deaths).value_or(0);
    }

    uint8_t APlayerReplicationInfo::GetPing(const MemoryManager& pm) const {
        if (!IsValid()) return 255; // Use a common max/default value
        return pm.Read<uint8_t>(Address + Offset_Ping).value_or(255);
    }

    int32_t APlayerReplicationInfo::GetPlayerID(const MemoryManager& pm) const {
        if (!IsValid()) return 0; // Or -1 if that's more appropriate
        return pm.Read<int32_t>(Address + Offset_PlayerID).value_or(0);
    }

    // Internal helper macro for reading PRI flags safely
#define GET_PRI_FLAG_IMPL(pm, addr, offset, bit) \
    (addr != 0 ? ((pm.Read<uint32_t>(addr + offset).value_or(0) >> bit) & 1) : false)

// Bitfield flag implementations
    bool APlayerReplicationInfo::IsAdmin(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 0); }
    bool APlayerReplicationInfo::IsSpectator(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 1); }
    bool APlayerReplicationInfo::IsOnlySpectator(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 2); }
    bool APlayerReplicationInfo::IsWaitingPlayer(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 3); }
    bool APlayerReplicationInfo::IsReadyToPlay(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 4); }
    bool APlayerReplicationInfo::IsOutOfLives(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 5); }
    bool APlayerReplicationInfo::IsBot(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 6); }
    bool APlayerReplicationInfo::IsInactive(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 7); }
    bool APlayerReplicationInfo::IsFromPreviousLevel(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 8); }
    bool APlayerReplicationInfo::IsTimedOut(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 9); }
    bool APlayerReplicationInfo::IsUnregistered(const MemoryManager& pm) const { return GET_PRI_FLAG_IMPL(pm, Address, Offset_Flags, 10); }

#undef GET_PRI_FLAG_IMPL // Undefine the helper macro

    // ADDED implementation for GetCameraSettings
    std::optional<FProfileCameraSettings> APRI::GetCameraSettings(const MemoryManager& pm) const {
        if (!IsValid()) return std::nullopt;
        
        // First get the Camera (ACameraSettingsActor_TA) address
        auto cameraAddressOpt = pm.Read<uintptr_t>(Address + Offset_CameraSettings);
        if (!cameraAddressOpt || *cameraAddressOpt == 0) return std::nullopt;
        
        // Now read the ProfileSettings from the Camera object at offset 0x0278
        return pm.Read<FProfileCameraSettings>(*cameraAddressOpt + Offset_CameraSettingsFProfileCameraSettings);
    }

    // --- APawn Method Definitions ---
    uintptr_t APawn::GetPlayerInfoAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Assuming Offset_PlayerReplicationInfo is correct for APawn
        return pm.Read<uintptr_t>(Address + Offset_PlayerReplicationInfo).value_or(0);
    }

    APlayerReplicationInfo APawn::GetPlayerInfo(const MemoryManager& pm) const {
        // Returns the base class wrapper. User might need to cast to APRI based on context.
        return APlayerReplicationInfo(GetPlayerInfoAddress(pm));
    }

    // Update GetName to use GetFString helper
    std::wstring ATeamInfo::GetName(const MemoryManager& pm) const {
        if (!IsValid()) return L"";
        return GetFString(pm, Address + Offset_TeamName);
    }

    // ... rest of ATeamInfo methods (GetSize might still be problematic)
    int32_t ATeamInfo::GetSize(const MemoryManager& pm) const {
        // ATeamInfo typically doesn't store size directly.
        // You'd usually get the ATeam instance and query its Members array count.
        // Returning the value at Offset_Size might be misleading or incorrect.
        if (!IsValid()) return 0;
        // Attempt to read, but be aware this might not be the intended way to get team size.
        // Using the existing arbitrary offset 0x278 as a placeholder.
        return pm.Read<int32_t>(Address + 0x0278 /* Placeholder offset, likely incorrect */).value_or(0);
    }

    int32_t ATeamInfo::GetScore(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_Score).value_or(0);
    }

    int32_t ATeamInfo::GetIndex(const MemoryManager& pm) const {
        if (!IsValid()) return -1; // 0 or 1 are valid indices
        return pm.Read<int32_t>(Address + Offset_TeamIndex).value_or(-1);
    }

    FColorData ATeamInfo::GetColor(const MemoryManager& pm) const {
        if (!IsValid()) return {}; // Return default FColorData
        return pm.Read<FColorData>(Address + Offset_TeamColor).value_or(FColorData{});
    }


    std::vector<APRI> ATeam::GetMembers(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Reads the TArray of APRI pointers representing team members
        return GetTArrayItems<APRI>(pm, Address + Offset_Members);
    }


    uintptr_t APlayerController::GetPRIActorAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Reads the pointer to this controller's Player Replication Info / Player State
        return pm.Read<uintptr_t>(Address + Offset_PlayerReplicationInfo).value_or(0);
    }

    APRI APlayerController::GetPRIActor(const MemoryManager& pm) const {
        // Returns as APRI, assuming the PRI is always an APRI in Rocket League context
        return APRI(GetPRIActorAddress(pm));
    }

    uintptr_t APlayerController::GetCarAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // AcknowledgedPawn usually points to the pawn the controller is currently controlling
        return pm.Read<uintptr_t>(Address + Offset_AcknowledgedPawn).value_or(0);
    }

    ACar APlayerController::GetCar(const MemoryManager& pm) const {
        // Assumes the acknowledged pawn is an ACar
        return ACar(GetCarAddress(pm));
    }

    // IMPLEMENT PlayerCamera Accessors
    uintptr_t APlayerController::GetPlayerCameraAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<uintptr_t>(Address + Offset_PlayerCamera).value_or(0);
    }

    ACamera APlayerController::GetPlayerCamera(const MemoryManager& pm) const {
        return ACamera(GetPlayerCameraAddress(pm));
    }

    FPickupData AVehiclePickup::GetPickupData(const MemoryManager& pm) const {
        if (!IsValid()) return {}; // Return default FPickupData
        // Assuming Offset_PickupData is correct relative to AVehiclePickup address
        return pm.Read<FPickupData>(Address + Offset_PickupData).value_or(FPickupData{});
    }

    ACar AVehiclePickup::GetInstigatorCar(const MemoryManager& pm) const {
        if (!IsValid()) return ACar(0);
        FPickupData data = GetPickupData(pm);
        // Construct ACar wrapper. Address might be 0 if not picked up or invalid.
        return ACar(data.InstigatorCarAddress);
    }

    bool AVehiclePickup::IsPickedUp(const MemoryManager& pm) const {
        // Read the data structure containing the flag
        if (!IsValid()) return true; // Assume picked up if actor is invalid? Or false?
        FPickupData data = GetPickupData(pm);
        return data.IsPickedUp();
    }

    float AVehiclePickup::GetRespawnDelay(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        // Assuming Offset_RespawnDelay is correct
        return pm.Read<float>(Address + Offset_RespawnDelay).value_or(0.0f);
    }

    // These inherit from AVehiclePickup
    float AVehiclePickup_Boost::GetBoostAmount(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        // Assuming Offset_BoostAmount is correct relative to AVehiclePickup_Boost address
        return pm.Read<float>(Address + Offset_BoostAmount).value_or(0.0f);
    }

    uint8_t AVehiclePickup_Boost::GetBoostType(const MemoryManager& pm) const {
        if (!IsValid()) return 0; // Assuming 0 is an invalid/default type
        // Assuming Offset_BoostType is correct (may not exist)
        return pm.Read<uint8_t>(Address + Offset_BoostType).value_or(0);
    }

    bool AVehiclePickup_Boost::IsBigPad(const MemoryManager& pm) const {
        // Logic might depend on BoostType enum or amount. Using amount for now.
        if (!IsValid()) return false;
        float amount = GetBoostAmount(pm);
        // Check if amount is closer to 100 (big pad) than small pad amount (e.g., 12)
        return amount > 50.0f; // Example threshold, adjust as needed
    }

    // Fix implementations assuming AGoal definition is correct in Objects.hpp
    uint8_t AGoal::GetTeamNum(const MemoryManager& pm) const {
        if (!IsValid()) return 255; // Use an invalid team number like 255
        // Assuming Offset_TeamNum is correct
        return pm.Read<uint8_t>(Address + Offset_TeamNum).value_or(255);
    }

    FVectorData AGoal::GetLocation(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Location is correct
        return pm.Read<FVectorData>(Address + Offset_Location).value_or(FVectorData{});
    }

    FVectorData AGoal::GetDirection(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Direction is correct
        return pm.Read<FVectorData>(Address + Offset_Direction).value_or(FVectorData{});
    }

    FVectorData AGoal::GetRight(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Right is correct
        return pm.Read<FVectorData>(Address + Offset_Right).value_or(FVectorData{});
    }

    FVectorData AGoal::GetUp(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Up is correct
        return pm.Read<FVectorData>(Address + Offset_Up).value_or(FVectorData{});
    }

    FRotatorData AGoal::GetRotation(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_Rotation is correct
        return pm.Read<FRotatorData>(Address + Offset_Rotation).value_or(FRotatorData{});
    }

    FBoxData AGoal::GetWorldBox(const MemoryManager& pm) const {
        if (!IsValid()) return {};
        // Assuming Offset_WorldBox is correct
        return pm.Read<FBoxData>(Address + Offset_WorldBox).value_or(FBoxData{});
    }

    float AGoal::GetWidth(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        FBoxData box = GetWorldBox(pm);
        if (!box.IsValid) return 0.0f;
        // Assuming Width corresponds to the difference along the Y-axis
        return std::abs(box.Max.Y - box.Min.Y);
    }

    float AGoal::GetHeight(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        FBoxData box = GetWorldBox(pm);
        if (!box.IsValid) return 0.0f;
        // Assuming Height corresponds to the difference along the Z-axis
        return std::abs(box.Max.Z - box.Min.Z);
    }

    float AGoal::GetDepth(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        FBoxData box = GetWorldBox(pm);
        if (!box.IsValid) return 0.0f;
        // Assuming Depth corresponds to the difference along the X-axis
        return std::abs(box.Max.X - box.Min.X);
    }

    // Fix implementations assuming definition is correct in Objects.hpp
    uintptr_t UGameViewportClient::GetGameEventAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        // Assuming Offset_GameEvent is correct
        return pm.Read<uintptr_t>(Address + Offset_GameEvent).value_or(0);
    }

    AGameEvent UGameViewportClient::GetGameEvent(const MemoryManager& pm) const {
        return AGameEvent(GetGameEventAddress(pm));
    }

    // --- BoostPadState Method Definitions --- (Should be fine, not directly using UObject offsets)
    // ... (Keep BoostPadState methods as they were)

    // --- FieldState Method Definitions --- (Should be fine, relies on BoostPadState)
    // ... (Keep FieldState methods as they were, including UpdateAllPads)
    // Note: UpdateAllPads relies on AVehiclePickup_Boost::GetLocation which comes from AActor -> UObject
    // Ensure AVehiclePickup_Boost definition in Objects.hpp is correct.

    APRI AVehicle::GetPRI(const MemoryManager& pm) const {
        // Read ReplicatedPRI directly from AVehicle offset
        if (!IsValid()) return APRI(0);
        auto priAddrOpt = pm.Read<uintptr_t>(Address + Offset_PRI); 
        return APRI(priAddrOpt.value_or(0));
    }

    // NEW: Add the missing implementation for GetBoostComponent
    uintptr_t AVehicle::GetBoostComponentAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_BoostComponent);
        return addrOpt.value_or(0);
    }

    // NEW: Add the missing implementation for GetBoostComponent
    UBoostComponent AVehicle::GetBoostComponent(const MemoryManager& pm) const {
        return UBoostComponent(GetBoostComponentAddress(pm));
    }

    // NEW: Add the missing implementation for GetAmount
    float UBoostComponent::GetAmount(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        auto amountOpt = pm.Read<float>(Address + Offset_CurrentAmount);
        // Multiply by 100 to convert from 0.0-1.0 scale to 0-100 percentage
        return amountOpt.value_or(0.0f) * 100.0f;
    }

    uintptr_t APRI::GetCarAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        auto addrOpt = pm.Read<uintptr_t>(Address + Offset_Car);
        return addrOpt.value_or(0);
    }

    ACar APRI::GetCar(const MemoryManager& pm) const {
        return ACar(GetCarAddress(pm));
    }

    int32_t APRI::GetBallTouches(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_BallTouches).value_or(0);
    }

    int32_t APRI::GetCarTouches(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_CarTouches).value_or(0);
    }

    int32_t APRI::GetBoostPickups(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<int32_t>(Address + Offset_BoostPickups).value_or(0);
    }

    uintptr_t APRI::GetGameEventAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<uintptr_t>(Address + Offset_GameEvent).value_or(0);
    }

    AGameEvent APRI::GetGameEvent(const MemoryManager& pm) const {
        return AGameEvent(GetGameEventAddress(pm));
    }

    uintptr_t APRI::GetReplicatedGameEventAddress(const MemoryManager& pm) const {
        if (!IsValid()) return 0;
        return pm.Read<uintptr_t>(Address + Offset_ReplicatedGameEvent).value_or(0);
    }

    AGameEvent APRI::GetReplicatedGameEvent(const MemoryManager& pm) const {
        return AGameEvent(GetReplicatedGameEventAddress(pm));
    }

    float UBoostComponent::GetMaxAmount(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_MaxAmount).value_or(0.0f);
    }

    float UBoostComponent::GetConsumptionRate(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_ConsumptionRate).value_or(0.0f);
    }

    float UBoostComponent::GetStartAmount(const MemoryManager& pm) const {
        if (!IsValid()) return 0.0f;
        return pm.Read<float>(Address + Offset_StartAmount).value_or(0.0f);
    }

    // --- BoostPadState Method Definitions ---
    void SDK::BoostPadState::Reset() {
        IsActive = true;
        PickupActorAddress = 0;
        PickedUpTime = std::chrono::steady_clock::time_point(); // Default/empty time point
    }

    float SDK::BoostPadState::RespawnTimeSeconds() const {
        return IsBigPad ? 10.0f : 4.0f; // Big pads take 10 seconds, small pads take 4 seconds
    }

    std::optional<float> SDK::BoostPadState::GetElapsedSeconds() const {
        // If we don't have a pickup time, return nullopt
        if (PickedUpTime == std::chrono::steady_clock::time_point()) {
            return std::nullopt;
        }
        
        // Calculate elapsed time since pickup
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - PickedUpTime).count() / 1000.0f;
        return elapsed;
    }

    std::optional<float> SDK::BoostPadState::GetRemainingSeconds() const {
        auto elapsed = GetElapsedSeconds();
        if (!elapsed) {
            return std::nullopt;
        }
        
        // Calculate remaining time
        float respawnTime = RespawnTimeSeconds();
        float remaining = respawnTime - *elapsed;
        return std::max(0.0f, remaining); // Don't return negative time
    }

    void SDK::BoostPadState::MarkInactive() {
        IsActive = false;
        PickedUpTime = std::chrono::steady_clock::now();
    }

    void SDK::BoostPadState::UpdateState(MemoryManager& pm) {
        // If we don't have a pickup actor address, we can't update
        if (PickupActorAddress == 0) {
            return;
        }
        
        // Read the pickup data to see if it's picked up
        AVehiclePickup pickup(PickupActorAddress);
        bool isPickedUp = pickup.IsPickedUp(pm);
        
        // If the state changed from active to inactive, mark the time
        if (IsActive && isPickedUp) {
            MarkInactive();
        }
        // If the state changed from inactive to active, reset
        else if (!IsActive && !isPickedUp) {
            Reset();
        }
        
        // If it's been inactive for longer than the respawn time, it should be active again
        auto remaining = GetRemainingSeconds();
        if (!IsActive && remaining && *remaining <= 0.0f) {
            Reset();
        }
    }

    // --- FieldState Method Definitions ---
    void SDK::FieldState::ResetBoostPads() {
        for (auto& pad : BoostPads) {
            pad.Reset();
        }
    }

    SDK::BoostPadState* SDK::FieldState::FindPadByLocation(const Vector3D& location, float tolerance) {
        for (auto& pad : BoostPads) {
            if (pad.Location.DistanceTo(location) <= tolerance) {
                return &pad;
            }
        }
        return nullptr;
    }

    SDK::BoostPadState* SDK::FieldState::FindPadByActorAddress(uintptr_t actorAddress) {
        for (auto& pad : BoostPads) {
            if (pad.PickupActorAddress == actorAddress) {
                return &pad;
            }
        }
        return nullptr;
    }

    void SDK::FieldState::UpdateAllPads(MemoryManager& pm, const std::vector<AVehiclePickup_Boost>& all_boost_pickups) {
        // First, update all pads that already have a pickup actor address
        for (auto& pad : BoostPads) {
            if (pad.PickupActorAddress != 0) {
                pad.UpdateState(pm);
            }
        }
        
        // Then, try to associate any unassociated pickups with pads
        for (const auto& pickup : all_boost_pickups) {
            // Get the location of the pickup
            FVectorData loc = pickup.GetLocation(pm);
            Vector3D pickupLoc(loc);
            
            // Find the pad closest to this pickup
            BoostPadState* pad = FindPadByLocation(pickupLoc);
            if (pad) {
                // If the pad doesn't have a pickup actor address yet, assign it
                if (pad->PickupActorAddress == 0) {
                    pad->PickupActorAddress = pickup.Address;
                    pad->UpdateState(pm);
                }
            }
        }
    }
}