# Vutrium - Rocket League SDK & Bot Bridge

![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)
![Language](https://img.shields.io/badge/Language-C++-00599C.svg)
![Build Status](https://img.shields.io/badge/Build-Stable-green.svg)

## 🚀 Overview

Vutrium is a comprehensive C++ DLL-based SDK designed for Rocket League that provides real-time game data access, visual overlays, and serves as a high-performance bridge to external Python bot clients. This project enables developers to create sophisticated bots and analysis tools by exposing Rocket League's internal game state through a clean, modern C++ API.

## ⚡ Key Features

### 🎯 **Complete Game State Access**
- **Real-time Ball Data**: Position, velocity, angular velocity, and physics state
- **Car Information**: All players' positions, rotations, velocities, boost amounts, and input states
- **Match State**: Game time, score, team information, and round status
- **Boost Pad Tracking**: Live monitoring of boost pad states with respawn timers

### 🔧 **Advanced Memory Management**
- **Unreal Engine Integration**: Direct access to `GObjects` and `GNames` tables
- **Type-Safe Wrappers**: C++ object wrappers for game entities (`AGameEvent`, `ACar`, `ABall`, `APRI`)
- **Robust Memory Reading**: Safe memory access with automatic error handling
- **Pattern Scanning**: Automatic offset resolution across game updates

### 🎨 **Visual Overlays (ESP)**
- **Ball Prediction**: 3D trajectory visualization with collision detection
- **Car Hitboxes**: Real-time hitbox rendering for all vehicles
- **Velocity Indicators**: 3D arrows showing movement direction and speed
- **Boost Visualization**: Circular indicators showing boost amounts above cars
- **Team-Colored Elements**: Automatic team color detection for enhanced visibility
- **Boost Pad Timers**: Countdown displays for boost pad respawn times

### 🌐 **High-Performance Networking**
- **TCP Bridge**: Lightweight localhost server for bot communication
- **JSON Protocol**: Structured data exchange with external clients
- **Real-time Updates**: Sub-millisecond latency for competitive applications
- **Configurable Settings**: Runtime adjustment of bot parameters

### 🔩 **Advanced Function Hooking**
- **MinHook Integration**: Safe and reliable function interception
- **ProcessEvent Hooking**: Comprehensive event system integration
- **DirectX 11 Rendering**: ImGui-based overlay system using Kiero
- **Event-Driven Architecture**: Pub/sub system for extensible functionality

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

## ⚠️ **Important Disclaimers**

### **Bot Client Required**
**This repository contains ONLY the C++ DLL component.** Vutrium acts as a bridge and data provider, but requires a separate Python bot client to function as an actual bot.

**What this project provides:**
- ✅ Real-time game data extraction
- ✅ Visual overlays and ESP features  
- ✅ TCP communication bridge
- ✅ Memory management and safety

**What you need separately:**
- ❌ Bot logic and decision making (Python client)
- ❌ Controller input simulation
- ❌ Machine learning models

### **🚨 Current State & Code Quality Warning**

**This codebase is currently in a rough state and should be considered experimental:**

- 🔴 **Code Quality**: The code is admittedly messy, poorly organized, and lacks proper documentation
- 🔴 **Hook Reliability**: Many of the function hooks are unstable and don't work consistently or dont work at all
- 🔴 **Memory Management**: While functional, the memory reading implementation needs significant cleanup
- 🔴 **Error Handling**: Insufficient error handling in many critical sections
- 🔴 **Architecture**: The overall architecture could be significantly improved

**Known Issues:**
- Function hooks may fail to initialize properly
- Some ESP features may not render correctly
- TCP bridge can be unreliable under certain conditions
- Memory access patterns may cause crashes in some scenarios
- Pattern scanning may fail on newer game versions

**This project is shared primarily for:**
- 📚 **Educational purposes** - Learning about game hacking techniques
- 🔧 **Reference implementation** - Base for building better tools
- 🤝 **Community contribution** - Hoping others can improve upon it

**If you're looking for a production-ready solution, this isn't it (yet).** Consider this a starting point that needs significant work to be reliable.

## 🛠️ Building the Project

### Prerequisites

- **Visual Studio 2019 or newer** with C++ Desktop Development workload
- **Windows 10/11 SDK**
- **[vcpkg](https://vcpkg.io/)** package manager (recommended)

### Dependencies

```bash
# Install required packages via vcpkg
vcpkg install curl:x64-windows
vcpkg install nlohmann-json:x64-windows
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

3. **Build the project:**
   ```bash
   # Open Vutrium.sln in Visual Studio
   # Select Release + x64 configuration
   # Build Solution (Ctrl+Shift+B)
   ```

4. **Output:** The compiled `Vutrium.dll` will be in `x64/Release/`

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
- If nothing happens after injection → Check if hooks initialized (console output)
- If game crashes → Try injecting at a different time (main menu vs in-game)
- If overlays don't show → DirectX hook probably failed
- If TCP connection fails → Check Windows Firewall and antivirus

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
1. **🔧 Fix Function Hooks** - Many hooks are broken or unreliable
2. **🧹 Code Cleanup** - Refactor messy sections and improve organization  
3. **🛡️ Memory Safety** - Better error handling and bounds checking
4. **📚 Documentation** - Add proper comments and API documentation
5. **🏗️ Architecture** - Redesign core systems for better maintainability
6. **🐛 Bug Fixes** - Address crashes and stability issues

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

**⚡ Built with performance and reliability in mind ⚡**

</div>
