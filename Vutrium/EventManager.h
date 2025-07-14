#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <functional>
#include <vector>
#include <unordered_map>
#include <string>
#include <mutex>
#include "EventData.h"

class EventManager {
public:
    using Callback = std::function<void(const EventData&)>;
    void Subscribe(const std::string& eventType, Callback callback);
    void Unsubscribe(const std::string& eventType, const Callback& callback); // Use with caution
    void Fire(const std::string& eventType, const EventData& data);
    void Clear(const std::string& eventType);
    void ClearAll();
private:
    std::unordered_map<std::string, std::vector<Callback>> subscribers_;
    std::mutex mutex_;
};

#endif