#pragma once // Use include guards

#include <cstdint>       // For uintptr_t, int32_t, uint32_t, etc.
#include <vector>        // For TArray::GetItems and std::vector return types
#include <string>        // For FString, GetName etc.
#include <cmath>         // For FRotator conversion and distance calculations
#include <chrono>        // For BoostPad timing
#include <optional>      // To represent potential null returns safely
#include <algorithm>     // For std::max in BoostPadState
#include <type_traits>   // For SFINAE or concepts if needed later

#include "MemoryManager.h"

class GNameTable;

namespace SDK
{
    class UObject;
    class UField;
    class UStruct;
    class UClass;
    class UFunction;
    struct FName;
    struct FNameEntry;

    class UGameViewportClient;
    class UBoostComponent;
    class UVehicleSim;
    class UWheel;
    class BoostPadState;
    class ACamera;

    struct alignas(4) FVectorData
    {
        float X = 0.0f; // 0x00
        float Y = 0.0f; // 0x04
        float Z = 0.0f; // 0x08
    }; // Size: 0x0C

    struct alignas(4) FRotatorData
    {
        int32_t Pitch = 0; // 0x00
        int32_t Yaw = 0;   // 0x04
        int32_t Roll = 0;  // 0x08

        static constexpr float INT_TO_RAD_FACTOR = 0.00009587379924285f;

        float GetPitchRad() const { return static_cast<float>(Pitch) * INT_TO_RAD_FACTOR; }
        float GetYawRad() const { return static_cast<float>(Yaw) * INT_TO_RAD_FACTOR; }
        float GetRollRad() const { return static_cast<float>(Roll) * INT_TO_RAD_FACTOR; }
    }; // Size: 0x0C

    struct alignas(1) FColorData
    {
        uint8_t B = 0; // 0x00
        uint8_t G = 0; // 0x01
        uint8_t R = 0; // 0x02
        uint8_t A = 0; // 0x03
    }; // Size: 0x04

    struct alignas(8) FBoxData // Note: alignas(8) because FVectorData contains floats
    {
        FVectorData Min;    // 0x00
        FVectorData Max;    // 0x0C
        uint8_t IsValid = 0;// 0x18 (Usually 1 if valid)
        // Padding likely exists here to align to 8 bytes (0x19 -> 0x20?)
        // Check in debugger if precise size matters
    }; // Size: Likely 0x1C or 0x20 due to alignment

    // ADD Profile Camera Settings struct
    struct alignas(4) FProfileCameraSettings
    {
        float FOV = 90.0f;        // 0x00 Defaulting to 90
        float Height = 100.0f;    // 0x04 Defaults based on common settings
        float Pitch = -3.0f;      // 0x08 Angle in degrees?
        float Distance = 270.0f;  // 0x0C
        float Stiffness = 0.5f;   // 0x10
        float SwivelSpeed = 2.5f; // 0x14
        float TransitionSpeed = 1.0f; // 0x18
    }; // Size: 0x1C

    // Represents an entry in the GNames table (Based on GameDefines.hpp)
    // Assuming ANSI names based on GameDefines.hpp and GNameTable.cpp logic
    // *** CORRECTING based on GNameTable.h comments ***
    struct alignas(8) FNameEntry // Use alignas(8) because Flags is uint64_t
    {
        // Offsets need verification based on game version / GNameTable logic
        // Using offsets from GNameTable.h internal struct comments
        static constexpr uintptr_t Offset_IndexInEntry = 0x08;
        static constexpr uintptr_t Offset_StringData = 0x18; // Corrected offset
        static constexpr size_t MaxNameLength = 1024; // Assuming max length is okay
        static constexpr bool IsWideChar = true;      // Corrected type

        // Member layout adjusted to match offsets - PRECISE layout needs verification
        uint64_t Flags = 0;             // 0x00 (Size: 8)
        int32_t Index = -1;             // 0x08 (Size: 4)
        uint8_t Padding_0x0C[0x18 - 0x0C]; // Padding from end of Index (0x0C) to StringData (0x18)
        wchar_t Name[MaxNameLength] = { 0 }; // 0x18 - String data (Wide Char)

        // Helper to get the name as a string
        std::string GetNameString() const {
            // Find the null terminator for wide string
            const wchar_t* end = std::find(Name, Name + MaxNameLength, L'\0');
            std::wstring wide_str(Name, end - Name);
            // Convert wide string to narrow string (lossy)
            std::string narrow_str;
            narrow_str.reserve(wide_str.length());
            for (wchar_t wc : wide_str) {
                if (wc > 0 && wc < 128) narrow_str += static_cast<char>(wc);
                else narrow_str += '?'; // Replace non-ASCII characters
            }
            return narrow_str;
        }
    };


    // Layout for FString (Based on GameDefines.hpp) - Used by GetFString helper
    struct alignas(8) FStringLayout
    {
        uintptr_t ArrayData = 0; // 0x00 - Pointer to character array (wchar_t assumed based on previous impl)
        int32_t ArrayCount = 0;  // 0x08 - Number of characters (potentially including null)
        int32_t ArrayMax = 0;    // 0x0C - Allocated capacity
    }; // Size: 0x10


    // Helper function to read FString content (defined inline for header use)
    // Updated to use FStringLayout and assumes wchar_t based on previous logic
    inline std::wstring GetFString(const MemoryManager& pm, uintptr_t fstring_address)
    {
        if (!pm.IsAttached() || fstring_address == 0) return L"";

        auto strLayoutOpt = pm.Read<FStringLayout>(fstring_address);
        if (!strLayoutOpt) return L"[ReadError:FStringLayout]";

        FStringLayout strLayout = *strLayoutOpt;
        if (strLayout.ArrayData == 0 || strLayout.ArrayCount <= 0) return L""; // Count should be >0 for null term

        // Prevent reading excessive amounts of memory
        const int32_t maxReasonableCount = 4096;
        if (strLayout.ArrayCount > maxReasonableCount || strLayout.ArrayCount < 0) return L"[ReadError:InvalidCount]";

        // Allocate buffer (+1 for safety, though Count should include null)
        std::vector<wchar_t> buffer(static_cast<size_t>(strLayout.ArrayCount) + 1, L'\0');
        // Read ArrayCount characters (assuming Count includes null terminator or is length+1)
        // Read (ArrayCount - 1) if Count is just length? Let's assume Count includes null for now.
        if (!pm.ReadBytes(strLayout.ArrayData, buffer.data(), (strLayout.ArrayCount) * sizeof(wchar_t)))
        {
            return L"[ReadError:StringBytes]";
        }
        // Ensure null termination
        buffer[strLayout.ArrayCount] = L'\0';

        // Construct string up to the first null terminator found
        return std::wstring(buffer.data());
    }


    // Layout for TArray (Based on GameDefines.hpp) - Used by GetTArrayItems helper
    struct alignas(8) TArrayLayout
    {
        uintptr_t ArrayData = 0; // 0x00 - Pointer to the array elements (usually pointers themselves)
        int32_t ArrayCount = 0;  // 0x08 - Number of elements currently in the array
        int32_t ArrayMax = 0;    // 0x0C - Allocated capacity of the array
    }; // Size: 0x10

    // Template helper function to get items from a TArray of POINTERS (defined inline)
    // Updated to use TArrayLayout
    template <typename T> // T is the C++ WRAPPER type (e.g., UObject, ACar)
    inline std::vector<T> GetTArrayItems(const MemoryManager& pm, uintptr_t tarray_address)
    {
        std::vector<T> items;
        if (!pm.IsAttached() || tarray_address == 0) return items;

        auto layoutOpt = pm.Read<TArrayLayout>(tarray_address);
        if (!layoutOpt) return items; // Failed to read layout

        TArrayLayout layout = *layoutOpt;
        // Basic sanity checks
        const int32_t maxReasonableCount = 4096; // Increased sanity check limit
        if (layout.ArrayData == 0 || layout.ArrayCount <= 0 || layout.ArrayCount > maxReasonableCount) {
            return items;
        }

        items.reserve(static_cast<size_t>(layout.ArrayCount));
        constexpr size_t pointerSize = sizeof(uintptr_t); // 8 bytes for 64-bit

        // Optimized: Read all pointers at once if count is reasonable
        if (layout.ArrayCount <= 128) { // Threshold for bulk read
            std::vector<uintptr_t> addresses(layout.ArrayCount);
            if (pm.ReadBytes(layout.ArrayData, addresses.data(), layout.ArrayCount * pointerSize)) {
                for (uintptr_t itemAddress : addresses) {
                    if (itemAddress != 0) {
                        // Construct the wrapper object around the address
                        items.emplace_back(itemAddress); // Assumes T has a constructor taking uintptr_t
                    }
                }
                return items;
            }
        }

        // Fallback: Read addresses individually
        for (int32_t i = 0; i < layout.ArrayCount; ++i)
        {
            auto itemAddressOpt = pm.Read<uintptr_t>(layout.ArrayData + i * pointerSize);
            if (itemAddressOpt && *itemAddressOpt != 0)
            {
                items.emplace_back(*itemAddressOpt); // Construct the wrapper
            }
        }
        return items;
    }

    // Template helper function to get a single item from a TArray of POINTERS (defined inline)
    // Updated to use TArrayLayout
    template <typename T> // T is the C++ WRAPPER type
    inline std::optional<T> GetTArrayItem(const MemoryManager& pm, uintptr_t tarray_address, int32_t index)
    {
        if (!pm.IsAttached() || tarray_address == 0) return std::nullopt;

        auto layoutOpt = pm.Read<TArrayLayout>(tarray_address);
        if (!layoutOpt) return std::nullopt;

        TArrayLayout layout = *layoutOpt;
        if (layout.ArrayData == 0 || index < 0 || index >= layout.ArrayCount) return std::nullopt;

        constexpr size_t pointerSize = sizeof(uintptr_t);
        auto itemAddressOpt = pm.Read<uintptr_t>(layout.ArrayData + index * pointerSize);

        if (!itemAddressOpt || *itemAddressOpt == 0) return std::nullopt; // Item pointer is null or read failed

        return T(*itemAddressOpt); // Construct and return
    }


    // (Keep these as they are game specific and inherit from the core types)
    class AActor;
    class APawn;
    class AVehicle;
    class ACar;
    class ATeam;
    class APRI;
    class AGameEvent;
    class APlayerController;
    class AController;
    class ATeamInfo;
    class ABall;
    class AGoal;
    class AVehiclePickup;
    class AVehiclePickup_Boost;
    class UGameViewportClient;
    class UBoostComponent;
    class UVehicleSim;
    class UWheel;
    class BoostPadState;
    struct FProfileCameraSettings;

    class UObject
    {
    public:
        uintptr_t VfTable;                             // 0x0000 (Pointer Size)
        uintptr_t HashNext;                            // 0x0008 (Pointer Size) - NEW
        uint64_t ObjectFlags;                          // 0x0010 (Size: 0x08)
        uintptr_t HashOuterNext;                       // 0x0018 (Pointer Size) - NEW
        uintptr_t StateFrame;                          // 0x0020 (Pointer Size) - NEW
        uintptr_t Linker;                              // 0x0028 (Pointer Size) - NEW
        int32_t LinkerIndex;                           // 0x0030 (Size: 0x04) - NEW
        int32_t ObjectInternalInteger;                 // 0x0034 (Size: 0x04) - SHIFTED from 0x38
        int32_t NetIndex;                              // 0x0038 (Size: 0x04) - SHIFTED from 0x3C
        // uint8_t Unknown_Padding_To_Outer[0x4];      // Padding from 0x3C to 0x40
        class UObject* Outer;                          // 0x0040 (Size: 0x08)
        // FName Name;                                 // 0x0048 (Size: 0x08) - REMOVED FName struct
        int32_t FNameEntryId;                          // 0x0048 (Size: 0x04) - ADDED direct ID
        int32_t InstanceNumber;                        // 0x004C (Size: 0x04) - ADDED direct Instance Number
        class UClass* Class;                           // 0x0050 (Size: 0x08)
        class UObject* ObjectArchetype;                // 0x0058 (Size: 0x08)

        // Public Address member for wrapper usage
        uintptr_t Address;

        static constexpr uintptr_t Offset_ObjectFlags = 0x0010;
        static constexpr uintptr_t Offset_InternalInteger = 0x0034; // SHIFTED
        static constexpr uintptr_t Offset_NetIndex = 0x0038;      // SHIFTED
        static constexpr uintptr_t Offset_Outer = 0x0040;
        static constexpr uintptr_t Offset_Name = 0x0048; // Offset to the FNameEntryId
        static constexpr uintptr_t Offset_InstanceNumber = 0x004C; // Offset to InstanceNumber
        static constexpr uintptr_t Offset_Class = 0x0050;
        static constexpr uintptr_t Offset_ObjectArchetype = 0x0058;


        explicit UObject(uintptr_t address = 0) : Address(address) {}
        bool IsValid() const { return Address != 0; }

        // GetName now needs to read the FName struct and use GNameTable
        std::string GetName(const MemoryManager& pm, const GNameTable& gnames) const;
        // GetIndex likely reads ObjectInternalInteger now
        int32_t GetIndex(const MemoryManager& pm) const;
        uintptr_t GetOuterAddress(const MemoryManager& pm) const;
        UObject GetOuter(const MemoryManager& pm) const;
        uintptr_t GetClassAddress(const MemoryManager& pm) const;
        UClass GetClass(const MemoryManager& pm) const; // Requires UClass definition
        // GetFullName needs update to handle FName struct and new hierarchy traversal
        std::string GetFullName(const MemoryManager& pm, const GNameTable& gnames) const;
        // IsA needs update for new hierarchy traversal (SuperField)
        bool IsA(const MemoryManager& pm, const GNameTable& gnames, const std::string& simple_class_name) const;
        // Helper to get the FName structure itself
        // std::optional<FName> GetFName(const MemoryManager& pm) const; // REMOVED Declaration
    };

    class UField : public UObject
    {
    public:
        // Inherits UObject members up to 0x58 (ObjectArchetype)
        class UField* Next;                          // 0x0060 (From old offset, GameDefines uses 0x34 - VERIFY THIS)
        // Let's try the GameDefines offset first
        // uint8_t Unknown_UObject_End[ CalculatePadding(End of UObject, Offset_Next) ];
        // class UField* Next;                          // 0x0034 (Based on GameDefines UField) - This seems wrong, likely relative to UField start *after* UObject base
        // Sticking to the layout provided: UObject members up to 0x60
        // Recalculating based on UObject ending around 0x60
        // uint8_t UnknownData00[0x8];                  // 0x0060 (Padding?)
        // class UField* Next;                          // 0x0068 (If UObject is 0x60 and Next is at +0x08)

        static constexpr uintptr_t Offset_Next = 0x0060; // Offset relative to UObject Address **(NEEDS VERIFICATION)**

        explicit UField(uintptr_t address = 0) : UObject(address) {}

        uintptr_t GetNextAddress(const MemoryManager& pm) const;
        UField GetNext(const MemoryManager& pm) const;
    };

    class UStruct : public UField
    {
    public:
        // Inherits UObject and UField members... UField ends around 0x68 (Next + size)
        uint8_t Unknown_0068[0x10];                    // Padding based on GameDefines UStruct layout?
        class UField* SuperField;                      // 0x0078? (GameDefines: 0x54 - this offset seems relative to base UObject?) - Let's try GameDefines direct offset
        // Resetting based on GameDefines.hpp direct offsets relative to base address:
        // UObject...
        // UField::Next at 0x60 (assumed)
        // UStruct members start after UField
        // SuperField at 0x0078 (Old Offset_SuperField=0x80) (GameDefines: 0x54 - Relative to base UObject?) -> Using 0x78, needs check
        // Children at 0x0080 (Old Offset_Children=0x90) (GameDefines: 0x5C - Relative to base UObject?) -> Using 0x80, needs check
        // PropertySize at 0x0088 (GameDefines: 0x64 - Relative to base UObject?) -> Using 0x88, needs check

        // Re-applying GameDefines direct offsets from the start address:
        // ... UObject members up to 0x50 (Class)
        // ... ObjectArchetype at 0x58
        // ... UField::Next assumed at 0x60
        // Now UStruct starts adding members:
        // SuperField at 0x78 (GameDefines 0x54 is too early if inheriting) -> Let's use 0x78 for now
        // Children at 0x80 (GameDefines 0x5C is too early) -> Let's use 0x80 for now
        // PropertySize at 0x88 (GameDefines 0x64 is too early) -> Let's use 0x88 for now

        // Sticking to previous assumed offsets based on inheritance for now:
        // uint8_t Unknown_UField_End[0x10];              // Padding from UField::Next to UStruct::SuperField // REMOVED - Covered by Unknown_0068 ?
        // class UField* SuperField;                      // 0x0080 (Old Offset) // REMOVED - Declared above around line 331
        class UField* Children;                        // 0x0088 (Old Offset + ptr size)
        uint32_t PropertySize;                         // 0x0090 (Old Offset_Children + ptr size)
        // uint8_t Unknown_Struct_End[...];


        static constexpr uintptr_t Offset_SuperField = 0x0080; // Offset relative to UObject Address **(NEEDS VERIFICATION - Reverted back from 0x54)**
        static constexpr uintptr_t Offset_Children = 0x0088; // Offset relative to UObject Address **(NEEDS VERIFICATION - Reverted back from 0x5C)**
        static constexpr uintptr_t Offset_PropertySize = 0x0090; // Offset relative to UObject Address **(NEEDS VERIFICATION - Reverted back from 0x64)**


        explicit UStruct(uintptr_t address = 0) : UField(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        uintptr_t GetSuperFieldAddress(const MemoryManager& pm) const;
        UField GetSuperField(const MemoryManager& pm) const;
        uintptr_t GetChildrenAddress(const MemoryManager& pm) const;
        UField GetChildren(const MemoryManager& pm) const;
    };

    class UFunction : public UStruct
    {
    public:
        uintptr_t Func; // Member exists, accessed via Offset_Func

        static constexpr uintptr_t Offset_Func = 0x0080; // ** USING 0x80 **

        explicit UFunction(uintptr_t address = 0) : UStruct(address) {}

        uintptr_t GetFuncAddress(const MemoryManager& pm) const {
            return pm.Read<uintptr_t>(Address + Offset_Func).value_or(0);
        }
        uint64_t GetFunctionFlags(const MemoryManager& pm) const {
            // Keep old offset for now, Func is priority
            return pm.Read<uint64_t>(Address + 0x00C0 /* Old Offset_FunctionFlags */).value_or(0);
        }

        // GetFullName is inherited from UObject
    };

    // NOTE: GameDefines has UClass : UState : UStruct. Simplifying to UClass : UStruct
    class UClass : public UStruct
    {
    public:
        // Inherits offsets from UStruct (SuperField, Children)

        explicit UClass(uintptr_t address = 0) : UStruct(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        // Inherits GetSuperFieldAddress, GetSuperField, GetChildrenAddress, GetChildren from UStruct

        // GetSuperClass wraps GetSuperFieldAddress
        UClass GetSuperClass(const MemoryManager& pm) const;

    };

    class AGameEvent : public UObject // Typically inherits AInfo -> AActor -> UObject
    {
    public:
        // --- Offsets --- (Updated as per Python)
        static constexpr uintptr_t Offset_Goals = 0x00D8; // TArray<AGoal*>
        static constexpr uintptr_t Offset_CountDownTime = 0x02A0; // int32_t
        static constexpr uintptr_t Offset_Players = 0x0330; // TArray<AController*>
        static constexpr uintptr_t Offset_PRIs = 0x0340; // TArray<APRI*>
        static constexpr uintptr_t Offset_Cars = 0x0350; // TArray<ACar*>
        static constexpr uintptr_t Offset_LocalPlayers = 0x0360; // TArray<APlayerController*>
        static constexpr uintptr_t Offset_GameOwner = 0x0430; // APRI*
        static constexpr uintptr_t Offset_Flags = 0x07F0; // uint32_t (bitfield)
        static constexpr uintptr_t Offset_GameTime = 0x0814; // int32_t (Seconds?)
        static constexpr uintptr_t Offset_WarmupTime = 0x0818; // int32_t (Seconds?)
        static constexpr uintptr_t Offset_MaxScore = 0x081C; // int32_t
        static constexpr uintptr_t Offset_TimeRemaining = 0x084C; // float (Seconds?)
        static constexpr uintptr_t Offset_SecondsRemaining = 0x0850; // int32_t
        static constexpr uintptr_t Offset_TotalGameTimePlayed = 0x0858; // float
        static constexpr uintptr_t Offset_OvertimePlayed = 0x085C; // float
        static constexpr uintptr_t Offset_Balls = 0x08C8; // TArray<ABall*>
        static constexpr uintptr_t Offset_Teams = 0x0910; // TArray<ATeam*>
        static constexpr uintptr_t Offset_MatchWinner = 0x0918; // ATeam*
        static constexpr uintptr_t Offset_MVP = 0x0928; // APRI*
        static constexpr uintptr_t Offset_FastestGoalPlayer = 0x0930; // APRI*
        static constexpr uintptr_t Offset_SlowestGoalPlayer = 0x0938; // APRI*
        static constexpr uintptr_t Offset_FurthestGoalPlayer = 0x0940; // APRI*
        static constexpr uintptr_t Offset_FastestGoalSpeed = 0x0948; // float
        static constexpr uintptr_t Offset_SlowestGoalSpeed = 0x094C; // float
        static constexpr uintptr_t Offset_FurthestGoal = 0x0950; // float
        static constexpr uintptr_t Offset_ScoringPlayer = 0x0958; // APRI*
        static constexpr uintptr_t Offset_RoundNum = 0x0960; // int32_t

        explicit AGameEvent(uintptr_t address = 0) : UObject(address) {}

        // --- Accessor Methods (Definitions in Objects.cpp) ---
        std::vector<ABall> GetBalls(const MemoryManager& pm) const;
        std::vector<ACar> GetCars(const MemoryManager& pm) const;
        std::vector<APRI> GetPRIs(const MemoryManager& pm) const;
        std::vector<ATeam> GetTeams(const MemoryManager& pm) const;
        std::vector<AController> GetPlayers(const MemoryManager& pm) const;
        int32_t GetGameTime(const MemoryManager& pm) const;
        int32_t GetWarmupTime(const MemoryManager& pm) const;
        int32_t GetMaxScore(const MemoryManager& pm) const;
        float GetTimeRemaining(const MemoryManager& pm) const;
        int32_t GetSecondsRemaining(const MemoryManager& pm) const;
        float GetTotalGameTimePlayed(const MemoryManager& pm) const;
        float GetOvertimePlayed(const MemoryManager& pm) const;

        // Bitfield flags
        bool IsRoundActive(const MemoryManager& pm) const;
        bool IsPlayReplays(const MemoryManager& pm) const;
        bool IsBallHasBeenHit(const MemoryManager& pm) const;
        bool IsOvertime(const MemoryManager& pm) const;
        bool IsUnlimitedTime(const MemoryManager& pm) const;
        bool IsNoContest(const MemoryManager& pm) const;
        bool IsDisableGoalDelay(const MemoryManager& pm) const;
        bool IsShowNoScorerGoalMessage(const MemoryManager& pm) const;
        bool IsMatchEnded(const MemoryManager& pm) const;
        bool IsShowIntroScene(const MemoryManager& pm) const;
        bool IsClubMatch(const MemoryManager& pm) const;
        bool IsCanDropOnlineRewards(const MemoryManager& pm) const;
        bool IsAllowHonorDuels(const MemoryManager& pm) const;

        ATeam GetMatchWinner(const MemoryManager& pm) const;
        ATeam GetGameWinner(const MemoryManager& pm) const; // Verify offset if different from MatchWinner
        APRI GetMVP(const MemoryManager& pm) const;
        APRI GetFastestGoalPlayer(const MemoryManager& pm) const;
        APRI GetSlowestGoalPlayer(const MemoryManager& pm) const;
        APRI GetFurthestGoalPlayer(const MemoryManager& pm) const;
        float GetFastestGoalSpeed(const MemoryManager& pm) const;
        float GetSlowestGoalSpeed(const MemoryManager& pm) const;
        float GetFurthestGoal(const MemoryManager& pm) const;
        APRI GetScoringPlayer(const MemoryManager& pm) const;
        int32_t GetRoundNum(const MemoryManager& pm) const;
        APRI GetGameOwner(const MemoryManager& pm) const;
        int32_t GetCountDownTime(const MemoryManager& pm) const;

        std::vector<AGoal> GetGoals(const MemoryManager& pm) const;
        std::vector<APlayerController> GetLocalPlayers(const MemoryManager& pm) const;
    };


    // --- Actors ---
    class AActor : public UObject
    {
    public:
        static constexpr uintptr_t Offset_Location = 0x0090; // FVector (RootComponent?)
        static constexpr uintptr_t Offset_Rotation = 0x009C; // FRotator (RootComponent?)
        // Offsets below usually within ReplicatedMovement struct/component:
        static constexpr uintptr_t Offset_Velocity = 0x01A8; // FVector (Example offset for Velocity in ReplicatedMovement)
        static constexpr uintptr_t Offset_AngularVelocity = 0x01C0; // FVector (Example offset for AngularVelocity)
        // Size: 0x0268 (minimum)

        explicit AActor(uintptr_t address = 0) : UObject(address) {}

        // Note: Location/Rotation might be indirect through RootComponent
        FVectorData GetLocation(const MemoryManager& pm) const;
        FRotatorData GetRotation(const MemoryManager& pm) const;
        FVectorData GetVelocity(const MemoryManager& pm) const;
        FVectorData GetAngularVelocity(const MemoryManager& pm) const;
    };

    // --- Ball ---
    class ABall : public AActor
    {
    public:
        // ... Add specific Ball offsets if needed (e.g., LastTouchedBy)
        // Size: 0x0A48

        explicit ABall(uintptr_t address = 0) : AActor(address) {}
        // Add Ball specific methods if required
    };

    // --- Player Replication Info ---
    class APlayerReplicationInfo : public UObject // Typically inherits AInfo -> AActor
    {
    public:
        static constexpr uintptr_t Offset_Score = 0x0278;      // int32_t
        static constexpr uintptr_t Offset_Deaths = 0x027C;     // int32_t
        static constexpr uintptr_t Offset_Ping = 0x0280;       // uint8_t
        static constexpr uintptr_t Offset_PlayerName = 0x0288; // FString
        static constexpr uintptr_t Offset_PlayerID = 0x02A8;    // int32_t (Unique ID)
        static constexpr uintptr_t Offset_TeamInfo = 0x02B0;    // ATeamInfo*
        static constexpr uintptr_t Offset_Flags = 0x02B8;      // uint32_t (bitfield: bIsAdmin, bIsSpectator...)
        // Size: 0x0410

        explicit APlayerReplicationInfo(uintptr_t address = 0) : UObject(address) {}

        std::wstring GetPlayerName(const MemoryManager& pm) const;
        uintptr_t GetTeamInfoAddress(const MemoryManager& pm) const;
        ATeamInfo GetTeamInfo(const MemoryManager& pm) const;
        int32_t GetScore(const MemoryManager& pm) const;
        int32_t GetDeaths(const MemoryManager& pm) const;
        uint8_t GetPing(const MemoryManager& pm) const;
        int32_t GetPlayerID(const MemoryManager& pm) const;

        bool IsAdmin(const MemoryManager& pm) const;
        bool IsSpectator(const MemoryManager& pm) const;
        bool IsOnlySpectator(const MemoryManager& pm) const;
        bool IsWaitingPlayer(const MemoryManager& pm) const;
        bool IsReadyToPlay(const MemoryManager& pm) const;
        bool IsOutOfLives(const MemoryManager& pm) const;
        bool IsBot(const MemoryManager& pm) const;
        bool IsInactive(const MemoryManager& pm) const;
        bool IsFromPreviousLevel(const MemoryManager& pm) const;
        bool IsTimedOut(const MemoryManager& pm) const;
        bool IsUnregistered(const MemoryManager& pm) const;
    };

    class APRI : public APlayerReplicationInfo
    {
    public:
        static constexpr uintptr_t Offset_GameEvent = 0x0480; // AGameEvent* (Server version)
        static constexpr uintptr_t Offset_ReplicatedGameEvent = 0x0488; // AGameEvent* (Client replicated version)
        static constexpr uintptr_t Offset_Car = 0x0490;      // ACar* (Possessed/Associated Car)
        static constexpr uintptr_t Offset_CameraSettings = 0x0608; // ACameraSettingsActor_TA* (Camera pointer)
        static constexpr uintptr_t Offset_CameraSettingsFProfileCameraSettings = 0x0278; // FProfileCameraSettings offset inside ACameraSettingsActor_TA
        static constexpr uintptr_t Offset_BoostPickups = 0x0708; // int32_t
        static constexpr uintptr_t Offset_BallTouches = 0x070C; // int32_t
        static constexpr uintptr_t Offset_CarTouches = 0x0710; // int32_t
        // Size: 0x0BD0

        explicit APRI(uintptr_t address = 0) : APlayerReplicationInfo(address) {}

        uintptr_t GetCarAddress(const MemoryManager& pm) const;
        ACar GetCar(const MemoryManager& pm) const;
        int32_t GetBallTouches(const MemoryManager& pm) const;
        int32_t GetCarTouches(const MemoryManager& pm) const;
        int32_t GetBoostPickups(const MemoryManager& pm) const;
        uintptr_t GetGameEventAddress(const MemoryManager& pm) const; // Gets Server version
        AGameEvent GetGameEvent(const MemoryManager& pm) const;
        uintptr_t GetReplicatedGameEventAddress(const MemoryManager& pm) const; // Gets Client version
        AGameEvent GetReplicatedGameEvent(const MemoryManager& pm) const;
        std::optional<FProfileCameraSettings> GetCameraSettings(const MemoryManager& pm) const;
    };

    class APawn : public AActor
    {
    public:
        // Points to the PRI associated with this Pawn's controller
        static constexpr uintptr_t Offset_PlayerReplicationInfo = 0x0410; // APlayerReplicationInfo*
        // Size: 0x0514

        explicit APawn(uintptr_t address = 0) : AActor(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        uintptr_t GetPlayerInfoAddress(const MemoryManager& pm) const;
        // Returns the base PRI, cast to APRI if needed
        APlayerReplicationInfo GetPlayerInfo(const MemoryManager& pm) const;
    };

    class UBoostComponent : public UObject // Actually ActorComponent -> UObject
    {
    public:
        static constexpr uintptr_t Offset_ConsumptionRate = 0x0300; // float
        static constexpr uintptr_t Offset_MaxAmount = 0x0304; // float
        static constexpr uintptr_t Offset_StartAmount = 0x0308; // float
        static constexpr uintptr_t Offset_CurrentAmount = 0x0318; // float (ReplicatedBoostAmount)
        // Size: 0x0368

        explicit UBoostComponent(uintptr_t address = 0) : UObject(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        float GetAmount(const MemoryManager& pm) const;
        float GetMaxAmount(const MemoryManager& pm) const;
        float GetConsumptionRate(const MemoryManager& pm) const;
        float GetStartAmount(const MemoryManager& pm) const;
    };


    struct alignas(4) VehicleInputsData
    {
        float Throttle = 0.0f;        // 0x00
        float Steer = 0.0f;           // 0x04
        float Pitch = 0.0f;           // 0x08
        float Yaw = 0.0f;             // 0x0C
        float Roll = 0.0f;            // 0x10
        float DodgeForward = 0.0f;    // 0x14
        float DodgeRight = 0.0f;      // 0x18
        uint32_t CombinedInput = 0;   // 0x1C (bitfield)

        // Bitfield accessors
        bool Handbrake() const { return CombinedInput & 1; }
        bool Jump() const { return (CombinedInput >> 1) & 1; }
        bool ActivateBoost() const { return (CombinedInput >> 2) & 1; }
        bool HoldingBoost() const { return (CombinedInput >> 3) & 1; }
        bool Jumped() const { return (CombinedInput >> 4) & 1; } // Often related to dodge state start
        bool Grab() const { return (CombinedInput >> 5) & 1; }   // Rumble item related?
        bool ButtonMash() const { return (CombinedInput >> 6) & 1; } // Rumble item related?
    }; // Size: 0x20


    struct alignas(4) FWheelContactData
    {
        uint32_t Flags = 0; // 0x00 (bitfield for HasContact, HasContactWithWorldGeometry)
        float ContactChangeTime = 0.0f; // 0x04
        // ... other members like ContactPoint, ContactNormal, etc. up to 0x50

        bool HasContact() const { return Flags & 1; }
        bool HasContactWithWorldGeometry() const { return (Flags >> 1) & 1; }
    }; // Size: 0x50

    // --- Wheel ---
    class UWheel : public UObject
    {
    public:
        static constexpr uintptr_t Offset_WheelIndex = 0x0158; // int32_t
        static constexpr uintptr_t Offset_ContactData = 0x0160; // FWheelContactData
        // Size: 0x01E0

        explicit UWheel(uintptr_t address = 0) : UObject(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        FWheelContactData GetContactData(const MemoryManager& pm) const;
        int32_t GetWheelIndex(const MemoryManager& pm) const;
    };

    // --- Vehicle Sim ---
    class UVehicleSim : public UObject
    {
    public:
        static constexpr uintptr_t Offset_Wheels = 0x00A0;  // TArray<UWheel*>
        static constexpr uintptr_t Offset_Vehicle = 0x0130; // AVehicle* (Owner Vehicle)
        static constexpr uintptr_t Offset_Car = 0x0138;     // ACar* (Owner Car, convenience pointer?)
        // Size: 0x0164

        explicit UVehicleSim(uintptr_t address = 0) : UObject(address) {}

        // --- Methods (Definitions in Objects.cpp) ---
        std::vector<UWheel> GetWheels(const MemoryManager& pm) const;
        uintptr_t GetVehicleAddress(const MemoryManager& pm) const;
        AVehicle GetVehicle(const MemoryManager& pm) const;
        uintptr_t GetCarAddress(const MemoryManager& pm) const;
        ACar GetCar(const MemoryManager& pm) const;
    };


    // --- Vehicle ---
    class AVehicle : public APawn
    {
    public:
        // Inside ST_ReplicatedVehicleData or similar struct:
        static constexpr uintptr_t Offset_VehicleSim = 0x07B0;    // UVehicleSim*
        static constexpr uintptr_t Offset_Flags = 0x07C8;        // uint32_t (bitfield: bDriving, bJumped, bDoubleJumped, bOnGround, bSupersonic...)
        static constexpr uintptr_t Offset_ReplicatedInputs = 0x09A8; // VehicleInputs (ST_ReplicatedVehicleData -> ReplicatedInputs)
        // Direct members of AVehicle:
        static constexpr uintptr_t Offset_PRI = 0x0410;         // APRI* (ReplicatedPRI)
        static constexpr uintptr_t Offset_BoostComponent = 0x0840; // UBoostComponent*
        // Size: 0x08A8

        explicit AVehicle(uintptr_t address = 0) : APawn(address) {}

        uintptr_t GetPRIAddress(const MemoryManager& pm) const;
        APRI GetPRI(const MemoryManager& pm) const;
        VehicleInputsData GetInputs(const MemoryManager& pm) const;
        uintptr_t GetBoostComponentAddress(const MemoryManager& pm) const;
        UBoostComponent GetBoostComponent(const MemoryManager& pm) const;
        uintptr_t GetVehicleSimAddress(const MemoryManager& pm) const;
        UVehicleSim GetVehicleSim(const MemoryManager& pm) const;

        // Checks if all wheels on the sim have contact
        bool HasWheelContact(const MemoryManager& pm) const;

        // Bitfield flags accessors
        bool IsDriving(const MemoryManager& pm) const;
        bool IsJumped(const MemoryManager& pm) const;        // Initial Jump Press/State
        bool IsDoubleJumped(const MemoryManager& pm) const;  // Dodge/Second Jump Active
        bool IsOnGround(const MemoryManager& pm) const;
        bool IsSupersonic(const MemoryManager& pm) const;
        bool IsPodiumMode(const MemoryManager& pm) const;    // Post-match celebration?
        bool HasPostMatchCelebration(const MemoryManager& pm) const;
    };

    class ACar : public AVehicle
    {
    public:
        static constexpr uintptr_t Offset_AttackerPRI = 0x09E0; // APRI* (Last car touch/demolition related)
        // Size: 0x0B48

        explicit ACar(uintptr_t address = 0) : AVehicle(address) {}

        uintptr_t GetAttackerPRIAddress(const MemoryManager& pm) const;
        APRI GetAttackerPRI(const MemoryManager& pm) const;
    };

    class ATeamInfo : public UObject // Typically inherits AReplicationInfo -> AInfo -> AActor
    {
    public:
        static constexpr uintptr_t Offset_TeamName = 0x0268; // FString
        // static constexpr uintptr_t Offset_Size = 0x0278; // Usually not directly on TeamInfo, use Members count
        static constexpr uintptr_t Offset_Score = 0x027C;    // int32_t
        static constexpr uintptr_t Offset_TeamIndex = 0x0280;// int32_t (0 or 1)
        static constexpr uintptr_t Offset_TeamColor = 0x0284;// FColorData
        // Size: 0x0290

        explicit ATeamInfo(uintptr_t address = 0) : UObject(address) {}

        std::wstring GetName(const MemoryManager& pm) const;
        int32_t GetSize(const MemoryManager& pm) const; // Might need dynamic_cast to ATeam and read Members
        int32_t GetScore(const MemoryManager& pm) const;
        int32_t GetIndex(const MemoryManager& pm) const;
        FColorData GetColor(const MemoryManager& pm) const;
    };

    class ATeam : public ATeamInfo
    {
    public:
        static constexpr uintptr_t Offset_Members = 0x0318; // TArray<APRI*>
        // Size: 0x0468

        explicit ATeam(uintptr_t address = 0) : ATeamInfo(address) {}

        std::vector<APRI> GetMembers(const MemoryManager& pm) const;
    };

    class AController : public AActor
    {
    public:
        explicit AController(uintptr_t address = 0) : AActor(address) {}

        // 0x0460 (0x04)
        class APawn* Pawn;                                                // 0x0280(0x0008)

        class APawn* GetPawn(const MemoryManager& mem) const
        {
            return mem.Read<APawn*>(this->Address + 0x0280).value_or(nullptr);
        }
    };

    class APlayerController : public AController
    {
    public:
        static constexpr uintptr_t Offset_AcknowledgedPawn = 0x0998;
        static constexpr uintptr_t Offset_PlayerReplicationInfo = 0x09A0; // Updated from 0x0988 to 0x09A0
        static constexpr uintptr_t Offset_PlayerCamera = 0x0480; // CORRECT Offset to ACamera*
        // Size: 0x0D00

        explicit APlayerController(uintptr_t address = 0) : AController(address) {}

        APawn* GetAcknowledgedPawn(const MemoryManager& mem) const
        {
            return mem.Read<APawn*>(this->Address + Offset_AcknowledgedPawn).value_or(nullptr);
        }

        uintptr_t GetPRIActorAddress(const MemoryManager& pm) const;
        APRI GetPRIActor(const MemoryManager& pm) const;
        APRI GetPRI(const MemoryManager& pm) const { return GetPRIActor(pm); }
        uintptr_t GetCarAddress(const MemoryManager& pm) const;
        ACar GetCar(const MemoryManager& pm) const;

        uintptr_t GetPlayerCameraAddress(const MemoryManager& pm) const;
        ACamera GetPlayerCamera(const MemoryManager& pm) const;
    };

    struct alignas(8) FPickupData // alignas(8) because contains pointer
    {
        uintptr_t InstigatorCarAddress = 0; // 0x00 ACar*
        uint8_t PickedUpFlag = 0;           // 0x08 bit 0

        bool IsPickedUp() const { return PickedUpFlag & 1; }
    }; // Size: >= 0x09 (Likely 0x10 due to alignment)

    class AVehiclePickup : public AActor
    {
    public:
        static constexpr uintptr_t Offset_RespawnDelay = 0x0268; // float
        static constexpr uintptr_t Offset_PickupData = 0x02A8;   // FPickupData

        explicit AVehiclePickup(uintptr_t address = 0) : AActor(address) {}

        FPickupData GetPickupData(const MemoryManager& pm) const;
        ACar GetInstigatorCar(const MemoryManager& pm) const; // Helper using PickupData
        bool IsPickedUp(const MemoryManager& pm) const; // Helper using PickupData
        float GetRespawnDelay(const MemoryManager& pm) const;
    };

    // --- Vehicle Pickup Boost ---
    class AVehiclePickup_Boost : public AVehiclePickup
    {
    public:
        static constexpr uintptr_t Offset_BoostAmount = 0x02F0; // float
        static constexpr uintptr_t Offset_BoostType = 0x0300;   // uint8_t (enum?) - May not exist, amount might define type

        explicit AVehiclePickup_Boost(uintptr_t address = 0) : AVehiclePickup(address) {}

        float GetBoostAmount(const MemoryManager& pm) const;
        uint8_t GetBoostType(const MemoryManager& pm) const; // May return 0 if unused
        bool IsBigPad(const MemoryManager& pm) const; // Example logic based on amount
    };

    class AGoal : public UObject // Typically inherits ATrigger -> AActor
    {
    public:
        static constexpr uintptr_t Offset_TeamNum = 0x00DC;   // uint8_t
        static constexpr uintptr_t Offset_Location = 0x0138; // FVector (World Loc of goal center?)
        static constexpr uintptr_t Offset_Direction = 0x0144;// FVector (Goal Direction vector)
        static constexpr uintptr_t Offset_Right = 0x0150;    // FVector (Goal Right vector)
        static constexpr uintptr_t Offset_Up = 0x015C;       // FVector (Goal Up vector)
        static constexpr uintptr_t Offset_Rotation = 0x0168; // FRotator (World Rot)
        static constexpr uintptr_t Offset_WorldBox = 0x01A4; // FBoxData (Bounding box)
        // Size: 0x01C0

        explicit AGoal(uintptr_t address = 0) : UObject(address) {}

        uint8_t GetTeamNum(const MemoryManager& pm) const;
        FVectorData GetLocation(const MemoryManager& pm) const;
        FVectorData GetDirection(const MemoryManager& pm) const;
        FVectorData GetRight(const MemoryManager& pm) const;
        FVectorData GetUp(const MemoryManager& pm) const;
        FRotatorData GetRotation(const MemoryManager& pm) const;
        FBoxData GetWorldBox(const MemoryManager& pm) const;

        float GetWidth(const MemoryManager& pm) const; // Calculated from Box (Assumes Y-axis)
        float GetHeight(const MemoryManager& pm) const; // Calculated from Box (Assumes Z-axis)
        float GetDepth(const MemoryManager& pm) const; // Calculated from Box (Assumes X-axis)
    };

    class UGameViewportClient : public UObject // Inherits from UObject directly or via intermediate classes
    {
    public:
        // Pointer to the current GameEvent (may differ from server version)
        static constexpr uintptr_t Offset_GameEvent = 0x02E8; // AGameEvent* (Verify this offset)
        // Size: 0x03C0

        explicit UGameViewportClient(uintptr_t address = 0) : UObject(address) {}

        uintptr_t GetGameEventAddress(const MemoryManager& pm) const;
        AGameEvent GetGameEvent(const MemoryManager& pm) const;
    };

    // Simple 3D Vector (Value type, not pointer based)
    struct Vector3D {
        float X = 0.0f, Y = 0.0f, Z = 0.0f;

        Vector3D() = default;
        Vector3D(float x, float y, float z) : X(x), Y(y), Z(z) {}
        // Construct from FVectorData read from memory
        Vector3D(const FVectorData& data) : X(data.X), Y(data.Y), Z(data.Z) {}

        float DistanceTo(const Vector3D& other) const {
            float dx = X - other.X;
            float dy = Y - other.Y;
            float dz = Z - other.Z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    };

    // Boost Pad State Helper
    class BoostPadState {
    public:
        Vector3D Location; // Known static location
        bool IsBigPad;
        bool IsActive = true; // Current state (available or cooling down)
        // Store the address of the corresponding AVehiclePickup_Boost for updates
        uintptr_t PickupActorAddress = 0;
        // Use chrono for time tracking
        std::chrono::steady_clock::time_point PickedUpTime;

        BoostPadState(float x, float y, float z, bool isBig)
            : Location(x, y, z), IsBigPad(isBig) {
        }

        // --- Methods (Definitions in Objects.cpp) ---
        void Reset(); // Resets IsActive, clears timer
        float RespawnTimeSeconds() const; // Returns 4.0f or 10.0f
        std::optional<float> GetElapsedSeconds() const; // Time since picked up
        std::optional<float> GetRemainingSeconds() const; // Time until respawn
        void MarkInactive(); // Sets IsActive false, starts timer
        void UpdateState(MemoryManager& pm); // Reads actor state and updates IsActive/timer
    };


    // Field State Helper (Manages Boost Pads)
    class FieldState {
    public:
        std::vector<BoostPadState> BoostPads;

        // Static boost pad locations (as in Python)
        static constexpr struct BoostLoc { float x, y, z; bool big; } BOOST_LOCATIONS[] = {
            {0.0f, -4240.0f, 70.0f, false}, { -1792.0f, -4184.0f, 70.0f, false}, { 1792.0f, -4184.0f, 70.0f, false},
            {-3072.0f, -4096.0f, 73.0f, true}, { 3072.0f, -4096.0f, 73.0f, true}, { -940.0f, -3308.0f, 70.0f, false},
            {940.0f, -3308.0f, 70.0f, false}, { 0.0f, -2816.0f, 70.0f, false}, { -3584.0f, -2484.0f, 70.0f, false},
            {3584.0f, -2484.0f, 70.0f, false}, { -1788.0f, -2300.0f, 70.0f, false}, { 1788.0f, -2300.0f, 70.0f, false},
            {-2048.0f, -1036.0f, 70.0f, false}, { 0.0f, -1024.0f, 70.0f, false}, { 2048.0f, -1036.0f, 70.0f, false},
            {-3584.0f, 0.0f, 73.0f, true}, { -1024.0f, 0.0f, 70.0f, false}, { 1024.0f, 0.0f, 70.0f, false},
            {3584.0f, 0.0f, 73.0f, true}, { -2048.0f, 1036.0f, 70.0f, false}, { 0.0f, 1024.0f, 70.0f, false},
            {2048.0f, 1036.0f, 70.0f, false}, { -1788.0f, 2300.0f, 70.0f, false}, { 1788.0f, 2300.0f, 70.0f, false},
            {-3584.0f, 2484.0f, 70.0f, false}, { 3584.0f, 2484.0f, 70.0f, false}, { 0.0f, 2816.0f, 70.0f, false},
            {-940.0f, 3310.0f, 70.0f, false}, { 940.0f, 3308.0f, 70.0f, false}, { -3072.0f, 4096.0f, 73.0f, true},
            {3072.0f, 4096.0f, 73.0f, true}, { -1792.0f, 4184.0f, 70.0f, false}, { 1792.0f, 4184.0f, 70.0f, false},
            {0.0f, 4240.0f, 70.0f, false}
        };

        FieldState() {
            BoostPads.reserve(std::size(BOOST_LOCATIONS));
            for (const auto& loc : BOOST_LOCATIONS) {
                BoostPads.emplace_back(loc.x, loc.y, loc.z, loc.big);
            }
        }

        void ResetBoostPads(); // Resets all pads to active state

        // Finds a pad state object based on its known static location
        BoostPadState* FindPadByLocation(const Vector3D& location, float tolerance = 100.0f);

        // Finds a pad state object based on the memory address of its pickup actor
        BoostPadState* FindPadByActorAddress(uintptr_t actorAddress);

        // Updates all boost pad states by reading memory, requires a list of relevant pickup actors
        void UpdateAllPads(MemoryManager& pm, const std::vector<AVehiclePickup_Boost>& all_boost_pickups);
    };

    class ACamera : public AActor // Inherits Location/Rotation offsets
    {
    public:
        // We don't need specific ACamera members for now, just the type
        // And its inherited AActor methods (GetLocation, GetRotation)
        explicit ACamera(uintptr_t address = 0) : AActor(address) {}

        // If CalcCamera is needed later, add its declaration here
        // bool CalcCamera(const MemoryManager& pm, float DeltaTime, FVectorData& outLoc, FRotatorData& outRot, float& outFOV);
    };

}