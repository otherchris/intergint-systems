#!/usr/bin/env bash
# Downloads the VCV Rack SDK for this machine into dep/Rack-SDK.
# Usage: ./scripts/fetch-sdk.sh [version]   (default: $RACK_SDK_VERSION or 2.6.6)
set -euo pipefail

VERSION="${1:-${RACK_SDK_VERSION:-2.6.6}}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEP="$ROOT/dep"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)            PLATFORM=mac-arm64 ;;
  Darwin-x86_64)           PLATFORM=mac-x64 ;;
  Linux-x86_64)            PLATFORM=lin-x64 ;;
  MINGW*|MSYS*|CYGWIN*)    PLATFORM=win-x64 ;;
  *) echo "Unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

URL="https://vcvrack.com/downloads/Rack-SDK-${VERSION}-${PLATFORM}.zip"
mkdir -p "$DEP"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Fetching $URL"
curl -fL --progress-bar -o "$TMP/sdk.zip" "$URL"
rm -rf "$DEP/Rack-SDK"
unzip -q "$TMP/sdk.zip" -d "$TMP/x"
mv "$TMP/x/Rack-SDK" "$DEP/Rack-SDK"
echo "$VERSION-$PLATFORM" > "$DEP/Rack-SDK/.sdk-version"
echo "Rack SDK $VERSION ($PLATFORM) installed at $DEP/Rack-SDK"
