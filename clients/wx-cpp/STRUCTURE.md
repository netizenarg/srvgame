wx-cpp/
├── - CMakeLists.txt
├── + INTERNAL_DOCS/
├── - STRUCTURE.md
├── - build.sh
├── + config/
│   ├── - input_config.json
│   └── - network_config.json
├── + include/
│   ├── + core/
│   │   ├── - Camera.hpp
│   │   ├── - ClientFrame.hpp
│   │   ├── - ConnectionState.hpp
│   │   ├── - EventDispatcher.hpp
│   │   ├── - GLCanvas.hpp
│   │   ├── - GameClient.hpp
│   │   ├── - InputEvents.hpp
│   │   ├── - InputManager.hpp
│   │   ├── - RenderSystem.hpp
│   │   └── - UIComponents.hpp
│   ├── + network/
│   │   ├── - NetworkClient.hpp
│   │   ├── - NetworkMonitor.hpp
│   │   └── - WebSocketClient.hpp
│   └── + scripting/
│       ├── - PythonScriptManager.hpp
│       └── - ScriptBindings.hpp
├── + resources/
│   ├── + config/
│   ├── + shaders/
│   └── + textures/
├── + scripts/
│   └── - game_scripts.py
└── + src/
    ├── + core/
    │   ├── - ClientApp.cpp
    │   ├── - ClientFrame.cpp
    │   ├── - ConnectionState.cpp
    │   ├── - EventDispatcher.cpp
    │   ├── - GLCanvas.cpp
    │   ├── - GameClient.cpp
    │   ├── - GameWorld.cpp
    │   └── - InputManager.cpp
    ├── - main.cpp
    ├── + network/
    │   ├── - NetworkClient.cpp
    │   └── - WebSocketClient.cpp
    └── + scripting/
        └── - PythonScriptManager.cpp
