#include "EventManager.h"
#include <algorithm>

void EventManager::Subscribe(const std::string& eventType, Callback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[eventType].push_back(callback);
}

void EventManager::Unsubscribe(const std::string& eventType, const Callback& callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribers_.count(eventType)) {
        auto& subs = subscribers_[eventType];
        subs.erase(std::remove_if(subs.begin(), subs.end(),
            [&](const Callback& C) { return C.target_type() == callback.target_type(); }), // Basic comparison
            subs.end());
    }
}

void EventManager::Fire(const std::string& eventType, const EventData& data) {
    std::vector<Callback> callbacksToCall;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (subscribers_.count(eventType)) callbacksToCall = subscribers_[eventType];
    }
    for (const auto& callback : callbacksToCall) { if (callback) callback(data); }
}

void EventManager::Clear(const std::string& eventType) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribers_.count(eventType)) subscribers_[eventType].clear();
}

void EventManager::ClearAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
}