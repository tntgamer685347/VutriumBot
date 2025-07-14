#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <mutex>
#include <sstream> // Needed for to_hex
#include "includes.h"
#include <iomanip> // Needed for to_hex

namespace Logger {
    bool Initialize();
    void Shutdown();
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    void MinHookError(const std::string& prefix, int mhStatus);

    inline std::string to_hex(uintptr_t p) {
        std::stringstream ss;
        ss << "0x" << std::hex << p;
        return ss.str();
    }
} // namespace Logger

#endif