# Game Server with Scripting

## Overview

GameServer is a sophisticated, game server designed for massively multiplayer online games. Built with a C++ core and extensible Python scripting, it provides solution for managing persistent game worlds with real-time player interactions, advanced networking, and comprehensive monitoring capabilities.

## Key Features

### 🎮 **Game Engine Capabilities**
- **Chunk-based World Management**: Procedurally generated terrain with biome support
- **Entity System**: Full lifecycle management for players, NPCs, and game objects
- **Physics & Collision**: Raycasting and collision detection for interactive environments
- **Loot & Inventory**: Complete inventory system with item management and trading
- **Combat System**: Damage calculation, health management, and combat events

### 🌐 **Advanced Networking**
- **Dual Protocol Support**: JSON for simplicity + Binary protocol for performance
- **Network Quality Adaptation**: Real-time monitoring and automatic optimization
- **Client-Side Prediction**: Smooth gameplay with server reconciliation
- **Multi-Process Architecture**: Master/worker model for horizontal scaling
- **SSL/TLS Support**: Secure encrypted connections

### ⚡ **Performance & Scalability**
- **Multi-Process Design**: Isolated worker processes for maximum CPU utilization
- **Async I/O**: Non-blocking network operations using ASIO
- **Message Compression**: On-the-fly data compression for bandwidth optimization
- **Connection Pooling**: Efficient resource management for thousands of concurrent players
- **Distributed Database**: Citus/PostgreSQL backend for horizontal scaling

### 🔧 **Development & Operations**
- **Comprehensive Logging**: Multi-level, async logging with rotation and compression
- **Advanced Debugging**: Performance profiling, memory leak detection, and metrics
- **Hot Reloading**: Runtime configuration updates without server restart
- **Process Monitoring**: Health checks and automatic worker restart
- **Python Scripting**: Extensible game logic through Python integration

## Architecture

### Core Components
┌─────────────────────────────────────────────────────────┐
│                    Master Process                       │
│    ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│    │ Worker #1   │  │ Worker #2   │  │ Worker #N   │    │
│    │ ┌─────────┐ │  │ ┌─────────┐ │  │ ┌─────────┐ │    │
│    │ │GameLogic│ │  │ │GameLogic│ │  │ │GameLogic│ │    │
│    │ │ World   │ │  │ │ World   │ │  │ │ World   │ │    │
│    │ │ Entities│ │  │ │ Entities│ │  │ │ Entities│ │    │
│    │ │ Network │ │  │ │ Network │ │  │ │ Network │ │    │
│    │ └─────────┘ │  │ └─────────┘ │  │ └─────────┘ │    │
│    └─────────────┘  └─────────────┘  └─────────────┘    │
└─────────────────────────────────────────────────────────┘
            │                │                │
            ▼                ▼                ▼
┌─────────────────────────────────────────────────────────┐
│              Distributed Database (Citus)               │
└─────────────────────────────────────────────────────────┘


### Network Protocol Stack
┌─────────────────────────────────────────────────────┐
│                                   Application Layer │
│ • Game Logic Messages • Chat • Inventory • Combat   │
├─────────────────────────────────────────────────────┤
│                               Binary Protocol Layer │
│ • Message Serialization • Compression • Encryption  │
├─────────────────────────────────────────────────────┤
│                           Transport Layer (TCP/SSL) │
│ • Reliable Delivery • Flow Control • Congestion     │
├─────────────────────────────────────────────────────┤
│                            Session Management Layer │
│ • Connection Pooling • Authentication • Rate Limit  │
└─────────────────────────────────────────────────────┘


## Technology Stack

### Core Technologies
- **C++17/20**: High-performance server core
- **Python 3.8+**: Game logic scripting and extensibility
- **ASIO**: Cross-platform asynchronous I/O
- **OpenSSL**: Secure communication layer
- **GLM**: Mathematics library for 3D operations
- **nlohmann/json**: JSON serialization/deserialization

### Database
- **PostgreSQL with Citus**: Distributed database backend
- **Connection Pooling**: Efficient database resource management
- **Sharding Support**: Horizontal scaling of game data

### Build System
- **CMake 3.16+**: Cross-platform build configuration
- **vcpkg**: C++ dependency management
- **Cross-platform**: Linux and macOS support

## Quick Start

### Prerequisites
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- Python 3.8 or higher
- PostgreSQL 12+ (with Citus extension for distributed setup)
- CMake 3.16+

### Installation
```
# Clone the repository
git clone https://github.com/usermicrodevices/gameserver.git
cd gameserver

# Install dependencies (Linux)
chmod +x install_dependencies_linux.sh
./install_dependencies_linux.sh

# Build the server
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Configure the server
cp ../config/server_config.example.json config/server_config.json
# Edit server_config.json with your database and world settings

# Run the server
./gameserver
```

### Configuration

Create ```config/server_config.json```:
```
{
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "process_count": 4,
    "io_threads": 2,
    "reuse_port": true
  },
  "database": {
    "backend": "postgresql",
    "host": "localhost",
    "port": 5432,
    "name": "gameserver",
    "user": "gameserver",
    "password": "secure_password",
    "worker_nodes": ["node1:5432", "node2:5432"]
  },
  "world": {
    "seed": 12345,
    "view_distance": 8,
    "chunk_size": 32,
    "max_active_chunks": 1000,
    "terrain_scale": 1.0,
    "max_terrain_height": 256.0,
    "water_level": 64.0,
    "preload_world": true,
    "world_preload_radius": 500.0
  },
  "logging": {
    "level": "info",
    "file": "logs/gameserver.log",
    "max_size": 104857600,
    "max_files": 10,
    "compress": true
  }
}
```

## Network Protocol

### Dual Protocol Architecture
The GameServer supports two communication protocols running in parallel:
Client ↔ Server Communication
├── JSON Protocol (Development)
│ ├── Human-readable format
│ ├── Easy debugging and testing
│ ├── Slower, higher bandwidth
│ └── Used for configuration, chat, admin commands
│
└── Binary Protocol (Production)
├── High-performance binary format
├── Minimal bandwidth usage
├── Fast serialization/deserialization
└── Used for real-time gameplay, entity updates, world data


### Protocol Features
**Compression & Encryption:**
- **LZ4 Compression** for large payloads (terrain, chunk data)
- **Selective Compression** based on message type and size
- **TLS/SSL Encryption** for secure connections
- **Flag-based Control** per-message compression/encryption

**Reliability & Ordering:**
- **Sequence Numbers** for message tracking
- **Acknowledgment System** for critical messages
- **Priority Queues** (High/Normal/Low priority)
- **Ordered Delivery** for state-dependent messages

**Network Adaptation:**
- **Automatic Quality Detection** (latency, packet loss, bandwidth)
- **Dynamic Compression** based on connection quality
- **MTU Optimization** for different network conditions
- **Update Rate Adjustment** for smooth gameplay

### Message Categories
**System Messages (1-99):**
- Heartbeat, protocol negotiation, authentication, error handling

**World Messages (100-199):**
- Chunk data, terrain height, biome information, world updates

**Entity Messages (200-299):**
- Entity spawn/update/despawn, batch updates, state synchronization

**Player Messages (300-399):**
- Position, velocity, rotation, state, server corrections

**NPC Messages (400-499):**
- NPC spawn/update/despawn, interactions, AI commands

**Combat Messages (500-599):**
- Combat events, damage calculation, health updates, status effects

**Inventory Messages (600-699):**
- Inventory updates, loot spawning, item pickup, equipment changes

**Chat Messages (700-799):**
- Text chat, system messages, notifications, announcements

**Custom Messages (1000+):**
- Game-specific events, mod support, plugin communications

### Protocol Flow
Connection Establishment:
- Client connects via TCP/SSL
- Protocol negotiation (JSON/Binary capabilities)
- Authentication and session creation
- World synchronization (chunk loading, entity states)

Gameplay Loop:
- Client sends input (position, actions)
- Server processes in game tick
- Server broadcasts updates to nearby players
- Client reconciles with server state

Network Optimization:
- Monitor connection quality
- Adjust compression/update rates
- Handle packet loss/reordering
- Maintain smooth gameplay experience


### Performance Characteristics
- **Low Latency**: Optimized for real-time interaction (<100ms target)
- **High Throughput**: Efficient binary format minimizes bandwidth
- **Scalable**: Protocol designed for thousands of concurrent players
- **Adaptive**: Adjusts to network conditions without client intervention
- **Reliable**: Critical messages guaranteed delivery with retransmission
