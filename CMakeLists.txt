cmake_minimum_required(VERSION 3.15)
project(GameServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT TARGET asio::asio)
    message(STATUS "ASIO not found via find_package, using manual configuration")
    set(ASIO_DIR "${CMAKE_SOURCE_DIR}/thirdparty/asio")
    set(ASIO_INCLUDE_DIRS "${ASIO_DIR}/asio/include")
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${ASIO_INCLUDE_DIRS})
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
    add_library(asio::asio ALIAS asio)
else()
    message(STATUS "ASIO found via find_package")
endif()

set(GLM_DIR "${CMAKE_SOURCE_DIR}/thirdparty/glm")
set(GLM_INCLUDE_DIRS "${GLM_DIR}")

find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(WARNING "nlohmann_json not found, using FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.2
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(spdlog 1.13.0 QUIET)
if(NOT spdlog_FOUND)
    # Fall back to cloned version
    message(STATUS "System spdlog not found, using local clone")
    set(SPDLOG_DIR "${CMAKE_SOURCE_DIR}/thirdparty/spdlog")
    add_subdirectory(${SPDLOG_DIR} EXCLUDE_FROM_ALL)
endif()

# Option for Citus support
option(USE_CITUS "Enable Citus distributed database support" OFF)

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)
#find_package(glm REQUIRED)
#find_package(asio REQUIRED)
#find_package(nlohmann_json REQUIRED)

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
    src/network/WebSocketProtocol.cpp
)

# Core logic system
set(LOGIC_CORE_SOURCES
    src/game/LogicCore.cpp
    src/game/LogicWorld.cpp
    src/game/LogicEntity.cpp
    src/game/GameLogic.cpp
    src/game/EntityManager.cpp
    src/game/Player.cpp
    src/game/NPCEntity.cpp
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
)

# Database system
set(DATABASE_SOURCES
    src/database/DbManager.cpp
    src/database/PostgreSqlClient.cpp
)

if(USE_CITUS)
    message(STATUS "Building with Citus support")
    list(APPEND DATABASE_SOURCES src/database/CitusClient.cpp)
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
    ${LOOT_SOURCES}
    ${WORLD_SOURCES}
    ${DATABASE_SOURCES}
)

# Link libraries
target_link_libraries(gameserver PRIVATE
    ${OPENGL_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    ${PostgreSQL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    asio::asio
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Python3::Python
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
