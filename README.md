# Vutrium - Rocket League SDK & Bot Bridge (Outdated)

![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)
![Language](https://img.shields.io/badge/Language-C++-00599C.svg)
![Build Status](https://img.shields.io/badge/Build-Stable-green.svg)

## 🚀 Overview

Vutrium is a comprehensive C++ DLL-based SDK designed for Rocket League that provides real-time game data access, visual overlays, and serves as a high-performance bridge to external Python bot clients. This project enables developers to create sophisticated bots and analysis tools by exposing Rocket League's internal game state through a clean, modern C++ API.

## ⚠️ **Important Disclaimers**

### **Bot Client Required**
**This repository contains ONLY the C++ DLL component.** Vutrium acts as a bridge and data provider, but requires a separate Python bot client to function as an actual bot.

**This repository wont be updated anymore.**

**What this project provides:**
- ✅ Real-time game data extraction *(100% accurate when GameEvent found)*
- ✅ Visual overlays and ESP features *(all work except boost pad timers)*
- ✅ TCP communication bridge *(mostly functional)*
- ✅ Memory management and safety *(solid implementation)*
- ❌ Boost pad tracking *(completely broken system)*

**What you need separately:**
- ❌ Bot logic and decision making (Python client)
- ❌ Controller input simulation
- ❌ Machine learning models

### **🚨 Current State & Code Quality Warning**

**This codebase is currently in a rough state and should be considered experimental:**

- 🔴 **Code Quality**: The code is admittedly messy, poorly organized, and lacks proper documentation
- 🔴 **Hook Reliability**: Many of the function hooks are unstable and don't work consistently
- 🔴 **Memory Management**: While functional, the memory reading implementation needs significant cleanup
- 🔴 **Error Handling**: Insufficient error handling in many critical sections
- 🔴 **Architecture**: The overall architecture could be significantly improved

**Known Issues:**
- **Primary Issue**: Function hooks may fail to initialize properly, preventing GameEvent detection
- **When GameEvent is found**: All core game state reading works perfectly (ball, cars, match data)
- **Boost pad tracking**: Completely non-functional - the tracking system is broken
- **Boost pad timers**: Don't work because the underlying tracking system is broken
- **TCP bridge**: Can be unreliable under certain conditions
- **Pattern scanning**: May fail on newer game versions requiring offset updates
- **Hook reliability**: Main barrier to functionality - when hooks work, most features work

**This project is shared primarily for:**
- 📚 **Educational purposes** - Learning about game hacking techniques
- 🔧 **Reference implementation** - Base for building better tools
- 🤝 **Community contribution** - Hoping others can improve upon it

**If you're looking for a production-ready solution, this isn't it (yet).** Consider this a starting point that needs significant work to be reliable.

## ⚡ Key Features *(When They Work)*

### 🎯 **Game State Access** *(Reliable When GameEvent Found)*
- **Real-time Ball Data**: Position, velocity, angular velocity *(100% functional)*
- **Car Information**: Player positions, rotations, velocities, boost amounts *(100% functional)*
- **Match State**: Game time, score, team information *(100% functional)*
- **Boost Pad Tracking**: Live monitoring with respawn timers *(completely broken)*

### 🔧 **Memory Management** *(Solid When Initialized)*
- **Unreal Engine Integration**: Access to `GObjects` and `GNames` tables *(reliable when pattern scanning works)*
- **Type-Safe Wrappers**: C++ object wrappers *(functional, but code is messy)*
- **Memory Reading**: Game state access *(100% accurate when GameEvent is found)*
- **Pattern Scanning**: Automatic offset resolution *(works but may fail on major updates)*

### 🎨 **Visual Overlays** *(Mostly Functional)*
- **Ball Prediction**: 3D trajectory visualization *(works well)*
- **Car Hitboxes**: Hitbox rendering *(functional)*
- **Velocity Indicators**: Movement arrows *(working)*
- **Boost Visualization**: Boost indicators above cars *(working)*
- **Team-Colored Elements**: Team color detection *(functional)*
- **Boost Pad Timers**: Countdown displays *(completely broken - don't use)*

### 🌐 **Networking** *(Sometimes Works)*
- **TCP Bridge**: Localhost server *(connection drops frequently)*
- **JSON Protocol**: Data exchange *(formatting inconsistencies)*
- **Real-time Updates**: Low latency *(when it doesn't crash)*
- **Settings**: Runtime adjustment *(many settings don't actually work)*

### 🔩 **Function Hooking** *(Major Issues)*
- **MinHook Integration**: Function interception *(many hooks fail to install)*
- **ProcessEvent Hooking**: Event system integration *(very unreliable)*
- **DirectX 11 Rendering**: ImGui overlays *(often crashes)*
- **Event System**: Pub/sub architecture *(events may not fire)*

## 🏗️ Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Python Bot   │◄──►│   Vutrium DLL    │◄──►│  Rocket League  │
│     Client      │    │    (TCP Bridge)  │    │   (Injected)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
        │                        │                        │
        │              ┌─────────▼─────────┐             │
        │              │  Memory Manager   │             │
        │              │  GNames/GObjects  │             │
        │              │  Function Hooks   │             │
        │              └───────────────────┘             │
        │                                                │
        └──── JSON Data Exchange ──────────────────────────┘
```

## 🛠️ Building the Project

### Prerequisites

- **Visual Studio 2019 or newer** with C++ Desktop Development workload
- **Windows 10/11 SDK**
- **[vcpkg](https://vcpkg.io/)** package manager (recommended)

### Dependencies

```bash
# Install required packages via vcpkg (STATIC linking required for DLL injection)
vcpkg install curl:x64-windows-static
vcpkg install nlohmann-json:x64-windows-static
```

**Included Dependencies:**
- **Kiero** - DirectX hooking framework
- **MinHook** - Function hooking library  
- **ImGui** - Immediate mode GUI library

### Build Steps

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/vutrium.git
   cd vutrium
   ```

2. **Setup vcpkg integration:**
   ```bash
   vcpkg integrate install
   ```
   
   **Note**: If you have multiple vcpkg installations or toolchains, you may need to:
   ```bash
   # Remove existing integration first
   vcpkg integrate remove
   
   # Then re-integrate from your specific vcpkg directory
   cd C:\your\vcpkg\directory
   vcpkg integrate install
   ```

3. **⚠️ CRITICAL: Manual vcpkg Path Configuration**
   
   **You MUST manually edit the project file to point to your vcpkg static installation:**
   
   - Open `Vutrium.vcxproj` in a text editor
   - Find the line containing `<VcpkgTriplet>` 
   - Change it to: `<VcpkgTriplet>x64-windows-static</VcpkgTriplet>`
   - Find any `<AdditionalLibraryDirectories>` sections
   - Update the vcpkg paths to point to your actual vcpkg installation directory
   - Example path format: `C:\vcpkg\installed\x64-windows-static\lib`
   - **Also check Runtime Library setting**: Project Properties → C/C++ → Code Generation → Runtime Library
   - **Should be set to**: `Multi-threaded (/MT)` for Release or `Multi-threaded Debug (/MTd)` for Debug
   
   **If the project file doesn't have these entries, you may need to add them manually in the PropertySheets or project settings.**

4. **Build the project:**
   ```bash
   # Open Vutrium.sln in Visual Studio
   # Select Release + x64 configuration  
   # Verify that vcpkg static libraries are being linked (check Project Properties > Linker > Input)
   # Build Solution (Ctrl+Shift+B)
   ```

5. **Output:** The compiled `Vutrium.dll` will be in `x64/Release/`

6. **⚠️ Verify Static Linking (IMPORTANT):**
   ```bash
   # Use Dependency Walker, dumpbin, or similar tool to check dependencies
   dumpbin /dependents x64\Release\Vutrium.dll
   
   # Should ONLY show system DLLs like:
   # - KERNEL32.dll
   # - USER32.dll  
   # - ADVAPI32.dll
   # - etc.
   
   # Should NOT show:
   # - vcruntime140.dll (if using /MT)
   # - msvcp140.dll (if using /MT)
   # - Any curl or json DLLs
   ```
   
   **If you see extra dependencies, your static linking failed and injection will not work.**

### 🔧 **Why Static Linking?**

Static linking is **essential** for DLL injection projects because:
- **No external dependencies** - The injected DLL won't require additional runtime libraries
- **Compatibility** - Avoids conflicts with the target process's existing libraries  
- **Reliability** - Reduces the chance of missing DLL errors during injection
- **Self-contained** - Everything needed is embedded in the single DLL file

**If you use dynamic linking, the injection will likely fail with missing DLL errors.**

### 🚨 **Common Build Issues & Solutions**

**Problem: "Cannot find vcpkg libraries"**
```
Solution: Verify your vcpkg paths in the .vcxproj file match your actual installation
- Check: Tools → Options → Projects and Solutions → Debugging → Path variables
- Ensure VCPKG_ROOT environment variable is set correctly
```

**Problem: "LNK2019: unresolved external symbol" errors**
```
Solution: 
1. Make sure you're using x64-windows-static triplet
2. Check Project Properties → Linker → Input → Additional Dependencies
3. Verify the .lib files exist in: [vcpkg_root]\installed\x64-windows-static\lib\
```

**Problem: "The application was unable to start correctly (0xc000007b)"**
```
Solution: You're probably linking dynamically instead of statically
- Double-check the vcpkg triplet is set to x64-windows-static
- Rebuild completely after changing triplet
```

**Problem: DLL injection fails with "module not found" errors**
```
Solution: 
- Use Dependency Walker or similar to check for missing DLLs
- Ensure ALL dependencies are statically linked
- Consider using MT (Multi-threaded) runtime instead of MD (Multi-threaded DLL)
```

## 🚀 Usage

### Quick Start (Your Mileage May Vary)

1. **Launch Rocket League** and enter a match
2. **Inject the DLL** using your preferred DLL injector:
   ```bash
   # Example with a command-line injector
   injector.exe RocketLeague.exe Vutrium.dll
   ```
   **⚠️ Warning**: Injection may fail or cause crashes depending on game state
3. **Start your Python bot client** (connects automatically to `localhost:13337`)
   - *If TCP bridge doesn't work, check the console for errors*
4. **Access the menu** by pressing `INSERT` in-game
   - *Menu may not appear if hooks failed to initialize*

**Troubleshooting Common Issues:**
- **If nothing happens after injection** → Check console output for hook initialization status
- **If game crashes** → Try injecting at different times (main menu vs in-game vs mid-match)
- **If overlays don't show** → DirectX hook failed, try different injection timing
- **If TCP connection fails** → Check Windows Firewall and antivirus
- **If GameEvent detection works** → Most features should work properly (except boost pads)
- **If you see "GameEvent Found" in console** → Core functionality should be 100% operational

### Configuration

The in-game menu provides access to:
- **Visual settings** - Toggle ESP features and adjust colors
- **Bot parameters** - Configure bot behavior and responsiveness  
- **Connection status** - Monitor TCP bridge health
- **Debug information** - Real-time game state display

### Python Client Integration

```python
import socket
import json

# Connect to Vutrium bridge
client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
client.connect(('localhost', 13337))

# Receive game state
while True:
    data = client.recv(4096).decode('utf-8')
    game_state = json.loads(data)
    
    # Your bot logic here
    bot_decision = analyze_game_state(game_state)
    
    # Send response (if needed)
    response = json.dumps(bot_decision)
    client.send(response.encode('utf-8'))
```

## 📊 Performance

**Current performance characteristics (when it works):**
- **Memory footprint**: ~2MB injected DLL
- **CPU overhead**: <1% additional load (varies significantly)
- **Network latency**: <1ms localhost communication (when stable)
- **Frame rate impact**: Negligible when overlays disabled, noticeable when enabled

**⚠️ Note**: Performance can be inconsistent due to the current implementation issues. Expect potential stutters, crashes, and memory leaks during extended use.

## 🔍 Supported Game Versions

Vutrium uses dynamic pattern scanning to remain compatible across updates:

- ✅ **Steam version** - Fully supported
- ✅ **Epic Games version** - Fully supported  
- ✅ **Automatic offset resolution** - No manual updates needed
- ⚠️ **Major game updates** - May require pattern updates

## 📋 API Reference

### Core Classes

```cpp
// Main SDK interface
class RLSDK {
    SDK::AGameEvent GetCurrentGameEvent();
    MemoryManager& GetMemoryManager();
    void Subscribe(const std::string& event, Callback callback);
};

// Game entities
class AGameEvent {
    std::vector<ABall> GetBalls();
    std::vector<ACar> GetCars();
    std::vector<APRI> GetPRIs();
    bool IsRoundActive();
};

class ACar : public AVehicle {
    FVectorData GetLocation();
    FVectorData GetVelocity();
    APRI GetPRI();
    UBoostComponent GetBoostComponent();
};
```

### Event System

```cpp
// Subscribe to game events
sdk.Subscribe(EventType::OnRoundActiveStateChanged, [](const EventData& data) {
    // Handle round state changes
});

sdk.Subscribe(EventType::OnBoostPadStateChanged, [](const EventData& data) {
    // Handle boost pad pickups
});
```

## 🛡️ Safety & Detection

**Intended safety features (implementation varies):**
- **Memory-safe operations** with bounds checking *(needs improvement)*
- **No permanent game modifications** - injected code only *(when it works)*
- **Graceful error handling** prevents game crashes *(often fails)*
- **Clean unloading** via in-game eject button *(sometimes crashes)*

**⚠️ Reality Check**: The current implementation may cause game instability, crashes, or other issues. Use at your own risk and save your game progress frequently. The "safety" features are aspirational rather than guaranteed.

## 🤝 Contributing

**We desperately need help!** This codebase has significant issues that need addressing:

### **Priority Areas for Improvement:**
1. **🔧 Fix Function Hook Reliability** - The main blocker preventing GameEvent detection
2. **🎯 Fix Boost Pad Tracking System** - Completely broken, needs rewrite
3. **🧹 Code Cleanup** - Refactor messy sections and improve organization  
4. **🛡️ TCP Bridge Stability** - Improve connection reliability
5. **📚 Documentation** - Add proper comments and API documentation
6. **🏗️ Architecture** - Redesign core systems for better maintainability

**Good News**: Once GameEvent detection works, most core functionality is solid! The main issues are:
- Getting the hooks to work reliably (biggest barrier)
- The boost pad tracking system being completely broken
- General code organization and stability

### **How to Contribute:**
1. Fork the repository
2. Pick an issue from the **Priority Areas** above
3. Create a feature branch: `git checkout -b fix/hook-reliability`
4. **Test thoroughly** - The code is fragile, so extensive testing is crucial
5. Submit a pull request with detailed explanation of changes

**Even small improvements are welcome!** Whether it's fixing a memory leak, improving error messages, or adding comments - every bit helps make this project more usable.

### **For New Contributors:**
- Don't be intimidated by the messy code - we know it's bad!
- Focus on one small area at a time
- Ask questions in issues if you're unsure about anything
- Consider this a learning opportunity in "how not to structure a project" 😅

## 📄 License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

### License Summary

- ✅ **Commercial use** - You can use this code in commercial projects
- ✅ **Modification** - You can modify and adapt the code
- ✅ **Distribution** - You can distribute original or modified versions
- ✅ **Private use** - You can use the code privately

**However:**
- 📋 **Copyleft** - Any derivative work MUST also be licensed under GPL v3
- 📋 **Source disclosure** - You must provide source code for any distributed versions
- 📋 **License notice** - You must include the license and copyright notice
- 📋 **State changes** - You must document any changes made to the code

This ensures that improvements to the codebase remain available to the community.

## ⚖️ Legal & Ethical Use

- **Educational purposes** - Learn about game internals and memory management
- **Analysis tools** - Create statistics and replay analysis systems
- **Accessibility** - Develop tools for players with disabilities
- **Research** - Academic research into game AI and human-computer interaction

**Please note:** Always respect the game's Terms of Service and use responsibly.

## 🙏 Acknowledgments

- **Epic Games / Psyonix** - For creating Rocket League
- **Kiero Team** - DirectX hooking framework
- **MinHook Project** - Function hooking library
- **Dear ImGui** - Immediate mode GUI
- **Community Contributors** - For testing and feedback

## 📞 Support & Contact

- **Author**: tntgamer0815
- **Discord**: `tntgamer0815`
- **Email**: `tntgamer0815@gmail.com`
- **Issues**: [GitHub Issues](https://github.com/yourusername/vutrium/issues)

---

<div align="center">

**⚡ Built with bad performance and broken code in mind ⚡**

</div>
