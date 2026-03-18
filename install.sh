#!/bin/bash
set -e
echo "======================================"
echo "  Vibelight Installer for Steam Deck"
echo "======================================"

DECK_HOME="/home/deck"
VIBELIGHT_REPO="https://github.com/xenstalker02/Vibelight.git"
VIBELIGHT_DIR="$DECK_HOME/vibelight"
WRAPPER="$DECK_HOME/vibelight-launch.sh"
CONFIG_DIR="$HOME/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project"

if [ ! -d "$VIBELIGHT_DIR/.git" ]; then
  echo "Cloning Vibelight..."
  git clone "$VIBELIGHT_REPO" "$VIBELIGHT_DIR"
else
  echo "Vibelight source already present."
fi

echo "Building Vibelight Flatpak (10-30 minutes)..."
cd "$DECK_HOME"
flatpak run org.flatpak.Builder --user --install --force-clean vibelight-build "$DECK_HOME/vibelight.json"
echo "Flatpak installed."

cat > "$WRAPPER" << 'WRAPPER_EOF'
#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000
exec flatpak run --user com.moonlight_stream.Moonlight "$@"
WRAPPER_EOF
chmod +x "$WRAPPER"
echo "Launch wrapper created."

mkdir -p "$CONFIG_DIR"
CONF="$CONFIG_DIR/Moonlight.conf"
if grep -q "micCapture" "$CONF" 2>/dev/null; then
  sed -i 's/micCapture=.*/micCapture=true/' "$CONF"
else
  echo "micCapture=true" >> "$CONF"
fi
echo "Mic passthrough enabled."

if command -v steamos-add-to-steam >/dev/null 2>&1; then
  steamos-add-to-steam "$WRAPPER" 2>/dev/null &&     echo "Added to Steam library." ||     echo "Add $WRAPPER to Steam manually as a non-Steam game."
else
  echo "Add $WRAPPER to Steam manually as a non-Steam game."
fi

echo ""
echo "======================================"
echo "  Vibelight installed successfully!"
echo "  Switch to Game Mode to stream."
echo "  Pair with Vibepollo at:"
echo "  https://[your-pc-ip]:47990"
echo "======================================"
