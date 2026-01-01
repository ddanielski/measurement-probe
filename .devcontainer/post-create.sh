#!/bin/bash
# =============================================================================
# Post-Create Script - Project-specific setup only
# =============================================================================
# This script runs once when the container is created.
# Heavy lifting (package installs, p10k) is done in the Dockerfile.
# =============================================================================

set -e

echo "=== Configuring project environment ==="

# -----------------------------------------------------------------------------
# Copy project's Powerlevel10k configuration
# -----------------------------------------------------------------------------
if [ -f ".devcontainer/.p10k.zsh" ]; then
    echo "• Applying project p10k configuration..."
    cp .devcontainer/.p10k.zsh ~/.p10k.zsh
fi

# -----------------------------------------------------------------------------
# Set ESP-IDF target to ESP32-C3
# -----------------------------------------------------------------------------
if [ -f "CMakeLists.txt" ]; then
    echo "• Setting ESP-IDF target to ESP32-C3..."
    . /opt/esp/idf/export.sh
    idf.py set-target esp32c3
fi

# -----------------------------------------------------------------------------
# Create .cache directory for bind mount (if not exists)
# -----------------------------------------------------------------------------
mkdir -p ~/.cache

echo ""
echo "=== Project setup complete ==="
echo ""
echo "Commands:"
echo "  idf.py build                         - Build the project"
echo "  idf.py -p /dev/ttyUSB0 flash monitor - Flash and monitor"
echo "  p10k configure                       - Reconfigure prompt theme"
