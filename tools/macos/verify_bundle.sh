#!/bin/bash

set -euo pipefail

APP_PATH="${1:?usage: verify_bundle.sh <Wowee.app> [expected-arch]}"
EXPECTED_ARCH="${2:-arm64}"
CONTENTS="${APP_PATH}/Contents"
MACOS_DIR="${CONTENTS}/MacOS"
FRAMEWORKS_DIR="${CONTENTS}/Frameworks"
ICD_MANIFEST="${CONTENTS}/Resources/vulkan/icd.d/MoltenVK_icd.json"
ASSETS_DIR="${CONTENTS}/Resources/assets"
DATA_DIR="${CONTENTS}/Resources/Data"

for tool in file lipo otool codesign python3; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "ERROR: required bundle verification tool is missing: ${tool}" >&2
        exit 1
    fi
done

if [ ! -f "${MACOS_DIR}/wowee_bin" ]; then
    echo "ERROR: app bundle is missing Contents/MacOS/wowee_bin" >&2
    exit 1
fi
if [ ! -x "${MACOS_DIR}/asset_extract" ]; then
    echo "ERROR: app bundle is missing executable Contents/MacOS/asset_extract" >&2
    exit 1
fi
if [ ! -f "${FRAMEWORKS_DIR}/libMoltenVK.dylib" ]; then
    echo "ERROR: app bundle is missing Contents/Frameworks/libMoltenVK.dylib" >&2
    exit 1
fi
if [ ! -f "${FRAMEWORKS_DIR}/libSDL3.dylib" ]; then
    echo "ERROR: app bundle is missing runtime-loaded Contents/Frameworks/libSDL3.dylib" >&2
    exit 1
fi
if [ ! -f "${ICD_MANIFEST}" ]; then
    echo "ERROR: app bundle is missing the MoltenVK ICD manifest" >&2
    exit 1
fi
if [ ! -d "${ASSETS_DIR}" ]; then
    echo "ERROR: app bundle is missing Contents/Resources/assets" >&2
    exit 1
fi
if [ ! -d "${DATA_DIR}" ]; then
    echo "ERROR: app bundle is missing Contents/Resources/Data" >&2
    exit 1
fi

python3 - "${ICD_MANIFEST}" <<'PY'
import json
import pathlib
import sys

manifest = pathlib.Path(sys.argv[1])
data = json.loads(manifest.read_text(encoding="utf-8"))
library_path = data.get("ICD", {}).get("library_path")
if not library_path:
    raise SystemExit("ERROR: MoltenVK ICD manifest has no ICD.library_path")
resolved = (manifest.parent / library_path).resolve()
if not resolved.is_file():
    raise SystemExit(f"ERROR: MoltenVK ICD library_path does not resolve: {resolved}")
if not data.get("ICD", {}).get("is_portability_driver", False):
    raise SystemExit("ERROR: MoltenVK ICD manifest is not marked as a portability driver")
print(f"MoltenVK ICD resolves to {resolved}")
PY

missing=0
checked=0

while IFS= read -r -d '' owner; do
    if ! file -b "${owner}" | grep -q 'Mach-O'; then
        continue
    fi

    checked=$((checked + 1))
    archs="$(lipo -archs "${owner}")"
    case " ${archs} " in
        *" ${EXPECTED_ARCH} "*) ;;
        *)
            echo "ERROR: ${owner} does not contain ${EXPECTED_ARCH} (has: ${archs})" >&2
            missing=$((missing + 1))
            ;;
    esac

    dylib_id="$(otool -D "${owner}" 2>/dev/null | tail -n +2 | head -n 1 || true)"
    while IFS= read -r dep_line; do
        dep="$(echo "${dep_line}" | sed 's/^[[:space:]]*//;s/ (compat.*//')"
        [ -n "${dep}" ] || continue
        # A dylib's own install ID is metadata, not a library it loads.
        if [ -n "${dylib_id}" ] && [ "${dep}" = "${dylib_id}" ]; then
            continue
        fi

        case "${dep}" in
            /usr/lib/*|/System/Library/*)
                continue
                ;;
            @executable_path/*)
                resolved="${MACOS_DIR}/${dep#@executable_path/}"
                ;;
            @loader_path/*)
                resolved="$(dirname "${owner}")/${dep#@loader_path/}"
                ;;
            @rpath/*)
                suffix="${dep#@rpath/}"
                resolved=""
                while IFS= read -r rpath; do
                    case "${rpath}" in
                        @executable_path)
                            candidate="${MACOS_DIR}/${suffix}"
                            ;;
                        @executable_path/*)
                            candidate="${MACOS_DIR}/${rpath#@executable_path/}/${suffix}"
                            ;;
                        @loader_path)
                            candidate="$(dirname "${owner}")/${suffix}"
                            ;;
                        @loader_path/*)
                            candidate="$(dirname "${owner}")/${rpath#@loader_path/}/${suffix}"
                            ;;
                        /*)
                            candidate="${rpath}/${suffix}"
                            ;;
                        *)
                            continue
                            ;;
                    esac
                    if [ -e "${candidate}" ]; then
                        resolved="${candidate}"
                        break
                    fi
                done < <(otool -l "${owner}" | awk '
                    $1 == "cmd" && $2 == "LC_RPATH" { in_rpath=1; next }
                    in_rpath && $1 == "path" { print $2; in_rpath=0 }
                ')
                if [ -z "${resolved}" ]; then
                    echo "ERROR: ${owner} has no LC_RPATH resolving dependency: ${dep}" >&2
                    missing=$((missing + 1))
                    continue
                fi
                ;;
            /*)
                echo "ERROR: ${owner} retains non-system absolute dependency: ${dep}" >&2
                missing=$((missing + 1))
                continue
                ;;
            *)
                echo "ERROR: ${owner} retains unscoped dependency: ${dep}" >&2
                missing=$((missing + 1))
                continue
                ;;
        esac

        if [ ! -e "${resolved}" ]; then
            echo "ERROR: ${owner} references missing dependency: ${dep}" >&2
            missing=$((missing + 1))
        fi
    done < <(otool -L "${owner}" | tail -n +2)
done < <(find "${MACOS_DIR}" "${FRAMEWORKS_DIR}" -type f -print0)

if [ "${checked}" -eq 0 ]; then
    echo "ERROR: no Mach-O files found in app bundle" >&2
    exit 1
fi
if [ "${missing}" -ne 0 ]; then
    echo "FAIL: app bundle has ${missing} architecture or dependency error(s)" >&2
    exit 1
fi

codesign --verify --deep --strict --verbose=2 "${APP_PATH}"
echo "Verified ${checked} Mach-O files; all runtime dependencies are bundled."
