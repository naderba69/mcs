#!/bin/bash
# ============================================================
# MultiCS r82a - Production Start Script
# Version: R32 (2026-10-16)
# Binary: bin/multics-r82a-stock-x64 (the maintained release build)
# Profiles: 71 profiles, guarded live by `make -C tests prof`
# Status: Production Ready ✅
# ============================================================

BINARY="bin/multics-r82a-stock-x64"
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
pkill -f multics-r82a-stock-x64 2>/dev/null
sleep 2

# Start server
echo "🚀 Starting MultiCS r82a (Production)..."
echo "Binary: $BINARY"
echo "Config: $CONFIG (71 profiles)"
echo ""
./$BINARY -C "$CONFIG" -b

# Wait for startup
sleep 3

# Check if running
if pgrep -f multics-r82a-stock-x64 > /dev/null; then
    echo "✅ MultiCS started successfully!"
    echo ""
    echo "Web/Telnet: the HTTP PORT / TELNET PORT of your config file"
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
