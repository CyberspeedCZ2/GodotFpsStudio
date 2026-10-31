#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/engine"
TAG="4.7.2-stable"
fail() {
    printf 'Build stopped: %s\n' "$1" >&2
    exit 1
}

[[ "$(uname -s)" == "Darwin" ]] || fail "this script prepares the native macOS build."
[[ "$(uname -m)" == "arm64" ]] || fail "this first build targets Apple Silicon arm64."

for tool in git python3 xcodebuild xcrun; do
    command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

python3 - <<'PY' || fail "Python 3.9 or newer is required."
import sys
if sys.version_info < (3, 9):
    raise SystemExit(1)
PY

command -v scons >/dev/null 2>&1 || fail "SCons 4.4 or newer is required. Install it with Homebrew (brew install scons), then retry."

if [[ ! -d "$ENGINE" || ! -f "$ENGINE/SConstruct" ]]; then
    fail "engine/ source directory with SConstruct is missing."
fi

if [[ -z "${VULKAN_SDK:-}" || ! -d "${VULKAN_SDK:-/nonexistent}" ]]; then
    [[ -x "$ENGINE/bin/godot.macos.editor.dev.arm64" && -f "$ENGINE/.sconsign5.dblite" ]] || fail "Vulkan SDK/MoltenVK is required for a clean build. Install it using engine/misc/scripts/install_vulkan_sdk_macos.sh and set VULKAN_SDK. An existing configured SCons build can be rebuilt without reinstalling the SDK."
    printf 'Using the existing configured SCons build cache; no Vulkan SDK installation is needed for this rebuild.\n'
fi

cd "$ENGINE"
export CLANG_MODULE_CACHE_PATH="$ENGINE/bin/clang-module-cache"
mkdir -p "$CLANG_MODULE_CACHE_PATH"
printf 'Building Godot %s editor for macOS arm64...\n' "$TAG"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
scons -j"$JOBS" platform=macos arch=arm64 dev_build=yes "cxxflags=-fmodules-cache-path=$CLANG_MODULE_CACHE_PATH"

DEV_BINARY="$ENGINE/bin/godot.macos.editor.dev.arm64"
APP_STAGE="$(mktemp -d "$ROOT/.godot-fps-build.XXXXXX")"
trap 'rm -rf "$APP_STAGE"' EXIT
APP_FINAL="$ROOT/Godot FPS Studio.app"
APP_BACKUP="$ROOT/.Godot FPS Studio.app.previous"
if [[ -e "$APP_BACKUP" ]]; then
    fail "temporary app backup already exists at $APP_BACKUP; inspect it before rebuilding."
fi
APP="$APP_STAGE/Godot FPS Studio.app"
APP_CONTENTS="$APP/Contents"
APP_BINARY="$APP_CONTENTS/MacOS/Godot"
ditto "$ENGINE/misc/dist/macos_tools.app" "$APP"
mkdir -p "$APP_CONTENTS/MacOS" "$APP_CONTENTS/Resources"
cp "$DEV_BINARY" "$APP_BINARY"
chmod +x "$APP_BINARY"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName "Godot FPS Studio"' "$APP_CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :CFBundleDisplayName string "Godot FPS Studio"' "$APP_CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier org.godotengine.godotfpsstudio' "$APP_CONTENTS/Info.plist"
xattr -cr "$APP"
codesign --force --sign - --entitlements "$ENGINE/misc/dist/macos/editor.entitlements" "$APP"
codesign --verify --deep --strict "$APP"
"$APP_BINARY" --version

if [[ -e "$APP_FINAL" ]]; then
    mv "$APP_FINAL" "$APP_BACKUP"
fi
if ! mv "$APP" "$APP_FINAL"; then
    if [[ -e "$APP_BACKUP" ]]; then
        mv "$APP_BACKUP" "$APP_FINAL"
    fi
    fail "could not install the newly built app bundle."
fi
rm -rf "$APP_BACKUP"

printf '\nBuild complete: %s\n' "$APP_FINAL"
printf 'Binary: %s\n' "$DEV_BINARY"
