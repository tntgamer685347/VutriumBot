#ifndef EVENT_DATA_H
#define EVENT_DATA_H

#include <string>
#include <vector>
#include <cstdint>
#include "Objects.hpp"

// Forward declarations
struct EventFunctionHookedData; struct EventPlayerTickData; struct EventRoundActiveStateChangedData; struct EventResetPickupsData; struct EventGameEventStartedData; struct EventKeyPressedData; struct EventBoostPadChangedData; struct EventGameEventDestroyedData;
struct EventViewportTickData;
struct EventProbableGameEventFoundData;

struct EventData { virtual ~EventData() = default; }; // Base with virtual destructor

struct EventFunctionHookedData : EventData {
    SDK::UFunction Function; uintptr_t CallerAddress;
    EventFunctionHookedData(const SDK::UFunction& func, uintptr_t caller) : Function(func), CallerAddress(caller) {}
};
struct EventPlayerTickData : EventData {
    uintptr_t PlayerControllerAddress; float DeltaTime;
    EventPlayerTickData(uintptr_t pcAddr, float dt) : PlayerControllerAddress(pcAddr), DeltaTime(dt) {}
};
struct EventRoundActiveStateChangedData : EventData {
    uintptr_t GameEventAddress; bool IsActive;
    EventRoundActiveStateChangedData(uintptr_t geAddr, bool active) : GameEventAddress(geAddr), IsActive(active) {}
};
struct EventResetPickupsData : EventData {
    uintptr_t GameEventAddress;
    EventResetPickupsData(uintptr_t geAddr) : GameEventAddress(geAddr) {}
};
struct EventGameEventStartedData : EventData {
    uintptr_t GameEventAddress;
    EventGameEventStartedData(uintptr_t geAddr) : GameEventAddress(geAddr) {}
};
struct EventKeyPressedData : EventData {
    enum class KeyEventType : uint8_t { Pressed = 0, Released = 1, Repeat = 2, DoubleClick = 3, Axis = 4 };
    uintptr_t ViewportClientAddress; int32_t ControllerId; std::string KeyName; KeyEventType EventType; float AmountDepressed; bool IsGamepad; bool ReturnValue;
    EventKeyPressedData(uintptr_t vpAddr, int32_t ctrlId, const std::string& key, KeyEventType type, bool gamepad, bool retVal, float amount = 0.0f) : ViewportClientAddress(vpAddr), ControllerId(ctrlId), KeyName(key), EventType(type), IsGamepad(gamepad), ReturnValue(retVal), AmountDepressed(amount) {}
};
struct EventBoostPadChangedData : EventData {
    uintptr_t BoostPickupAddress; bool IsNowActive;
    EventBoostPadChangedData(uintptr_t pickupAddr, bool active) : BoostPickupAddress(pickupAddr), IsNowActive(active) {}
};
struct EventGameEventDestroyedData : EventData {
    uintptr_t GameEventAddress;
    EventGameEventDestroyedData(uintptr_t geAddr) : GameEventAddress(geAddr) {}
};

struct EventViewportTickData : EventData {
    uintptr_t ViewportClientAddress;
    uintptr_t CurrentGameEventAddress; // Address read from ViewportClient
    EventViewportTickData(uintptr_t vpAddr, uintptr_t geAddr) 
        : ViewportClientAddress(vpAddr), CurrentGameEventAddress(geAddr) {}
};

struct EventProbableGameEventFoundData : EventData {
    uintptr_t ProbableGameEventAddress;
    EventProbableGameEventFoundData(uintptr_t geAddr)
        : ProbableGameEventAddress(geAddr) {}
};


namespace EventType {
    inline constexpr const char* OnFunctionHooked = "OnFunctionHooked"; inline constexpr const char* OnPlayerTick = "OnPlayerTick"; inline constexpr const char* OnRoundActiveStateChanged = "OnRoundActiveStateChanged"; inline constexpr const char* OnResetPickups = "OnResetPickups"; inline constexpr const char* OnGameEventStarted = "OnGameEventStarted"; inline constexpr const char* OnKeyPressed = "OnKeyPressed"; inline constexpr const char* OnBoostPadStateChanged = "OnBoostPadStateChanged"; inline constexpr const char* OnGameEventDestroyed = "OnGameEventDestroyed";
    inline constexpr const char* OnViewportTick = "OnViewportTick";
    inline constexpr const char* OnProbableGameEventFound = "OnProbableGameEventFound";
}

#endif