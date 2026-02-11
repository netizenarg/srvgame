#!/bin/bash
# build.sh

# Clone dependencies
mkdir -p thirdparty
cd thirdparty

git clone https://github.com/chriskohlhoff/asio.git
if [ ! -d "asio/asio/include" ]; then
    echo "ASIO directory structure incorrect, checking..."
    if [ -f "asio/asio.hpp" ]; then
        echo "Found ASIO headers at root"
    else
        echo "Warning: ASIO structure may be unexpected"
    fi
fi

git clone https://github.com/nlohmann/json.git
git clone https://github.com/gabime/spdlog.git
git clone https://github.com/g-truc/glm.git
cd ..

# Install system dependencies
#sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    python3-dev \
    libssl-dev \
    zlib1g-dev \
    postgresql-15 \
    libglm-dev \
    libasio-dev \
    libspdlog-dev \
    nlohmann-json3-dev


# Optional: Install Citus only if requested
if [ "$1" = "--with-citus" ]; then
    echo "Installing Citus extension..."
    sudo apt-get install -y postgresql-15-citus-12
    export USE_CITUS=ON
else
    echo "Building without Citus (PostgreSQL only)"
    export USE_CITUS=OFF
fi

rm -rf build
rm -f CMakeCache.txt Makefile cmake_install.cmake
rm -rf CMakeFiles

# Build
mkdir -p build
cd build
cmake .. -B . -DUSE_CITUS=${USE_CITUS:-OFF} -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

if [ -f "gameserver" ]; then
    echo "Build successful! Executable: $(pwd)/gameserver"
else
    echo "Build failed - no executable created"
    echo "Check cmake output above for errors"
fi
