cmake_minimum_required(VERSION 3.15)
project(GameServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fmt REQUIRED)

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

# Add GLM include directory
set(GLM_DIR "${CMAKE_SOURCE_DIR}/thirdparty/glm")
set(GLM_INCLUDE_DIRS "${GLM_DIR}")
include_directories(${glm_INCLUDE_DIRS})

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

# Options for database backends
option(USE_CITUS "Enable Citus distributed database support" OFF)
option(USE_SQLITE "Enable SQLite embedded database support" OFF)

# Find dependencies
find_package(OpenGL REQUIRED)
find_package(PostgreSQL REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Development)

if(USE_SQLITE)
    find_package(SQLite3 REQUIRED)
endif()

# Find zlib for compression
find_package(ZLIB REQUIRED)

# Find OpenSSL for encryption
find_package(OpenSSL REQUIRED)

# Find UUID library
find_library(UUID_LIBRARY NAMES uuid)
if(NOT UUID_LIBRARY)
    message(FATAL_ERROR "libuuid not found - install libuuid-dev or equivalent")
endif()

find_library(CRYPT_LIB crypt)

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
    src/scripting/PythonModule.cpp
    src/scripting/PythonScripting.cpp
    src/scripting/ScriptHotReloader.cpp
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
    src/network/WebSocketSession.cpp
)

# Loot system source files
set(LOOT_SOURCES
    src/game/LootItem.cpp
    src/game/InventorySystem.cpp
    src/game/LootTable.cpp
    src/game/LootTableManager.cpp
)

# 3D world system
set(WORLD_SOURCES
    src/game/WorldChunk.cpp
    src/game/WorldGenerator.cpp
    src/game/NPCEntity.cpp
    src/game/NPCSystem.cpp
    src/game/MobSystem.cpp
    src/game/GameEntity.cpp
    src/game/CollisionSystem.cpp
    src/game/LogicWorld.cpp
)

# Core logic system
set(LOGIC_CORE_SOURCES
    src/game/LogicCore.cpp
    src/game/LogicEntity.cpp
    src/game/GameLogic.cpp
    src/game/EntityManager.cpp
    src/game/SkillSystem.cpp
    src/game/Player.cpp
    src/game/PlayerManager.cpp
    src/game/QuestManager.cpp
)

# Database system
set(DATABASE_SOURCES
    src/database/DbManager.cpp
    src/database/PostgreSqlClient.cpp
)

if(USE_CITUS)
    message(STATUS "Building with Citus support")
    list(APPEND DATABASE_SOURCES src/database/CitusClient.cpp)
endif()

if(USE_SQLITE)
    message(STATUS "Building with SQLite support")
    list(APPEND DATABASE_SOURCES src/database/SQLiteClient.cpp)
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

option(ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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

if(ENABLE_ASAN)
    message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(gameserver PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -g
            -O1
        )
        target_link_options(gameserver PRIVATE
            -fsanitize=address,undefined
        )
    elseif(MSVC)
        target_compile_options(gameserver PRIVATE /fsanitize=address)
    endif()
endif()

# Add target-specific preprocessor definitions
if(USE_CITUS)
    target_compile_definitions(gameserver PRIVATE USE_CITUS=1)
endif()

if(USE_SQLITE)
    target_compile_definitions(gameserver PRIVATE USE_SQLITE=1)
endif()

add_definitions(-DUSE_POSTGRESQL=1)
add_definitions(-DUSE_SPDLOG=1)

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
    fmt::fmt
    spdlog::spdlog
    ${UUID_LIBRARY}
    ${CRYPT_LIB}
)

if(USE_SQLITE)
    target_link_libraries(gameserver PRIVATE SQLite::SQLite3)
endif()

# Compiler flags
if(MSVC)
    target_compile_options(gameserver PRIVATE /W4)
else()
    target_compile_options(gameserver PRIVATE -Wall -Wextra -pedantic)
endif()

target_link_options(gameserver PRIVATE -Wl,-Bsymbolic)

# Installation
install(TARGETS gameserver DESTINATION bin)
install(DIRECTORY config/ DESTINATION config)
install(DIRECTORY scripts/ DESTINATION scripts)
