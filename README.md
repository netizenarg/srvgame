# Game Server with Scripting

![image](https://private-user-images.githubusercontent.com/29286243/565547709-32033803-16ea-421b-b2f0-f5fa27ba8107.gif?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NzM4Mzg4OTMsIm5iZiI6MTc3MzgzODU5MywicGF0aCI6Ii8yOTI4NjI0My81NjU1NDc3MDktMzIwMzM4MDMtMTZlYS00MjFiLWIyZjAtZjVmYTI3YmE4MTA3LmdpZj9YLUFtei1BbGdvcml0aG09QVdTNC1ITUFDLVNIQTI1NiZYLUFtei1DcmVkZW50aWFsPUFLSUFWQ09EWUxTQTUzUFFLNFpBJTJGMjAyNjAzMTglMkZ1cy1lYXN0LTElMkZzMyUyRmF3czRfcmVxdWVzdCZYLUFtei1EYXRlPTIwMjYwMzE4VDEyNTYzM1omWC1BbXotRXhwaXJlcz0zMDAmWC1BbXotU2lnbmF0dXJlPWMwNjYzMTNlMzBlMzlhNzMxNzU2OGYyZGNkOWEzOWJlZDM2NDM3ZjY5MGM5ZmQ0ODViZTI0Zjk2MjViZGRmMjgmWC1BbXotU2lnbmVkSGVhZGVycz1ob3N0In0.JVSx-xKXSiATX3FEo1Cb0zj-7KNe44Cbqbzj75wR40M "test client small screen moving")
![image](https://private-user-images.githubusercontent.com/29286243/565547944-293b2ab0-f902-433e-80db-a9c57e410e17.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NzM4Mzg4OTMsIm5iZiI6MTc3MzgzODU5MywicGF0aCI6Ii8yOTI4NjI0My81NjU1NDc5NDQtMjkzYjJhYjAtZjkwMi00MzNlLTgwZGItYTljNTdlNDEwZTE3LnBuZz9YLUFtei1BbGdvcml0aG09QVdTNC1ITUFDLVNIQTI1NiZYLUFtei1DcmVkZW50aWFsPUFLSUFWQ09EWUxTQTUzUFFLNFpBJTJGMjAyNjAzMTglMkZ1cy1lYXN0LTElMkZzMyUyRmF3czRfcmVxdWVzdCZYLUFtei1EYXRlPTIwMjYwMzE4VDEyNTYzM1omWC1BbXotRXhwaXJlcz0zMDAmWC1BbXotU2lnbmF0dXJlPWM0ZTk5ZGI2OTIyMDlkNWYzZTMyMjU0M2YwODBiNjZkYjNiNmE4ODk4NGQ2ZDQzZjAzNmE1ZjRiOWI3N2Y4MGMmWC1BbXotU2lnbmVkSGVhZGVycz1ob3N0In0.V2LLYNdBIm1NjZSwfD0WbmTeYwxEX38xqGK1HXLk-qI "test client screenshot night")
![image](https://private-user-images.githubusercontent.com/29286243/565547953-1bba216f-a353-4f64-8ab2-5e669fc19c71.png?jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3NzM4Mzg4OTMsIm5iZiI6MTc3MzgzODU5MywicGF0aCI6Ii8yOTI4NjI0My81NjU1NDc5NTMtMWJiYTIxNmYtYTM1My00ZjY0LThhYjItNWU2NjlmYzE5YzcxLnBuZz9YLUFtei1BbGdvcml0aG09QVdTNC1ITUFDLVNIQTI1NiZYLUFtei1DcmVkZW50aWFsPUFLSUFWQ09EWUxTQTUzUFFLNFpBJTJGMjAyNjAzMTglMkZ1cy1lYXN0LTElMkZzMyUyRmF3czRfcmVxdWVzdCZYLUFtei1EYXRlPTIwMjYwMzE4VDEyNTYzM1omWC1BbXotRXhwaXJlcz0zMDAmWC1BbXotU2lnbmF0dXJlPTE5M2FhZWExMDVlOTYzZjFiZjM2YjQ4N2QwZmY4ZTVlNjQ3YWQyMjhmY2I1ZTE0MjRjOTczMWIyYTQ2MmMzNDMmWC1BbXotU2lnbmVkSGVhZGVycz1ob3N0In0.994xWIJfmpMn9RL6fcYFT95L4wVnLQ9NAdT3VzKLvQc "test client screenshot day")

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

## Technology Stack

### Core Technologies
- **C++17/20**: High-performance server core
- **Python 3.8+**: Game logic scripting and extensibility
- **ASIO**: Cross-platform asynchronous I/O
- **OpenSSL**: Secure communication layer
- **GLM**: Mathematics library for 3D operations
- **nlohmann/json**: JSON serialization/deserialization

### Database
- **SQLite**: mainly for fast-testing
- **PostgreSQL with Citus**: Distributed database backend
- **Connection Pooling**: Efficient database resource management
- **Sharding Support**: Horizontal scaling of game data

### Build System
- **CMake 3.16+**: Cross-platform build configuration

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

# Build the server
./build.sh --with-sqlite --with-asan

# Run the server
./gameserver
```

## Network Protocol

### Dual Protocol Architecture
The GameServer supports two communication protocols running in parallel:
Client ↔ Server Communication
├── JSON Protocol (Development)
│ ├── Human-readable format
│ ├── Easy debugging and testing
│ ├── Slower, higher bandwidth
│ └── Used for configuration,
│     chat, admin commands
│
├── Binary Protocol (Production)
├── High-performance binary format
├── Minimal bandwidth usage
├── Fast serialization/deserialization
└── Used for real-time gameplay,
    entity updates, world data


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
