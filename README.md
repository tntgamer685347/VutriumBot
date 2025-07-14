Of course. Here is the final `README.md` with the correct repository URL.

# Vutrium - Rocket League SDK & Bot Bridge

![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-0078D6.svg)
![Language](https://img.shields.io/badge/Language-C++-00599C.svg)

Vutrium is an internal C++ DLL designed to be injected into Rocket League. It provides a comprehensive in-game Software Development Kit (SDK), hooks core game functions, and serves as a high-performance bridge to an external Python bot client.

The primary purpose of this project is to read live game data, provide in-game visual overlays (ESP), and communicate this information to a botting client, which then sends back controller inputs.

## ⚠️ A Note on Code Quality & Hooking

This project was developed as a learning experience and is currently in a rough state. The code is not clean, contains many commented-out experimental sections, and needs significant refactoring.

**Crucially, the original individual function hooks (e.g., for boost pickups, round start/end) are NON-FUNCTIONAL.** The current implementation relies **entirely** on a single, powerful hook on `UObject::ProcessEvent`. All game event logic is derived from intercepting calls through this central function. Please be aware of this when navigating the codebase.

***

## Key Features

- **Full In-Game SDK:**
  - Runtime access to Unreal Engine's `GObjects` and `GNames` tables.
  - C++ object wrappers for core game entities like `AGameEvent`, `ACar`, `ABall`, `APRI`, etc., allowing for easy and safe access to game state.
  - Robust memory manager for reading and writing process memory.

- **Advanced Function Hooking:**
  - Utilizes MinHook (via Kiero) to safely hook critical game functions.
  - Hooks the core `ProcessEvent` function to intercept all Unreal Engine events, enabling powerful and flexible event-driven logic.

- **In-Game GUI and Visuals:**
  - Integrated ImGui menu for real-time configuration and status monitoring, rendered using Kiero for DirectX 11.
  - A suite of configurable visual aids (ESP), including:
    - 2D/3D Ball Indicator & Prediction Path
    - Car Hitboxes & Velocity Pointers
    - Opponent Boost Indicators & Team-Colored Tracers
    - Boost Pad Respawn Timers

- **High-Speed TCP Server:**
  - Runs a lightweight TCP server on `localhost` to communicate with an external bot client.
  - Sends game state and receives user settings in JSON format, creating a powerful bridge for out-of-process bots.

***

## ⚠️ Important: Python Bot Client Required

This repository contains **only the C++ DLL**, which acts as the SDK and the bridge to the game. The actual bot logic (e.g., Nexto, Genesis) resides in a **separate, closed-source Python client.**

**This project WILL NOT function as a bot on its own.** You must run the companion Python client for the bot functionality to work. This C++ project's role is to provide the necessary game data and control hooks for that client.

***

## Building the Project

### Prerequisites
- **Visual Studio 2019 or newer** (with C++ Desktop Development workload)
- **Windows 10/11 SDK**
- **[vcpkg](https://vcpkg.io/en/index.html)** package manager is recommended for dependencies.

### Dependencies
This project relies on the following libraries:
- **cURL:** For any potential web requests.
- **nlohmann/json:** For JSON serialization/deserialization with the bot client.
- **Kiero:** For DirectX hooking and ImGui rendering. (Included)
- **MinHook:** For function hooking. (Included with Kiero)
- **ImGui:** For the in-game GUI. (Included)

### Steps
1. **Clone the repository:**
   ```sh
   git clone https://github.com/tntgamer685347/VutriumBot.git
   cd VutriumBot
   ```
2. **Install Dependencies with vcpkg:**
   ```sh
   vcpkg install curl:x64-windows nlohmann-json:x64-windows
   ```
3. **Integrate vcpkg with Visual Studio:**
   ```sh
   vcpkg integrate install
   ```
4. **Open the Solution:** Open `Vutrium.sln` in Visual Studio.
5. **Set Build Configuration:** Select `Release` and `x64` from the dropdowns.
6. **Build:** Build the solution (Ctrl+Shift+B). The output `Vutrium.dll` will be in the `x64/Release` folder.

## Usage

1. **Launch Rocket League.**
2. **Inject the DLL:** Use a standard DLL injector to inject the compiled `Vutrium.dll` into the `RocketLeague.exe` process.
3. **Run the Bot Client:** Start the companion Python bot client. It will automatically connect to the TCP server hosted by the DLL.
4. **Open the Menu:** Press the `INSERT` key in-game to toggle the Vutrium menu. From here, you can configure visuals and bot settings.

## Author & Contact

This project was developed solely by **tntgamer0815**.

- **Discord:** `tntgamer0815`
- **Email:** `tntgamer0815@gmail.com`

## License

This project is licensed under the **GNU General Public License v3.0**. A copy of the license is included in the `LICENSE` file.

In short, this means:
- You are free to use, study, and modify this code.
- **Any project that uses or modifies this code (a derivative work) must also be released as open source under the same GPLv3 license.** This is a "copyleft" license designed to keep the software and its derivatives free and open.
