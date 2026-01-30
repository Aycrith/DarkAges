# Getting Started with DarkAges Development

This guide will help you set up your development environment and understand the codebase structure.

## Prerequisites

### Required Tools

| Tool | Version | Purpose |
|------|---------|---------|
| CMake | 3.20+ | Build system |
| C++ Compiler | C++20 support | Server development |
| Docker Desktop | Latest | Infrastructure |
| Godot | 4.2+ | Client development |
| Python | 3.10+ | Stress testing |

### Windows Setup

1. **Install Visual Studio 2022**
   - Workload: "Desktop development with C++"
   - Individual components: CMake tools, AddressSanitizer

2. **Install Docker Desktop**
   - Enable WSL2 backend
   - Start Docker Desktop

3. **Install Godot 4.2+**
   - Download from https://godotengine.org/
   - Add to PATH (optional)

### Linux Setup (Ubuntu/Debian)

```bash
# Install build tools
sudo apt update
sudo apt install -y build-essential cmake git

# Install dependencies
sudo apt install -y libssl-dev libhiredis-dev

# Install Docker
sudo apt install -y docker.io docker-compose
sudo usermod -aG docker $USER
# Logout and login for group change

# Install Godot
wget https://downloads.tuxfamily.org/godotengine/4.2.1/Godot_v4.2.1-stable_linux.x86_64.zip
unzip Godot_v4.2.1-stable_linux.x86_64.zip
sudo mv Godot_v4.2.1-stable_linux.x86_64 /usr/local/bin/godot
```

### macOS Setup

```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake git

# Install Docker Desktop from https://www.docker.com/products/docker-desktop

# Install Godot
brew install --cask godot
```

## First Build

### 1. Start Infrastructure

```bash
# Start Redis and ScyllaDB
docker-compose up -d

# Verify services are running
docker-compose ps

# View logs
docker-compose logs -f
```

### 2. Build Server

```bash
# Clone repository
git clone <repository-url>
cd DarkAges

# Build release version with tests
./build.sh Release --tests

# On Windows
.\build.ps1 Release -Tests
```

### 3. Run Tests

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
./build/bin/darkages-tests "[spatial]"
```

### 4. Run Server

```bash
# Basic run
./build/bin/darkages-server

# With options
./build/bin/darkages-server --port 7777 --zone 1
```

### 5. Run Client

1. Open Godot 4.2+
2. Click "Import" button
3. Navigate to `src/client/project.godot`
4. Click "Import & Edit"
5. Press F5 to run

## Development Workflow

### Making Changes

1. **Create a feature branch**
   ```bash
   git checkout -b feature/my-feature
   ```

2. **Make your changes**
   - Follow coding standards in [AGENTS.md](../../AGENTS.md)
   - Add tests for new functionality
   - Update documentation

3. **Build and test**
   ```bash
   ./build.sh Debug --tests
   ctest --test-dir build
   ```

4. **Commit with proper message**
   ```bash
   git commit -m "[PHYSICS] Add sphere-capsule collision - 15% faster"
   ```

5. **Push and create PR**
   ```bash
   git push origin feature/my-feature
   ```

### Debugging

#### Server Debugging

**With GDB (Linux/macOS):**
```bash
gdb ./build/bin/darkages-server
(gdb) run --port 7777
(gdb) backtrace  # When crash occurs
```

**With Visual Studio (Windows):**
1. Open Visual Studio
2. File → Open → CMake → Select project root
3. Set startup item to darkages-server
4. Press F5 to debug

**With AddressSanitizer:**
```bash
./build.sh Debug --tests --sanitizers
./build/bin/darkages-server
# ASan will report memory errors
```

#### Client Debugging

1. Open Godot Editor
2. Set breakpoints in GDScript/C# code
3. Press F5 to debug
4. Use Output panel for print debugging

## Understanding the Codebase

### Server Architecture

```
src/server/
├── include/           # Public headers
│   ├── ecs/          # Entity Component System types
│   ├── physics/      # Spatial hash, movement
│   ├── netcode/      # Networking layer
│   └── zones/        # Zone server logic
│
├── src/              # Implementation
│   ├── main.cpp      # Entry point
│   ├── ecs/          # Component implementations
│   ├── physics/      # Physics systems
│   ├── netcode/      # Network manager
│   └── zones/        # Zone server
│
└── tests/            # Unit tests
    ├── TestSpatialHash.cpp
    ├── TestMovementSystem.cpp
    └── TestNetworkProtocol.cpp
```

### Key Classes

| Class | File | Purpose |
|-------|------|---------|
| `ZoneServer` | `zones/ZoneServer.hpp` | Main server orchestrator |
| `NetworkManager` | `netcode/NetworkManager.hpp` | UDP networking |
| `SpatialHash` | `physics/SpatialHash.hpp` | O(1) spatial queries |
| `MovementSystem` | `physics/MovementSystem.hpp` | Character movement |
| `RedisManager` | `db/RedisManager.hpp` | Hot-state cache |

### Client Architecture

```
src/client/
├── src/
│   ├── networking/    # Network client
│   │   └── NetworkManager.cs
│   ├── prediction/    # Client-side prediction
│   │   └── PredictedPlayer.cs
│   └── GameState.cs   # Global game state
│
├── scripts/          # Game logic
│   ├── Main.cs       # Main scene
│   └── UI.cs         # UI controller
│
└── scenes/           # Godot scenes
    ├── Main.tscn
    └── Player.tscn
```

## Common Tasks

### Adding a New Component

1. **Define component struct** in `include/ecs/CoreTypes.hpp`:
```cpp
struct MyComponent {
    float value{0.0f};
};
```

2. **Use in system**:
```cpp
auto view = registry.view<Position, MyComponent>();
view.each([](auto& pos, auto& comp) {
    // Process
});
```

3. **Add test** in `tests/TestMyComponent.cpp`

### Adding a New Packet Type

1. **Define in FlatBuffers schema** (`shared/proto/game_protocol.fbs`):
```protobuf
table MyPacket {
    value:float;
}
```

2. **Regenerate code**:
```bash
flatc --cpp -o src/shared/generated src/shared/proto/game_protocol.fbs
```

3. **Handle in NetworkManager**

### Running Specific Tests

```bash
# Run only spatial hash tests
./build/bin/darkages-tests "[spatial]"

# Run with verbose output
./build/bin/darkages-tests -s "[spatial]"

# Run benchmark tests
./build/bin/darkages-tests "[performance]"
```

## Troubleshooting

### Build Issues

**CMake not found:**
```bash
# Windows: Install Visual Studio with CMake
# Linux: sudo apt install cmake
# macOS: brew install cmake
```

**EnTT not found:**
- EnTT is fetched automatically via CMake FetchContent
- Check internet connection
- Clear CMake cache: `rm -rf build`

### Runtime Issues

**Server won't start:**
```bash
# Check if port is in use
netstat -an | grep 7777

# Check Redis connection
docker-compose ps

# Run with verbose logging
./build/bin/darkages-server --verbose
```

**Client can't connect:**
- Verify server is running: `netstat -an | grep 7777`
- Check Windows Firewall settings
- Try localhost (127.0.0.1) instead of IP

### Docker Issues

**Containers won't start:**
```bash
# Check Docker is running
docker info

# Reset everything
docker-compose down -v
docker-compose up -d
```

**Out of disk space:**
```bash
# Clean up Docker
docker system prune -a
docker volume prune
```

## Next Steps

- Read [Architecture Overview](Overview.md)
- Study [Network Protocol](../network-protocol/Protocol.md)
- Review [Implementation Roadmap](../../ImplementationRoadmapGuide.md)
- Join development discussions

## Getting Help

- Check existing documentation in `docs/`
- Review test files for usage examples
- Check [AI_COORDINATION_PROTOCOL.md](../../AI_COORDINATION_PROTOCOL.md) for agent workflow
