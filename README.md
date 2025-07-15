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

## ⚠️ **Important: Bot Client Required**

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

### Quick Start

1. **Launch Rocket League** and enter a match
2. **Inject the DLL** using your preferred DLL injector:
   ```bash
   # Example with a command-line injector
   injector.exe RocketLeague.exe Vutrium.dll
   ```
3. **Start your Python bot client** (connects automatically to `localhost:13337`)
4. **Access the menu** by pressing `INSERT` in-game

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

- **Memory footprint**: ~2MB injected DLL
- **CPU overhead**: <1% additional load
- **Network latency**: <1ms localhost communication
- **Frame rate impact**: Negligible when overlays disabled

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

- **Memory-safe operations** with bounds checking
- **No permanent game modifications** - injected code only
- **Graceful error handling** prevents game crashes
- **Clean unloading** via in-game eject button

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guidelines](CONTRIBUTING.md) for details.

### Development Setup

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/amazing-feature`
3. Make your changes and test thoroughly
4. Submit a pull request with a clear description

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
