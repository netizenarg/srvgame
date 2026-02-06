gameserver-client/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── ClientApp.h
│   │   ├── ClientApp.cpp
│   │   ├── ClientFrame.h
│   │   ├── ClientFrame.cpp
│   │   ├── GLCanvas.h
│   │   ├── GLCanvas.cpp
│   │   ├── GameWorld.h
│   │   └── GameWorld.cpp
│   ├── network/
│   │   ├── NetworkClient.h
│   │   └── NetworkClient.cpp
│   └── scripting/
│       ├── PythonScriptManager.h
│       └── PythonScriptManager.cpp
├── include/
│   ├── core/
│   │   ├── GameClient.h
│   │   ├── Player.h
│   │   ├── Camera.h
│   │   ├── InputManager.h
│   │   ├── RenderSystem.h
│   │   ├── UIComponents.h
│   │   └── ScriptBindings.h
│   ├── network/
│   │   ├── NetworkClient.h
│   │   └── NetworkClient.cpp
│   └── scripting/
│       ├── PythonWrapper.h
│       └── ScriptEvents.h
├── resources/
│   ├── shaders/
│   │   ├── basic.vert
│   │   └── basic.frag
│   ├── textures/
│   └── config/
│       └── client_config.json
└── scripts/
    ├── game_scripts.py
    ├── ui_scripts.py
    └── event_handlers.py
