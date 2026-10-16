#!/bin/bash
# ============================================================
# MultiCS r82a - Production Start Script
# Version: R12 (Stable)
# Binary: 756 KB (multics-r82a-stock-x64)
# Profiles: 72 profiles optimized
# Status: Production Ready ✅
# ============================================================

BINARY="bin/multics-r82a-x64"
CONFIG="multics-r24-optimized.cfg"

# Check binary
if [ ! -f "$BINARY" ]; then
    echo "❌ Error: Binary not found: $BINARY"
    exit 1
fi

# Check config
if [ ! -f "$CONFIG" ]; then
    echo "❌ Error: Config not found: $CONFIG"
    exit 1
fi

# Stop existing instance
echo "🛑 Stopping existing instances..."
pkill -f multics-r82a-x64 2>/dev/null
sleep 2

# Start server
echo "🚀 Starting MultiCS r82a (Production)..."
echo "Binary: $BINARY (756 KB)"
echo "Config: $CONFIG (72 profiles)"
echo ""
./$BINARY -c $CONFIG -b

# Wait for startup
sleep 3

# Check if running
if pgrep -f multics-r82a-x64 > /dev/null; then
    echo "✅ MultiCS started successfully!"
    echo ""
    echo "Web Interface: http://localhost:15500"
    echo "Telnet: telnet localhost 15502"
    echo ""
    echo "To check status:"
    echo "  ps aux | grep multics"
    echo ""
    echo "To view logs:"
    echo "  tail -f /var/log/multics.log"
else
    echo "❌ Failed to start MultiCS"
    exit 1
fi
