#!/bin/bash
set -euo pipefail

SOURCE_PNG="${1:?usage: create_icns.sh <source.png> <output.icns>}"
OUTPUT_ICNS="${2:?usage: create_icns.sh <source.png> <output.icns>}"
ICONSET="$(mktemp -d)/Wowee.iconset"
mkdir -p "${ICONSET}"

for size in 16 32 128 256 512; do
    double=$((size * 2))
    sips -z "${size}" "${size}" "${SOURCE_PNG}" \
        --out "${ICONSET}/icon_${size}x${size}.png" >/dev/null
    sips -z "${double}" "${double}" "${SOURCE_PNG}" \
        --out "${ICONSET}/icon_${size}x${size}@2x.png" >/dev/null
done

mkdir -p "$(dirname "${OUTPUT_ICNS}")"
iconutil -c icns "${ICONSET}" -o "${OUTPUT_ICNS}"
