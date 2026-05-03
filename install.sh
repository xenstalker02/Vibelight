#!/bin/bash
set -euo pipefail
trap 'echo "ERROR: install.sh failed at line $LINENO — command: $BASH_COMMAND" >&2' ERR
echo "======================================"
echo "  Vibelight Installer for Steam Deck"
echo "======================================"

DECK_HOME="${HOME:-/home/deck}"
VIBELIGHT_REPO="https://github.com/xenstalker02/Vibelight.git"
VIBELIGHT_DIR="$DECK_HOME/vibelight"
CANONICAL_WRAPPER="$DECK_HOME/vibelight-launch.sh"
# Legacy wrapper used by older personal/private setups (wake-via-Pi orchestration).
# If it's already wired into the user's Steam shortcut, we preserve it byte-for-byte
# rather than churning the AppID + losing the user's Steam Input controller layout.
LEGACY_WRAPPER="$DECK_HOME/Documents/moonlight_wake.sh"
CONFIG_DIR="$DECK_HOME/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project"

# Detect which wrapper the existing Steam shortcut points at (if any). New installs
# default to the canonical path; existing installs keep whatever's already wired up.
SHORTCUTS_VDF=$(find "$DECK_HOME/.local/share/Steam/userdata" -name "shortcuts.vdf" 2>/dev/null | head -1 || true)
ACTIVE_WRAPPER="$CANONICAL_WRAPPER"
if [ -n "$SHORTCUTS_VDF" ] && grep -qF "$LEGACY_WRAPPER" "$SHORTCUTS_VDF" 2>/dev/null; then
  ACTIVE_WRAPPER="$LEGACY_WRAPPER"
  echo "Existing Steam shortcut uses legacy wrapper — preserving it to keep AppID + controller layout stable."
fi

if [ ! -d "$VIBELIGHT_DIR/.git" ]; then
  echo "Cloning Vibelight (with submodules)..."
  git clone --recursive "$VIBELIGHT_REPO" "$VIBELIGHT_DIR"
else
  echo "Vibelight source already present — pulling latest..."
  git -C "$VIBELIGHT_DIR" fetch origin
  git -C "$VIBELIGHT_DIR" reset --hard origin/master
  git -C "$VIBELIGHT_DIR" submodule update --init --recursive
fi

echo "Building Vibelight Flatpak (10-30 minutes)..."
cd "$DECK_HOME"
flatpak run org.flatpak.Builder --user --install --force-clean vibelight-build "$VIBELIGHT_DIR/vibelight.json"
echo "Flatpak installed."

if [ "$ACTIVE_WRAPPER" = "$CANONICAL_WRAPPER" ]; then
  cat > "$CANONICAL_WRAPPER" << 'WRAPPER_EOF'
#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000
exec flatpak run --user com.moonlight_stream.Moonlight "$@"
WRAPPER_EOF
  chmod +x "$CANONICAL_WRAPPER"
  echo "Canonical launch wrapper written to $CANONICAL_WRAPPER."
else
  echo "Skipping canonical wrapper write — legacy wrapper at $LEGACY_WRAPPER is active."
fi

mkdir -p "$CONFIG_DIR"
CONF="$CONFIG_DIR/Moonlight.conf"
# Use Python to safely write micCapture=true into [General] section,
# handling fresh install, upgrade (key already exists), and any INI layout.
python3 - "$CONF" <<'PY'
import sys, os, configparser
conf_path = sys.argv[1]
cp = configparser.RawConfigParser()
cp.optionxform = str  # preserve camelCase
if os.path.exists(conf_path):
    cp.read(conf_path)
if not cp.has_section('General'):
    cp.add_section('General')
cp.set('General', 'micCapture', 'true')
os.makedirs(os.path.dirname(conf_path), exist_ok=True)
with open(conf_path, 'w') as f:
    cp.write(f, space_around_delimiters=False)
PY
echo ""
echo "NOTE: Mic passthrough has been enabled (micCapture=true in Moonlight.conf)."
echo "      This sends your Steam Deck microphone to the host PC during streaming."
echo "      To disable: open Vibelight -> Settings -> Audio Settings -> uncheck"
echo "      'Send microphone to host PC'."
echo ""

# Deploy Qt Material theme config — read at runtime, no rebuild needed.
QT_CONF_DIR="$DECK_HOME/.var/app/com.moonlight_stream.Moonlight/config/QtProject"
mkdir -p "$QT_CONF_DIR"
cp "$DECK_HOME/vibelight/app/qt_qt5.conf" "$QT_CONF_DIR/qt_qt5.conf"
echo "Qt Material theme config deployed."

# Set PipeWire mic capture volume to 50% to prevent built-in mic from
# overdriving the Opus encoder. The Deck's built-in mic runs at high gain
# by default. This is idempotent — safe to run on upgrade too.
if command -v pactl >/dev/null 2>&1; then
  pactl set-source-volume @DEFAULT_SOURCE@ 50% || \
    echo "pactl volume set failed — set mic volume manually: pactl set-source-volume @DEFAULT_SOURCE@ 50%"
  echo "PipeWire mic volume set to 50% to prevent encoder overdrive."
else
  echo "pactl not found — set mic volume manually: pactl set-source-volume @DEFAULT_SOURCE@ 50%"
fi

if command -v steamos-add-to-steam >/dev/null 2>&1; then
  # Guard: only add a Steam shortcut if NEITHER the canonical nor legacy wrapper
  # is already wired up. Checking both paths prevents the install.sh-rerun churn
  # that previously created a fresh AppID (and reset the user's Steam Input
  # controller layout, which surfaced as the Steam OSK popping unprompted).
  if [ -n "$SHORTCUTS_VDF" ] && \
     (grep -qF "$CANONICAL_WRAPPER" "$SHORTCUTS_VDF" 2>/dev/null || \
      grep -qF "$LEGACY_WRAPPER" "$SHORTCUTS_VDF" 2>/dev/null); then
    echo "Steam shortcut already exists - skipping (idempotent)."
  else
    steamos-add-to-steam "$ACTIVE_WRAPPER" 2>/dev/null && \
      echo "Added $ACTIVE_WRAPPER to Steam library." || \
      echo "Add $ACTIVE_WRAPPER to Steam manually as a non-Steam game."
  fi
else
  echo "Add $ACTIVE_WRAPPER to Steam manually as a non-Steam game."
fi

echo ""
echo "======================================"
echo "  Vibelight installed successfully!"
echo "  Switch to Game Mode to stream."
echo "  Pair with Vibepollo at:"
echo "  https://[your-pc-ip]:47990"
echo "======================================"
