#!/usr/bin/env bash
# Grant non-root access to common ESP32 USB-serial adapters.
set -euo pipefail

RULES_FILE="/etc/udev/rules.d/99-esp32-marauder.rules"

sudo tee "$RULES_FILE" > /dev/null <<'EOF'
# CH340 / CH341
SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", MODE="0666", GROUP="dialout"
# CP2102 / CP2104 (Silicon Labs)
SUBSYSTEM=="tty", ATTRS{idVendor}=="10c4", MODE="0666", GROUP="dialout"
# FTDI FT232
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", MODE="0666", GROUP="dialout"
# Espressif USB-CDC (ESP32-S2/S3 native USB)
SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", MODE="0666", GROUP="dialout"
EOF

sudo udevadm control --reload-rules
sudo udevadm trigger

echo "udev rules written to $RULES_FILE"
echo "You may also need to add yourself to the 'dialout' group:"
echo "  sudo usermod -aG dialout \$USER"
echo "Then log out and back in (or run: newgrp dialout)"
