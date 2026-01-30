#!/bin/bash
# DarkAges MMO - Integration Test Environment Setup (WP-6-5)
#
# This script sets up the local environment for integration testing.
# Usage: ./setup_test_env.sh

set -e

echo "======================================================================"
echo "DarkAges MMO - Integration Test Environment Setup (WP-6-5)"
echo "======================================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check Python version
echo ""
echo "[1/6] Checking Python version..."
python_version=$(python3 --version 2>&1 | awk '{print $2}')
echo "Python version: $python_version"

if python3 -c "import sys; exit(0 if sys.version_info >= (3, 8) else 1)"; then
    echo -e "${GREEN}✓ Python 3.8+ detected${NC}"
else
    echo -e "${RED}✗ Python 3.8+ required${NC}"
    exit 1
fi

# Install Python dependencies
echo ""
echo "[2/6] Installing Python dependencies..."
pip3 install --quiet --upgrade pip
pip3 install --quiet -r requirements.txt
echo -e "${GREEN}✓ Dependencies installed${NC}"

# Check for Docker
echo ""
echo "[3/6] Checking Docker..."
if command -v docker &> /dev/null; then
    docker_version=$(docker --version)
    echo -e "${GREEN}✓ Docker found: $docker_version${NC}"
    
    # Check if Docker Compose is available
    if docker-compose --version &> /dev/null || docker compose version &> /dev/null; then
        echo -e "${GREEN}✓ Docker Compose found${NC}"
    else
        echo -e "${YELLOW}⚠ Docker Compose not found (optional for local testing)${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Docker not found (optional for local testing)${NC}"
fi

# Check for tc (Linux only)
echo ""
echo "[4/6] Checking network tools (for chaos testing)..."
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    if command -v tc &> /dev/null; then
        echo -e "${GREEN}✓ tc (traffic control) found - chaos testing available${NC}"
    else
        echo -e "${YELLOW}⚠ tc not found. Install with: sudo apt-get install iproute2${NC}"
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo -e "${YELLOW}⚠ macOS detected - chaos testing limited${NC}"
else
    echo -e "${YELLOW}⚠ Unsupported OS for chaos testing${NC}"
fi

# Verify directory structure
echo ""
echo "[5/6] Verifying directory structure..."
if [ -f "integration_harness.py" ]; then
    echo -e "${GREEN}✓ integration_harness.py found${NC}"
else
    echo -e "${RED}✗ integration_harness.py not found${NC}"
    exit 1
fi

if [ -f "bot_swarm.py" ]; then
    echo -e "${GREEN}✓ bot_swarm.py found${NC}"
else
    echo -e "${RED}✗ bot_swarm.py not found${NC}"
    exit 1
fi

if [ -f "network_chaos.py" ]; then
    echo -e "${GREEN}✓ network_chaos.py found${NC}"
else
    echo -e "${YELLOW}⚠ network_chaos.py not found${NC}"
fi

# Check if server binary exists
echo ""
echo "[6/6] Checking for DarkAges server binary..."
if [ -f "../../build/darkages_server" ]; then
    echo -e "${GREEN}✓ Server binary found at ../../build/darkages_server${NC}"
elif [ -f "../../build/darkages-server" ]; then
    echo -e "${GREEN}✓ Server binary found at ../../build/darkages-server${NC}"
else
    echo -e "${YELLOW}⚠ Server binary not found. Build with: cmake --build build${NC}"
fi

echo ""
echo "======================================================================"
echo "Setup Complete!"
echo "======================================================================"
echo ""
echo "Next steps:"
echo ""
echo "1. Start infrastructure services (if not already running):"
echo "   cd ../../infra && docker-compose up -d redis scylla"
echo ""
echo "2. Start the DarkAges server:"
echo "   cd ../../build && ./darkages_server"
echo ""
echo "3. Run integration tests:"
echo "   python3 integration_harness.py --all"
echo ""
echo "4. Or run specific tests:"
echo "   python3 integration_harness.py --test basic_connectivity"
echo "   python3 integration_harness.py --test 10_player_session"
echo "   python3 integration_harness.py --stress 100 --duration 60"
echo ""
echo "5. List all available tests:"
echo "   python3 integration_harness.py --list"
echo ""
echo "======================================================================"
