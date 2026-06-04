#!/bin/bash

# save original home directory path
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Clone dependencies
mkdir -p thirdparty
cd thirdparty

# Clone or update ASIO
if [ -d "asio" ]; then
    echo "ASIO already exists, updating..."
    cd asio && git pull && cd ..
else
    git clone https://github.com/chriskohlhoff/asio.git
fi

if [ ! -d "asio/asio/include" ]; then
    echo "ASIO directory structure incorrect, checking..."
    if [ -f "asio/asio.hpp" ]; then
        echo "Found ASIO headers at root"
    else
        echo "Warning: ASIO structure may be unexpected"
    fi
fi

# Clone or update JSON
if [ -d "json" ]; then
    echo "JSON already exists, updating..."
    cd json && git pull && cd ..
else
    git clone https://github.com/nlohmann/json.git
fi

# Clone or update spdlog
if [ -d "spdlog" ]; then
    echo "spdlog already exists, updating..."
    cd spdlog && git pull && cd ..
else
    git clone https://github.com/gabime/spdlog.git
fi

# Clone or update GLM
if [ -d "glm" ]; then
    echo "GLM already exists, updating..."
    cd glm && git pull && cd ..
else
    git clone https://github.com/g-truc/glm.git
fi

cd ..

# Parse command line arguments
USE_CITUS=OFF
USE_SQLITE=OFF
ENABLE_ASAN=OFF
CLEAR_PREVIOUS=OFF
APT_UPDATE=OFF

for arg in "$@"; do
    case $arg in
        --with-citus)
            echo "Installing Citus extension..."
            USE_CITUS=ON
            ;;
        --with-sqlite)
            echo "Installing SQLite3 development libraries..."
            USE_SQLITE=ON
            ;;
        --with-asan)
            echo "Enabling AddressSanitizer and UndefinedBehaviorSanitizer"
            ENABLE_ASAN=ON
            ;;
        --apt-update)
            echo "Enabling linux apt update"
            APT_UPDATE=ON
            ;;
        --clear)
            echo "Enabling clear previous compilations"
            CLEAR_PREVIOUS=ON
            ;;
        *)
            ;;
    esac
done

# Build configuration
echo "Building with Citus: $USE_CITUS, SQLite: $USE_SQLITE, ASan: $ENABLE_ASAN, apt update: $APT_UPDATE"

# Install system dependencies
if [ $APT_UPDATE == ON ]; then
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    libpq-dev \
    python3-dev \
    libssl-dev \
    zlib1g-dev \
    postgresql \
    libglm-dev \
    libasio-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libx11-dev \
    libxi-dev \
    libxrandr-dev \
    mesa-common-dev \
    uuid-dev \
    libcrypt-dev \
    libfmt-dev
    if [ $USE_CITUS == ON ]; then
        sudo apt install -y postgresql-citus
    fi

    if [ $USE_SQLITE == ON ]; then
        sudo apt install -y libsqlite3-dev
    fi
fi


# Clean previous build artifacts
rm -f CMakeCache.txt Makefile cmake_install.cmake
rm -rf CMakeFiles

# Create build directory and copy related folders
mkdir -p build
cd build
if [ "$CLEAR_PREVIOUS" == ON ]; then
    echo "Clearing previous compilations..."
    find . -mindepth 1 -maxdepth 1 ! -name "certs" -exec rm -rf {} +
fi

# Run CMake
cmake .. -B . \
    -DUSE_CITUS=${USE_CITUS} \
    -DUSE_SQLITE=${USE_SQLITE} \
    -DENABLE_ASAN=${ENABLE_ASAN} \
    -DCMAKE_BUILD_TYPE=Debug

# Build
make -j$(nproc)

# ========== SSL Certificate Generation ==========
if command -v openssl &> /dev/null; then
    mkdir -p certs
    if [ ! -f "certs/server.crt" ] || [ ! -f "certs/server.key" ]; then
        echo "Generating self-signed SSL certificate..."
        openssl req -x509 -newkey rsa:4096 \
            -keyout certs/server.key \
            -out certs/server.crt \
            -days 365 -nodes \
            -subj "/CN=localhost"
        echo "SSL certificate and key created in certs/"
    fi
    if [ ! -f "certs/dhparam.pem" ]; then
        echo "Generating DH parameters (this may take a moment)..."
        openssl dhparam -out certs/dhparam.pem 2048
        echo "DH parameters generated."
    fi
else
    echo "openssl not found, skipping SSL certificate generation"
fi
# ================================================

if [ -f "gameserver" ]; then
    echo "Build successful! Executable: $(pwd)/gameserver"
else
    echo "Build failed - no executable created"
    echo "Check cmake output above for errors"
fi

# create default database user (commented out by default)
#sudo -u postgres psql -c "DROP USER IF EXISTS gameuser;"
#sudo -u postgres psql -c "CREATE USER gameuser WITH PASSWORD 'password' SUPERUSER;"

echo "Go to $SCRIPT_DIR"
cd "$SCRIPT_DIR" || exit
echo "Copy config to build folder ..."
rsync -a --delete config/ build/config/
echo "Copy dbschema to build folder ..."
rsync -a --delete dbschema/ build/dbschema/
