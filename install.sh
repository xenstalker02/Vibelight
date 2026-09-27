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
# Some existing installs use a legacy launch-wrapper path. If one is already wired into
# the user's Steam shortcut, preserve it byte-for-byte rather than churning the AppID
# and losing the user's Steam Input controller layout.
LEGACY_WRAPPER="$DECK_HOME/Documents/moonlight_wake.sh"

# Detect which wrapper the existing Steam shortcut points at (if any). New installs
# default to the canonical path; existing installs keep whatever's already wired up.
SHORTCUTS_VDF=$(find "$DECK_HOME/.local/share/Steam/userdata" -name "shortcuts.vdf" 2>/dev/null | head -1 || true)
ACTIVE_WRAPPER="$CANONICAL_WRAPPER"
if [ -n "$SHORTCUTS_VDF" ] && grep -qF "$LEGACY_WRAPPER" "$SHORTCUTS_VDF" 2>/dev/null; then
  ACTIVE_WRAPPER="$LEGACY_WRAPPER"
  echo "Existing Steam shortcut uses legacy wrapper — preserving it to keep AppID + controller layout stable."
fi

checkout_error() {
  echo "ERROR: $* No reset or cleanup was performed. Resolve the checkout manually, then rerun." >&2
  exit 1
}

# Refuse local work, including ignored files that a checkout could overwrite.
# Inspect every initialized submodule too; Git's parent status can hide their
# ignored files or be configured to ignore submodule changes entirely.
require_clean_checkout() {
  local changes submodules
  changes=$(git -C "$VIBELIGHT_DIR" status --porcelain --untracked-files=all --ignored --ignore-submodules=none)
  [ -z "$changes" ] || checkout_error "Vibelight has local changes or untracked/ignored files."
  submodules=$(git -C "$VIBELIGHT_DIR" submodule status --recursive)
  if printf '%s\n' "$submodules" | grep -qE '^[+U]'; then
    checkout_error "A submodule differs from its recorded commit."
  fi
  git -C "$VIBELIGHT_DIR" submodule foreach --quiet --recursive '
    changes=$(git status --porcelain --untracked-files=all --ignored --ignore-submodules=none) || exit 1
    test -z "$changes"
  ' || checkout_error "A submodule has local changes or untracked/ignored files."
}

if [ ! -e "$VIBELIGHT_DIR" ]; then
  echo "Cloning Vibelight (with submodules)..."
  git clone --branch master --recursive "$VIBELIGHT_REPO" "$VIBELIGHT_DIR"
else
  # Linked worktrees have a .git file rather than a directory.
  [ -e "$VIBELIGHT_DIR/.git" ] || checkout_error "Vibelight path exists but is not a Git checkout."
  branch=$(git -C "$VIBELIGHT_DIR" symbolic-ref --quiet --short HEAD) || checkout_error "Vibelight HEAD is detached."
  [ "$branch" = master ] || checkout_error "Vibelight must be on master (currently $branch)."
  origin=$(git -C "$VIBELIGHT_DIR" config --get-all remote.origin.url) || checkout_error "Vibelight has no origin."
  case "$origin" in
    https://github.com/xenstalker02/Vibelight|https://github.com/xenstalker02/Vibelight.git|git@github.com:xenstalker02/Vibelight.git|ssh://git@github.com/xenstalker02/Vibelight.git) ;;
    *) checkout_error "Vibelight origin is not the expected repository." ;;
  esac
  require_clean_checkout
  echo "Vibelight source already present — checking safe fast-forward..."
  git -C "$VIBELIGHT_DIR" fetch --no-recurse-submodules origin refs/heads/master
  git -C "$VIBELIGHT_DIR" merge-base --is-ancestor HEAD FETCH_HEAD || checkout_error "Vibelight has local commits or divergent history."
  require_clean_checkout
  git -C "$VIBELIGHT_DIR" -c merge.autostash=false merge --ff-only --no-autostash FETCH_HEAD
  git -C "$VIBELIGHT_DIR" submodule update --init --recursive
fi

# Ensure the Flatpak Builder is available (required to build the app below). Without it a
# fresh Steam Deck fails with "app/org.flatpak.Builder/x86_64/... not installed" (issue #29).
echo "Ensuring Flathub remote + Flatpak Builder..."
flatpak remote-add --if-not-exists --user flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install -y --user flathub org.flatpak.Builder

echo "Building Vibelight Flatpak (10-30 minutes)..."
cd "$DECK_HOME"
# --install-deps-from pulls the runtime/sdk named in vibelight.json (currently
# org.kde.Platform + org.kde.Sdk 6.10). Without it a fresh Steam Deck fails with
# "Failed to init: Unable to find sdk org.kde.Sdk version 6.10".
flatpak run org.flatpak.Builder --user --install-deps-from=flathub --install --force-clean vibelight-build "$VIBELIGHT_DIR/vibelight.json"
echo "Flatpak installed."

if [ "$ACTIVE_WRAPPER" = "$CANONICAL_WRAPPER" ]; then
  cat > "$CANONICAL_WRAPPER" << 'WRAPPER_EOF'
#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000
exec flatpak run --user com.moonlight_stream.Moonlight
WRAPPER_EOF
  chmod +x "$CANONICAL_WRAPPER"
  echo "Canonical launch wrapper written to $CANONICAL_WRAPPER."
else
  echo "Skipping canonical wrapper write — legacy wrapper at $LEGACY_WRAPPER is active."
fi

echo ""
echo "NOTE: Mic passthrough is OFF by default — it is opt-in."
echo "      To enable: open Vibelight -> Settings -> Audio Settings -> check"
echo "      'Send microphone to host PC'."
echo "      (This sends your Steam Deck microphone to the host PC while streaming.)"
echo ""

# Deploy Qt Material theme config — read at runtime, no rebuild needed.
QT_CONF_DIR="$DECK_HOME/.var/app/com.moonlight_stream.Moonlight/config/QtProject"
mkdir -p "$QT_CONF_DIR"
cp "$VIBELIGHT_DIR/app/qt_qt5.conf" "$QT_CONF_DIR/qt_qt5.conf"
echo "Qt Material theme config deployed."

# Set PipeWire mic capture volume only with explicit installer opt-in.
# The default source may be an external mic, and microphone streaming is
# disabled by default; an upgrade must preserve the user's audio settings.
if [ "${VIBELIGHT_SET_MIC_VOLUME:-0}" != 1 ]; then
  echo "Microphone volume preserved. To set the current default mic to 50%, rerun with VIBELIGHT_SET_MIC_VOLUME=1."
elif command -v pactl >/dev/null 2>&1; then
  if pactl set-source-volume @DEFAULT_SOURCE@ 50%; then
    echo "PipeWire mic volume set to 50% to prevent encoder overdrive."
  else
    echo "pactl volume set failed — set mic volume manually: pactl set-source-volume @DEFAULT_SOURCE@ 50%"
  fi
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
