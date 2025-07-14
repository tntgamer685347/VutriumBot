#include "Logger.h"
#include <windows.h>
#include <ShlObj.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <vector>
#include "kiero/minhook/include/MinHook.h"


#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")

namespace {
    FILE* g_conOutFile = nullptr;
    std::mutex g_logMutex;
    bool g_consoleAllocated = false;
    bool DebugBuild = true;


    std::string GetTimestampSafe() {
        try {
            auto now = std::chrono::system_clock::now();
            auto now_c = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
#pragma warning(suppress : 4996)
            std::tm timeinfo = *std::localtime(&now_c);
            ss << std::setfill('0') << std::setw(4) << (timeinfo.tm_year + 1900) << '-' << std::setw(2) << (timeinfo.tm_mon + 1) << '-' << std::setw(2) << timeinfo.tm_mday << ' ' << std::setw(2) << timeinfo.tm_hour << ':' << std::setw(2) << timeinfo.tm_min << ':' << std::setw(2) << timeinfo.tm_sec;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
            ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
            return ss.str();
        }
        catch (...) { return "TIMESTAMP_ERROR"; }
    }

    void LogInternal(const std::string& level, const std::string& message) {
        if (!DebugBuild) return;
        if (!g_consoleAllocated) { OutputDebugStringA(("[LogInternal] Console not alloc. Msg: [" + level + "] " + message + "\n").c_str()); return; }
        std::string logEntry = "[" + GetTimestampSafe() + "] [" + level + "] " + message + "\n";
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_conOutFile) { fprintf(g_conOutFile, "%s", logEntry.c_str()); fflush(g_conOutFile); }
    }
} // anon namespace

namespace Logger {
    bool Initialize() {
        if (!DebugBuild) return true;
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_consoleAllocated) return true;
        if (AllocConsole()) {
            if (freopen_s(&g_conOutFile, "CONOUT$", "w", stdout) == 0) {
                SetConsoleTitleW(L"Vutrium SDK Log"); g_consoleAllocated = true;
                printf("[INFO] Logger Console Initialized.\n"); fflush(stdout); return true;
            }
            else { perror("freopen_s failed"); FreeConsole(); return false; }
        }
        else {
            if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE) {
                if (freopen_s(&g_conOutFile, "CONOUT$", "w", stdout) == 0) {
                    OutputDebugStringA("Logger::Initialize(): Console already existed, redirected stdout.\n");
                    g_consoleAllocated = true; printf("[INFO] Logger Console Attached (already existed).\n"); fflush(stdout); return true;
                }
                else { OutputDebugStringA("Logger::Initialize(): Console existed, but freopen_s failed.\n"); perror("freopen_s failed"); return false; }
            }
            else { OutputDebugStringA("Logger::Initialize(): AllocConsole failed and GetStdHandle failed.\n"); return false; }
        }
    }
    void Shutdown() {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_consoleAllocated) {
            LogInternal("INFO", "Logger Shutting Down Console.");
            if (g_conOutFile) { fclose(g_conOutFile); g_conOutFile = nullptr; }
            FreeConsole(); g_consoleAllocated = false;
        }
    }
    void Info(const std::string& message) { LogInternal("INFO", message); }
    void Warning(const std::string& message) { LogInternal("WARN", message); }
    void Error(const std::string& message) { LogInternal("ERROR", message); }
    void MinHookError(const std::string& prefix, int mhStatus) { Error(prefix + ": " + MH_StatusToString(static_cast<MH_STATUS>(mhStatus)) + " (Code: " + std::to_string(mhStatus) + ")"); }
}