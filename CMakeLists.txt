cmake_minimum_required(VERSION 3.15)
project(GameServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option for Citus support
option(USE_CITUS "Enable Citus distributed database support" OFF)

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(glm REQUIRED)
find_package(asio REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PostgreSQL REQUIRED)

# Find zlib for compression
find_package(ZLIB REQUIRED)

# Find OpenSSL for encryption
find_package(OpenSSL REQUIRED)

# Add GLM include directory
include_directories(${glm_INCLUDE_DIRS})

# Config system
set(CONFIG_SOURCES
    src/config/ConfigManager.cpp
)

# Logging system
set(LOGGING_SOURCES
    src/logging/Logger.cpp
)

# Process system
set(PROCESS_SOURCES
    src/process/ProcessPool.cpp
)

# Scripting system
set(SCRIPTING_SOURCES
    src/scripting/PythonAPI.cpp
    src/scripting/PythonScripting.cpp
)

# Network system
set(NETWORK_SOURCES
    src/network/BinaryProtocol.cpp
    src/network/ConnectionManager.cpp
    src/network/GameServer.cpp
    src/network/GameSession.cpp
    src/network/NetworkQualityMonitor.cpp
    src/network/PredictionSystem.cpp
)

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

# Database system
set(DATABASE_SOURCES
    src/database/Pool.cpp
    src/database/Backend.cpp
    src/database/PostgreSQLBackend.cpp
)

if(USE_CITUS)
    message(STATUS "Building with Citus support")
    list(APPEND DATABASE_SOURCES src/database/CitusClient.cpp)
    list(APPEND DATABASE_SOURCES src/database/CitusBackend.cpp)
    add_definitions(-DUSE_CITUS=1)
else()
    message(STATUS "Building without Citus (PostgreSQL only)")
    add_definitions(-DUSE_CITUS=0)
endif()

# Include directories
include_directories(
    include
    ${OPENGL_INCLUDE_DIR}
    ${glm_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# Main executable
add_executable(gameserver
    src/main.cpp
    ${CONFIG_SOURCES}
    ${LOGGING_SOURCES}
    ${PROCESS_SOURCES}
    ${SCRIPTING_SOURCES}
    ${NETWORK_SOURCES}
    ${LOGIC_CORE_SOURCES}
    ${WORLD_SOURCES}
    ${DATABASE_SOURCES}
)

# Link libraries
target_link_libraries(gameserver
    ${OPENGL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    asio::asio
    nlohmann_json::nlohmann_json
    ${PostgreSQL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
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
