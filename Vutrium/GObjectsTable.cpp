#include "GObjectsTable.h"
#include "Logger.h"      // Include Logger
#include <iostream>
#include <string_view>   // For efficient string checking
#include <exception>     // For std::exception

GObjectsTable::GObjectsTable() : gobjectsArrayAddress_(0), initialized_(false) {}

bool GObjectsTable::Initialize(const MemoryManager& pm, const GNameTable& gnames, uintptr_t moduleBase, uintptr_t gobjectsOffset) {
    initialized_ = false; // Reset status
    staticClasses_.clear();
    staticFunctions_.clear();

    // Crucially check if GNameTable is initialized *before* proceeding
    if (!pm.IsAttached() || !gnames.IsInitialized() || moduleBase == 0 || gobjectsOffset == 0) {
        Logger::Error("GObjectsTable Error: Invalid parameters or GNameTable not initialized.");
        return false;
    }

    gobjectsArrayAddress_ = moduleBase + gobjectsOffset;
    // Logger::Info("GObjectsTable: Calculated GObjects TArray address: " + Logger::to_hex(gobjectsArrayAddress_)); 

    // Read the TArray layout for GObjects
    auto layoutOpt = pm.Read<SDK::TArrayLayout>(gobjectsArrayAddress_);
    if (!layoutOpt) {
        Logger::Error("GObjectsTable Error: Failed to read TArray layout at " + Logger::to_hex(gobjectsArrayAddress_));
        gobjectsArrayAddress_ = 0;
        return false;
    }

    SDK::TArrayLayout layout = *layoutOpt;
    // Logger::Info("GObjectsTable: Layout Count=" + std::to_string(layout.ArrayCount) + ", Data=" + Logger::to_hex(layout.ArrayData));

    // Basic sanity checks for the TArray
    const int32_t maxReasonableObjectCount = 10'000'000; // Adjust if necessary
    if (layout.ArrayData == 0 || layout.ArrayCount <= 0 || layout.ArrayCount > maxReasonableObjectCount) {
        Logger::Error("GObjectsTable Error: Invalid TArray layout data (Count=" + std::to_string(layout.ArrayCount) + ", Data=" + Logger::to_hex(layout.ArrayData) + ")");
        gobjectsArrayAddress_ = 0;
        return false;
    }

    constexpr size_t pointerSize = sizeof(uintptr_t);
    int mappedClasses = 0;
    int mappedFunctions = 0;
    int skippedObjects = 0;
    int errorCount = 0;
    const int logModulo = 10000; // Log progress less frequently

    Logger::Info("GObjectsTable: Starting object mapping loop (" + std::to_string(layout.ArrayCount) + " objects)...");

    // --- Map Static Objects ---
    // This loop assumes GObjects TArray contains POINTERS to UObject instances.
    for (int32_t i = 0; i < layout.ArrayCount; ++i) {
        /* // REMOVED Progress Logging
        if (i > 0 && i % logModulo == 0) { // Log progress periodically
            Logger::Info("GObjectsTable: Processing index " + std::to_string(i) + "...");
        }
        */

        uintptr_t objPtrAddress = layout.ArrayData + i * pointerSize;
        auto objPtrOpt = pm.Read<uintptr_t>(objPtrAddress);

        // Skip null pointers in the array
        if (!objPtrOpt || *objPtrOpt == 0) {
            skippedObjects++;
            continue;
        }

        uintptr_t objAddress = *objPtrOpt;
        SDK::UObject currentObject(objAddress); // Create wrapper

        // Skip potential self-references or very early objects that might be invalid
        if (objAddress < 0x1000 ||
            currentObject.Address == currentObject.GetOuterAddress(pm) ||
            currentObject.Address == currentObject.GetClassAddress(pm)) {
            skippedObjects++;
            continue;
        }


        // Add try-catch specifically around GetFullName
        std::string fullName;
        try {
            // Pass const reference to GetFullName
            fullName = currentObject.GetFullName(pm, gnames);

            // Optional detailed logging (can spam the log quickly)
            // if (i < 100 || (i % (logModulo * 10) == 0)) {
            //      Logger::Info("GObjectsTable [" + std::to_string(i) + " Addr: " + Logger::to_hex(objAddress) + "] FullName: '" + fullName + "'");
            // }

        }
        catch (const std::exception& e) {
            errorCount++;
            if (errorCount < 20) { // Limit error logging spam
                Logger::Error("GObjectsTable Error getting name for index " + std::to_string(i) + " Addr: " + Logger::to_hex(objAddress) + ": " + e.what());
            }
            continue; // Skip this object if GetFullName failed
        }
        catch (...) {
            errorCount++;
            if (errorCount < 20) {
                Logger::Error("GObjectsTable Unknown Error getting name for index " + std::to_string(i) + " Addr: " + Logger::to_hex(objAddress));
            }
            continue;
        }


        // Check if a valid name was returned before trying to find substrings
        if (fullName.empty() || fullName == "None" || fullName.find("Error") != std::string::npos || fullName.find("Invalid") != std::string::npos) {
            skippedObjects++;
            continue; // Skip objects with invalid names
        }


        if (fullName.find("Class ") != std::string::npos) {
            staticClasses_[fullName] = SDK::UClass(objAddress); // Store UClass wrapper
            mappedClasses++;
            // Optional: Log first few finds
            // if (mappedClasses < 10) Logger::Info("Mapped Class [" + std::to_string(mappedClasses) + "]: " + fullName);
        }
        else if (fullName.find("Function ") != std::string::npos) {
            staticFunctions_[fullName] = SDK::UFunction(objAddress); // Store UFunction wrapper
            mappedFunctions++;
            // Optional: Log first few finds
            // if (mappedFunctions < 10) Logger::Info("Mapped Function [" + std::to_string(mappedFunctions) + "]: " + fullName);
        }
        // else: It's some other type of UObject, we don't map it by default.

    } // End object loop

    Logger::Info("GObjectsTable: Mapping loop finished.");
    Logger::Info("GObjectsTable: Mapped " + std::to_string(mappedClasses) + " classes, "
        + std::to_string(mappedFunctions) + " functions. Skipped/Other: "
        + std::to_string(skippedObjects) + ". Errors: " + std::to_string(errorCount));

    // Instead of class traversal, do a second direct pass through the GObjects array to find functions
    // This is more similar to how RocketDumper.cs works
    Logger::Info("GObjectsTable: Starting direct function scan...");
    int directMappedFunctions = 0;
    int directErrors = 0;

    // Define a simplified UObject struct that matches the new SDK::UObject layout
    // We can directly use SDK::UObject now if needed, but the direct read approach might still be beneficial.
    // Let's adapt the DirectUObject struct to match SDK::UObject for clarity
    struct DirectUObjectLayout { // Renamed to avoid conflict if SDK::UObject is included directly later
        // Based on SDK::UObject in Objects.hpp (Updated Layout)
        uintptr_t VfTable;                             // 0x0000
        uintptr_t HashNext;                            // 0x0008
        uint64_t ObjectFlags;                          // 0x0010
        uintptr_t HashOuterNext;                       // 0x0018
        uintptr_t StateFrame;                          // 0x0020
        uintptr_t Linker;                              // 0x0028
        int32_t LinkerIndex;                           // 0x0030
        int32_t ObjectInternalInteger;                 // 0x0034
        int32_t NetIndex;                              // 0x0038
        uintptr_t Outer;                               // 0x0040 (UObject*)
        int32_t FNameEntryId;                          // 0x0048 (FNameEntryId)
        int32_t InstanceNumber;                        // 0x004C
        uintptr_t Class;                               // 0x0050 (UClass*)
        uintptr_t ObjectArchetype;                     // 0x0058 (UObject*)
        // Ensure this struct's size matches or is less than the actual UObject size
    };

    // Reset to beginning of GObjects array for a second pass looking specifically for functions
    for (int32_t i = 0; i < layout.ArrayCount; ++i) {
        /* // REMOVED Progress Logging
        if (i > 0 && i % logModulo == 0) { // Log progress periodically
            Logger::Info("GObjectsTable: Function scan index " + std::to_string(i) + "...");
        }
        */

        uintptr_t objPtrAddress = layout.ArrayData + i * pointerSize;
        auto objPtrOpt = pm.Read<uintptr_t>(objPtrAddress);

        // Skip null pointers in the array
        if (!objPtrOpt || *objPtrOpt == 0) {
            continue;
        }

        uintptr_t objAddress = *objPtrOpt;
        
        // Skip very low addresses that are likely invalid
        if (objAddress < 0x1000) {
            continue;
        }

        // Read the object directly using our simplified struct
        auto directObjOpt = pm.Read<DirectUObjectLayout>(objAddress);
        if (!directObjOpt) {
            directErrors++;
            if (directErrors < 20) Logger::Warning("GObjectsTable: Failed to read DirectUObjectLayout at " + Logger::to_hex(objAddress));
            continue;
        }
        
        DirectUObjectLayout uObject = *directObjOpt;
        
        // Skip objects that reference themselves (can cause infinite loops)
        if (uObject.Outer == objAddress || uObject.Class == objAddress) {
            continue;
        }

        // Get the object's own base name using the FName struct read directly
        // Use the FNameEntryId directly from the read struct
        std::string baseName = gnames.GetName(uObject.FNameEntryId);
        if (baseName.empty() || baseName == "None" || baseName[0] == '[') {
            // Skip if base name is invalid or error
            continue;
        }
        
        // Get the inner name (class name) (RocketDumper.cs::GetObjectName - innerName)
        std::string innerName = "";
        if (uObject.Class != 0) {
            // Read the FName struct from the class object
            // Read the FNameEntryId directly from the class object
            auto innerClassNameIdOpt = pm.Read<int32_t>(uObject.Class + SDK::UObject::Offset_Name);
            if (innerClassNameIdOpt) {
                innerName = gnames.GetName(*innerClassNameIdOpt);
                if (innerName.empty() || innerName == "None" || innerName[0] == '[') {
                    innerName = "UnknownClass"; // Fallback if name lookup fails
                }
            }
            else {
                innerName = "UnknownClass"; // Failed to read class object FName
                 directErrors++;
                 if (directErrors < 20) Logger::Warning("GObjectsTable: Failed to read Inner Class FName at " + Logger::to_hex(uObject.Class + SDK::UObject::Offset_Name));
            }
        }
        else {
             innerName = "UnknownClass"; // Class pointer is null
        }

        // Only process objects whose class name is exactly "Function"
        if (innerName != "Function") {
            continue;
        }
        
        // Build the outer name path (RocketDumper.cs::GetObjectName - outerName logic)
        std::string outerNamePath = "";
        uintptr_t currentOuterAddr = uObject.Outer;
        int outerDepth = 0;
        const int maxOuterDepth = 16; // Safety limit for nested outers

        while (currentOuterAddr != 0 && outerDepth < maxOuterDepth) {
             // Read the FName struct from the outer object
             // Read the FNameEntryId directly from the outer object
             auto outerNameIdOpt = pm.Read<int32_t>(currentOuterAddr + SDK::UObject::Offset_Name);
             // Read the next outer pointer
             auto nextOuterOpt = pm.Read<uintptr_t>(currentOuterAddr + SDK::UObject::Offset_Outer);

             if (!outerNameIdOpt || !nextOuterOpt) {
                 directErrors++;
                 if (directErrors < 20) Logger::Warning("GObjectsTable: Failed to read Outer FName/Ptr at " + Logger::to_hex(currentOuterAddr));
                 outerNamePath = "[OuterReadError]." + outerNamePath;
                 break;
             }
             uintptr_t nextOuterAddr = *nextOuterOpt;

             // Prevent self-referential loops in the Outer chain
             if (nextOuterAddr == currentOuterAddr) { // Check if Outer points back to the address we just read
                Logger::Warning("GObjectsTable: Detected self-loop in Outer chain at " + Logger::to_hex(currentOuterAddr));
                outerNamePath = "[SelfLoop]." + outerNamePath;
                break;
             }

             std::string currentOuterName = gnames.GetName(*outerNameIdOpt);
             if (currentOuterName.empty() || currentOuterName == "None" || currentOuterName[0] == '[') {
                 outerNamePath = "[OuterNameError]." + outerNamePath;
                 break; // Stop if outer name is invalid
             }

             outerNamePath = currentOuterName + "." + outerNamePath;
             currentOuterAddr = nextOuterAddr; // Move to the next outer
             outerDepth++;
        }
        if (outerDepth >= maxOuterDepth) {
            Logger::Warning("GObjectsTable: Outer chain depth exceeded max limit for object " + Logger::to_hex(objAddress));
            outerNamePath = "[MaxDepth]." + outerNamePath;
        }

        // Format exactly like RocketDumper: "{innerName} {outerNamePath}{baseName}"
        // Note: outerNamePath already includes the trailing dot if it's not empty
        std::string functionFullName = innerName + " " + outerNamePath + baseName;
        
        // Final validation and storage
        if (!functionFullName.empty() && 
            functionFullName.find("None") == std::string::npos &&
            functionFullName.find("[") == std::string::npos && // Check for error markers
            staticFunctions_.find(functionFullName) == staticFunctions_.end()) { // Avoid duplicates
            
            // Store in our map using the UFunction wrapper
            staticFunctions_[functionFullName] = SDK::UFunction(objAddress);
            directMappedFunctions++;
            
            // Log first 20 functions or every 1000th - COMMENTED OUT
            /*
            if (directMappedFunctions < 20 || directMappedFunctions % 1000 == 0) {
                Logger::Info("Found Function [" + std::to_string(directMappedFunctions) + "]: " + functionFullName);
            }
            */
        }
        // else: skip invalid names, errors, or duplicates
    }

    Logger::Info("GObjectsTable: Direct function scan complete. Found " + std::to_string(directMappedFunctions) + 
                " functions. Errors during scan: " + std::to_string(directErrors));
    
    // Update total count (mappedFunctions was from the first pass, which might be zero or very few)
    // It might be clearer to just use the count from the second pass if that's the primary method now.
    // Keep the count from the first loop, as the second loop failed to find functions.
    // If the second loop were successful, you might use: mappedFunctions += directMappedFunctions;
    Logger::Info("GObjectsTable: Final mapped - " + std::to_string(mappedClasses) + " classes, " + 
                std::to_string(mappedFunctions) + " functions.");

    initialized_ = true; // Mark initialized even if nothing was mapped, as process completed
    return true;
}

SDK::UClass GObjectsTable::FindStaticClass(const std::string& fullName) const {
    if (!initialized_) {
        return SDK::UClass(0); // Return invalid UClass
    }
    auto it = staticClasses_.find(fullName);
    if (it != staticClasses_.end()) {
        return it->second; // Return the stored UClass wrapper
    }
    return SDK::UClass(0); // Not found, return invalid UClass
}

SDK::UFunction GObjectsTable::FindStaticFunction(const std::string& fullName) const {
    if (!initialized_) {
        return SDK::UFunction(0); // Return invalid UFunction
    }
    auto it = staticFunctions_.find(fullName);
    if (it != staticFunctions_.end()) {
        return it->second; // Return the stored UFunction wrapper
    }
    return SDK::UFunction(0); // Not found, return invalid UFunction
}

SDK::UObject GObjectsTable::GetObjectByIndex(const MemoryManager& pm, int32_t index) const {
    if (!initialized_ || gobjectsArrayAddress_ == 0) {
        return SDK::UObject(0);
    }

    // Read layout fresh each time for simplicity, could cache if needed
    auto layoutOpt = pm.Read<SDK::TArrayLayout>(gobjectsArrayAddress_);
    if (!layoutOpt) return SDK::UObject(0);
    SDK::TArrayLayout layout = *layoutOpt;

    if (index < 0 || index >= layout.ArrayCount || layout.ArrayData == 0) {
        return SDK::UObject(0); // Index out of bounds
    }

    constexpr size_t pointerSize = sizeof(uintptr_t);
    uintptr_t objPtrAddress = layout.ArrayData + index * pointerSize;
    auto objPtrOpt = pm.Read<uintptr_t>(objPtrAddress);

    if (!objPtrOpt || *objPtrOpt == 0) {
        return SDK::UObject(0); // Pointer is null or read failed
    }

    return SDK::UObject(*objPtrOpt); // Return wrapper for the object
}

int32_t GObjectsTable::GetObjectCount(const MemoryManager& pm) const {
    if (!initialized_ || gobjectsArrayAddress_ == 0) {
        return 0;
    }
    // Read count directly from the layout
    auto layoutOpt = pm.Read<SDK::TArrayLayout>(gobjectsArrayAddress_);
    if (!layoutOpt) return 0;
    return layoutOpt->ArrayCount;
}


bool GObjectsTable::IsInitialized() const {
    return initialized_;
}

size_t GObjectsTable::GetMappedClassCount() const {
    return staticClasses_.size();
}

size_t GObjectsTable::GetMappedFunctionCount() const {
    return staticFunctions_.size();
}