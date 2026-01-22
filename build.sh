#!/bin/bash
# build.sh

# Clone dependencies
mkdir -p thirdparty
cd thirdparty
git clone https://github.com/chriskohlhoff/asio.git
git clone https://github.com/nlohmann/json.git
git clone https://github.com/gabime/spdlog.git
git clone https://github.com/g-truc/glm.git
cd ..

# Install system dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libpq-dev \
    python3-dev \
    libssl-dev \
    zlib1g-dev \
    postgresql-15

# Optional: Install Citus only if requested
if [ "$1" = "--with-citus" ]; then
    echo "Installing Citus extension..."
    sudo apt-get install -y postgresql-15-citus-12
    export USE_CITUS=ON
else
    echo "Building without Citus (PostgreSQL only)"
    export USE_CITUS=OFF
fi

# Build
mkdir -p build
cd build
cmake .. -DUSE_CITUS=${USE_CITUS:-OFF} -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo "Build complete!"