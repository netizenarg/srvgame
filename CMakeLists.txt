cmake_minimum_required(VERSION 3.15)
project(GameServer3D)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)
find_package(asio REQUIRED)
find_package(nlohmann_json REQUIRED)

# Add GLM include directory
include_directories(${glm_INCLUDE_DIRS})

# Core logic system
set(LOGIC_CORE_SOURCES
    src/game/LogicCore.cpp
    src/game/LogicWorld.cpp
    src/game/LogicEntity.cpp
    src/game/GameLogic.cpp
)

# Loot system source files
set(LOOT_SOURCES
    src/game/LootItem.cpp
    src/game/InventorySystem.cpp
    src/game/LootTable.cpp
)

# 3D world system
set(WORLD_SOURCES
    src/game/WorldChunk.cpp
    src/game/WorldGenerator.cpp
    src/game/NPCSystem.cpp
    src/game/MobSystem.cpp
    src/game/GameEntity.cpp
    src/game/CollisionSystem.cpp
    src/game/EntityManager.cpp
    ${LOOT_SOURCES}
)

# Include directories
include_directories(
    include
    ${OPENGL_INCLUDE_DIR}
    ${glm_INCLUDE_DIRS}
)

# Main executable
add_executable(gameserver
    src/main.cpp
    ${LOGIC_CORE_SOURCES}
    ${WORLD_SOURCES}
)

# Link libraries
target_link_libraries(gameserver
    ${OPENGL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    asio::asio
    nlohmann_json::nlohmann_json
)

# Compiler flags
if(MSVC)
    target_compile_options(gameserver PRIVATE /W4)
else()
    target_compile_options(gameserver PRIVATE -Wall -Wextra -pedantic)
endif()

# Installation
install(TARGETS gameserver DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
install(DIRECTORY scripts/ DESTINATION scripts)
