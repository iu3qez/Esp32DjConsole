#!/bin/bash
# Unified build: frontend + SPIFFS image + firmware
# Usage: ./build.sh [flash] [port]
#   ./build.sh            # Build only
#   ./build.sh flash      # Build and flash
#   ./build.sh flash /dev/ttyUSB0  # Build and flash to specific port

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== ESP32 DJ Console Build ===${NC}"

# Check ESP-IDF environment
if [ -z "$IDF_PATH" ]; then
    echo -e "${YELLOW}ESP-IDF not sourced, trying ~/esp/esp-idf/export.sh...${NC}"
    source ~/esp/esp-idf/export.sh 2>/dev/null || {
        echo -e "${RED}Error: ESP-IDF not found. Run 'source ~/esp/esp-idf/export.sh' first.${NC}"
        exit 1
    }
fi

# Step 1: Build frontend
echo -e "\n${GREEN}[1/3] Building frontend...${NC}"
if [ ! -d frontend/node_modules ]; then
    echo "Installing npm dependencies..."
    (cd frontend && npm install)
fi
(cd frontend && npm run build)
echo -e "${GREEN}Frontend built -> build/www/${NC}"
ls -lh build/www/index.html build/www/assets/* 2>/dev/null

# Step 2: Build firmware (includes SPIFFS image generation via CMake)
echo -e "\n${GREEN}[2/3] Building firmware + SPIFFS image...${NC}"
idf.py build

# Step 3: Flash if requested
if [ "$1" = "flash" ]; then
    PORT="${2:-}"
    echo -e "\n${GREEN}[3/3] Flashing...${NC}"
    if [ -n "$PORT" ]; then
        idf.py -p "$PORT" flash monitor
    else
        idf.py flash monitor
    fi
else
    echo -e "\n${GREEN}Build complete!${NC}"
    echo -e "To flash: ${YELLOW}./build.sh flash [port]${NC}"
    echo -e "Or manually: ${YELLOW}idf.py -p /dev/ttyUSB0 flash monitor${NC}"
fi
