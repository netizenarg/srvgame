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

# Parse command line arguments for optional database backends
USE_CITUS=OFF
USE_SQLITE=OFF

for arg in "$@"; do
    case $arg in
        --with-citus)
            echo "Installing Citus extension..."
            sudo apt-get install -y postgresql-15-citus-12
            USE_CITUS=ON
            ;;
        --with-sqlite)
            echo "Installing SQLite3 development libraries..."
            sudo apt-get install -y libsqlite3-dev
            USE_SQLITE=ON
            ;;
        *)
            # ignore unknown
            ;;
    esac
done

# Build configuration
echo "Building with Citus: $USE_CITUS, SQLite: $USE_SQLITE"

# Clean previous build artifacts
rm -f CMakeCache.txt Makefile cmake_install.cmake
rm -rf CMakeFiles

# Create build directory and copy related folders
mkdir -p build
rsync -a --delete config/ build/config/
rsync -a --delete dbschema/ build/dbschema/
cd build

# Run CMake
cmake .. -B . \
    -DUSE_CITUS=${USE_CITUS} \
    -DUSE_SQLITE=${USE_SQLITE} \
    -DCMAKE_BUILD_TYPE=Debug

# Build
make -j$(nproc)

# ========== SSL Certificate Generation ==========
# Generate self-signed SSL certificates if missing
if command -v openssl &> /dev/null; then
    # Create certs directory if needed
    mkdir -p certs
    # Generate server certificate and key if not present
    if [ ! -f "certs/server.crt" ] || [ ! -f "certs/server.key" ]; then
        echo "Generating self-signed SSL certificate..."
        openssl req -x509 -newkey rsa:4096 \
            -keyout certs/server.key \
            -out certs/server.crt \
            -days 365 -nodes \
            -subj "/CN=localhost"
        echo "SSL certificate and key created in certs/"
    fi
    # Generate DH parameters if not present (optional but may be used)
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
