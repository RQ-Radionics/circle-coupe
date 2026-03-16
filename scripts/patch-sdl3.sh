#!/usr/bin/env bash
# scripts/patch-sdl3.sh
#
# Applies the circle-coupe patches to the SDL3 submodule.
# Run this once after cloning the repo, or after "git submodule update".
#
# Patches applied:
#   1. SDL3/src/dynapi/SDL_dynapi.h
#      Remove the #error guard that prevents SDL_DYNAMIC_API from being set
#      via compiler flags or SDL_build_config.h.
#      Reason: Circle bare-metal has no dlopen(); SDL_DYNAMIC_API must be 0.
#
# The patch is idempotent: running it twice is safe.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDL3_DIR="$REPO_ROOT/SDL3"
DYNAPI_H="$SDL3_DIR/src/dynapi/SDL_dynapi.h"

echo "circle-coupe: patching SDL3 submodule..."

# ---- Patch 1: SDL_dynapi.h -----------------------------------------------
# Replace the #ifdef SDL_DYNAMIC_API / #error block with a comment.

MARKER="circle-coupe patch"

if grep -q "$MARKER" "$DYNAPI_H"; then
    echo "  [skip] $DYNAPI_H already patched"
else
    echo "  [patch] $DYNAPI_H"

    # Use Python for reliable multi-line sed (portable across macOS/Linux)
    python3 - "$DYNAPI_H" <<'PYEOF'
import sys, re

path = sys.argv[1]
with open(path, 'r') as f:
    content = f.read()

old = (
    '#ifdef SDL_DYNAMIC_API // Tried to force it on the command line?\n'
    '#error Nope, you have to edit this file to force this off.\n'
    '#endif'
)

new = (
    '/* circle-coupe patch: allow SDL_DYNAMIC_API to be set via compiler flags\n'
    ' * or SDL_build_config.h without triggering this #error.\n'
    ' * Rationale: bare-metal target (Circle/RPi3B) has no dlopen(); dynamic API\n'
    ' * is meaningless and must be disabled.  Patch applied by scripts/patch-sdl3.sh */\n'
    '#ifdef SDL_DYNAMIC_API\n'
    '/* allowed: caller has explicitly set it (e.g. SDL_DYNAMIC_API=0) */\n'
    '#endif'
)

if old not in content:
    print(f"WARNING: expected block not found in {path} - already patched?", file=sys.stderr)
    sys.exit(0)

with open(path, 'w') as f:
    f.write(content.replace(old, new, 1))

print(f"  patched: {path}")
PYEOF
fi

echo "circle-coupe: SDL3 patches applied successfully."
echo ""
echo "Patched files:"
echo "  SDL3/src/dynapi/SDL_dynapi.h  (SDL_DYNAMIC_API guard removed)"
