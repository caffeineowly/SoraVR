#!/bin/sh
# Builds the release zip: release/SoraVR-v<version>.zip and release/SHA256SUMS.txt
#
# Zip layout (extract into the game folder, next to sora_1st.exe):
#   d3d11.dll             SoraVR
#   openxr_loader.dll     official Khronos OpenXR loader (pinned version, checksum verified)
#   SoraVR/               README.md, CHANGELOG.md, LICENSE.txt (GPLv3), LICENSE-OpenXR.txt,
#                         SoraVR.default.ini
set -e
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^#define SORAVR_VERSION "\(.*\)".*/\1/p' src/soravr.cpp)
[ -n "$VERSION" ] || { echo "SORAVR_VERSION not found"; exit 1; }

# Official loader from https://github.com/KhronosGroup/OpenXR-SDK-Source/releases
OXR_VER=1.1.65
OXR_ZIP_SHA256=859d7de35b7a968112db770b09ed8bddeed3cb16fd8ae070643a857676bea6fd
OXR_DLL_SHA256=b0d0eed4e2b6aeb3ce529d82daf3daa5db104857d65c872884b4661142366ce2

./build.sh

CACHE=build/openxr-$OXR_VER
mkdir -p "$CACHE"
if [ ! -f "$CACHE/loader.zip" ]; then
    curl -fsSL -o "$CACHE/loader.zip" \
        "https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/release-$OXR_VER/openxr_loader_windows-$OXR_VER.zip"
fi
echo "$OXR_ZIP_SHA256  $CACHE/loader.zip" | sha256sum -c --quiet
unzip -o -q "$CACHE/loader.zip" x64/bin/openxr_loader.dll share/doc/openxr/LICENSE -d "$CACHE"
echo "$OXR_DLL_SHA256  $CACHE/x64/bin/openxr_loader.dll" | sha256sum -c --quiet

NAME=SoraVR-v$VERSION
STAGE=build/release/$NAME
rm -rf "$STAGE" && mkdir -p "$STAGE/SoraVR"
cp bin/d3d11.dll "$STAGE/"
cp "$CACHE/x64/bin/openxr_loader.dll" "$STAGE/"
cp README.md CHANGELOG.md "$STAGE/SoraVR/"
cp LICENSE "$STAGE/SoraVR/LICENSE.txt"
cp "$CACHE/share/doc/openxr/LICENSE" "$STAGE/SoraVR/LICENSE-OpenXR.txt"
cp dist/SoraVR.ini "$STAGE/SoraVR/SoraVR.default.ini"

mkdir -p release
rm -f "release/$NAME.zip"
(cd "$STAGE" && zip -q -X -r "../../../release/$NAME.zip" .)
(cd release && sha256sum "$NAME.zip" > SHA256SUMS.txt)
echo "openxr_loader.dll $OXR_VER sha256 $OXR_DLL_SHA256" >> release/SHA256SUMS.txt
(cd "$STAGE" && sha256sum d3d11.dll) | sed 's/^/contents: /' >> release/SHA256SUMS.txt

echo "release/$NAME.zip"
unzip -l "release/$NAME.zip"
cat release/SHA256SUMS.txt
