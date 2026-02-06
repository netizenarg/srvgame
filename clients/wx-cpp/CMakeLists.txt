# CMakeLists.txt
find_package(WebSocketPP REQUIRED)

# Add to target_link_libraries
target_link_libraries(GameServerClient
    ${wxWidgets_LIBRARIES}
    ${OpenGL_LIBRARIES}
    ${Python3_LIBRARIES}
    asio::asio
    glm::glm
    nlohmann_json::nlohmann_json
    WebSocketPP::WebSocketPP
)

set(SOURCES
    src/main.cpp
    src/core/ClientApp.cpp
    src/core/ClientFrame.cpp
    src/core/GLCanvas.cpp
    src/core/GameWorld.cpp
    src/network/NetworkClient.cpp
    src/network/WebSocketClient.cpp
    src/scripting/PythonScriptManager.cpp
)
