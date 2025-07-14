#include "GNameTable.h"
#include "Objects.hpp" // Include full Objects.hpp here for SDK::TArrayLayout definition
#include <iostream>
#include <vector>

GNameTable::GNameTable() : gnamesArrayAddress_(0), initialized_(false) {}

bool GNameTable::Initialize(const MemoryManager& pm, uintptr_t moduleBase, uintptr_t gnamesOffset) {
    initialized_ = false;
    names_.clear();
    if (!pm.IsAttached() || moduleBase == 0 || gnamesOffset == 0) return false;

    gnamesArrayAddress_ = moduleBase + gnamesOffset;
    // Read the TArray layout for GNames
    auto layoutOpt = pm.Read<SDK::TArrayLayout>(gnamesArrayAddress_);
    if (!layoutOpt) { gnamesArrayAddress_ = 0; return false; }

    SDK::TArrayLayout layout = *layoutOpt;
    if (layout.ArrayData == 0 || layout.ArrayCount <= 0 || layout.ArrayCount > 500000) { gnamesArrayAddress_ = 0; return false; }

    names_.reserve(static_cast<size_t>(layout.ArrayCount));
    constexpr size_t pointerSize = sizeof(uintptr_t);
    int successfulLoads = 0;

    for (int32_t i = 0; i < layout.ArrayCount; ++i) {
        uintptr_t entryPtrAddress = layout.ArrayData + i * pointerSize;
        auto entryPtrOpt = pm.Read<uintptr_t>(entryPtrAddress);
        if (!entryPtrOpt || *entryPtrOpt == 0) continue;
        uintptr_t entryAddress = *entryPtrOpt;

        // Read the index from the entry for validation using the offset defined in FNameEntry
        auto indexOpt = pm.Read<int32_t>(entryAddress + SDK::FNameEntry::Offset_IndexInEntry);
        if (!indexOpt) continue;
        
        // CRITICAL CHECK: Validate index matches expected slot index
        int32_t readIndex = *indexOpt;
        if (readIndex != i) continue;  // Skip if index doesn't match (corrupted/reallocated entry)
        
        std::optional<std::string> nameOpt = ReadNameString(pm, entryAddress);
        if (nameOpt && !nameOpt->empty()) {
            names_[i] = *nameOpt;
            successfulLoads++;
        }
    }

    if (names_.empty() && successfulLoads == 0) { gnamesArrayAddress_ = 0; return false; }
    initialized_ = true;
    return true;
}

std::optional<std::string> GNameTable::ReadNameString(const MemoryManager& pm, uintptr_t entryAddress) const {
    if (entryAddress == 0) return std::nullopt;
    
    // Read the index from the entry first using the offset defined in FNameEntry
    auto entryIndexOpt = pm.Read<int32_t>(entryAddress + SDK::FNameEntry::Offset_IndexInEntry);
    if (!entryIndexOpt) return std::nullopt;
    
    // Get string address at the expected offset using info from FNameEntry
    uintptr_t stringAddress = entryAddress + SDK::FNameEntry::Offset_StringData;

    // Read the string data based on whether it's wide or ansi
    if constexpr (SDK::FNameEntry::IsWideChar) {
        std::vector<wchar_t> buffer(SDK::FNameEntry::MaxNameLength, L'\0');
        if (pm.ReadBytes(stringAddress, buffer.data(), (SDK::FNameEntry::MaxNameLength - 1) * sizeof(wchar_t))) {
            buffer[SDK::FNameEntry::MaxNameLength - 1] = L'\0'; // Ensure null termination
            std::wstring wide_str(buffer.data());
            std::string narrow_str;
            narrow_str.reserve(wide_str.length());
            for (wchar_t wc : wide_str) { 
                if (wc == 0) break; // Stop at null terminator
                if (wc > 0 && wc < 128) narrow_str += static_cast<char>(wc); 
                else narrow_str += '?'; 
            }
            // Return the string only if not empty
            if (!narrow_str.empty()) return narrow_str;
        }
    }
    else {
        std::vector<char> buffer(SDK::FNameEntry::MaxNameLength, '\0');
        if (pm.ReadBytes(stringAddress, buffer.data(), SDK::FNameEntry::MaxNameLength - 1)) {
            buffer[SDK::FNameEntry::MaxNameLength - 1] = '\0'; // Ensure null termination
            std::string result(buffer.data());
            // Return the string only if not empty
            if (!result.empty()) return result;
        }
    }
    
    return std::nullopt;
}

std::string GNameTable::GetName(int32_t index) const {
    if (!initialized_) return "[Name Not Found]";
    if (index < 0) return "[Invalid Index]";
    
    // Look up the name in our map
    auto it = names_.find(index);
    if (it != names_.end()) {
        return it->second;
    }
    
    return "[Name Not Found]";
}

bool GNameTable::IsInitialized() const { return initialized_; }
size_t GNameTable::GetNameCount() const { return names_.size(); }