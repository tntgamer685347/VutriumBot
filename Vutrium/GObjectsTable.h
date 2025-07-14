#ifndef GOBJECTS_TABLE_H
#define GOBJECTS_TABLE_H

#include "MemoryManager.h"
#include "Objects.hpp"
#include "GNameTable.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

class GObjectsTable {
public:
    GObjectsTable();
    bool Initialize(const MemoryManager& pm, const GNameTable& gnames, uintptr_t moduleBase, uintptr_t gobjectsOffset);
    SDK::UClass FindStaticClass(const std::string& fullName) const;
    SDK::UFunction FindStaticFunction(const std::string& fullName) const;
    SDK::UObject GetObjectByIndex(const MemoryManager& pm, int32_t index) const;
    int32_t GetObjectCount(const MemoryManager& pm) const;
    bool IsInitialized() const;
    size_t GetMappedClassCount() const;
    size_t GetMappedFunctionCount() const;

private:
    uintptr_t gobjectsArrayAddress_ = 0;
    std::unordered_map<std::string, SDK::UClass> staticClasses_;
    std::unordered_map<std::string, SDK::UFunction> staticFunctions_;
    bool initialized_ = false;
};

#endif