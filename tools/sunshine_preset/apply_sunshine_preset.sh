#!/usr/bin/env bash
# bc250-vcn-driver v0.1.0 - https://github.com/Kai/bc250-vcn-driver
#
# apply_sunshine_preset.sh - Applies the pre-tuned BC-250 streaming configuration to Sunshine
#

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}${BOLD}   Apply BC-250 Sunshine Game Streaming Preset      ${NC}"
echo -e "${BLUE}======================================================${NC}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_CONF="$SCRIPT_DIR/sunshine.conf"

# Locate Sunshine config folder (user configuration takes precedence)
TARGET_DIR="$HOME/.config/sunshine"
mkdir -p "$TARGET_DIR"

TARGET_CONF="$TARGET_DIR/sunshine.conf"

if [ -f "$TARGET_CONF" ]; then
    BACKUP_FILE="${TARGET_CONF}.backup.$(date +%s)"
    echo -e "  -> Backing up existing config to: $BACKUP_FILE"
    cp "$TARGET_CONF" "$BACKUP_FILE"
fi

echo -e "  -> Applying BC-250 configuration to: $TARGET_CONF"
cp "$SOURCE_CONF" "$TARGET_CONF"

# Check if sunshine is running via systemd
if systemctl --user is-active --quiet sunshine 2>/dev/null; then
    echo -e "  -> Restarting Sunshine user service..."
    systemctl --user restart sunshine
    echo -e "  ${GREEN}✓ Sunshine restarted successfully!${NC}"
elif command -v sunshine &> /dev/null; then
    echo -e "  ${YELLOW}Notice: Sunshine config updated. Restart Sunshine to load new settings.${NC}"
fi

echo -e "\n${GREEN}======================================================${NC}"
echo -e "${GREEN}${BOLD}   Sunshine Preset Successfully Applied!             ${NC}"
echo -e "${GREEN}======================================================${NC}"
echo -e "\nRecommended Moonlight Client Settings:"
echo -e "  * Resolution: ${GREEN}1920x1080 (1080p)${NC} or ${GREEN}1280x720 (720p)${NC}"
echo -e "  * Framerate:  ${GREEN}60 FPS${NC}"
echo -e "  * Bitrate:    ${GREEN}20 - 30 Mbps${NC}"
echo -e "  * Video Codec: ${GREEN}H.264${NC}"
