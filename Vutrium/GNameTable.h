#ifndef GNAME_TABLE_H
#define GNAME_TABLE_H

#include "MemoryManager.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

// Forward declare TArrayHeader if Objects.hpp is too heavy or causes circular dependencies
namespace SDK { struct TArrayHeader; }

class GNameTable {
public:
    GNameTable();
    bool Initialize(const MemoryManager& pm, uintptr_t moduleBase, uintptr_t gnamesOffset);
    std::string GetName(int32_t index) const;
    bool IsInitialized() const; // Declaration ONLY
    size_t GetNameCount() const; // Declaration ONLY

private:
    struct FNameEntryData {
        static constexpr uintptr_t Offset_IndexInEntry = 0x08; // VERIFIED (Assuming 8 bytes for Flags)
        static constexpr uintptr_t Offset_StringData = 0x18; // VERIFIED (8 + 4 + 12 = 24 = 0x18)
        static constexpr bool      IsWideChar = true;   // VERIFIED (CharSet = CharSet.Unicode)
        static constexpr size_t    MaxNameLength = 1024;
    };

    uintptr_t gnamesArrayAddress_ = 0;
    std::unordered_map<int32_t, std::string> names_;
    bool initialized_ = false;

    std::optional<std::string> ReadNameString(const MemoryManager& pm, uintptr_t entryAddress) const; // Declaration ONLY
};

#endif