#!/bin/bash
set -euo pipefail

APP_PATH="${1:?usage: sign_app.sh <app> [identity]}"
IDENTITY="${2:--}"

# Homebrew bottles and downloaded resources can carry read-only modes or
# provenance/quarantine attributes. Both interfere with deterministic bundle
# sealing, so normalize the staged copy before applying any signature.
chmod -R u+w "${APP_PATH}"
xattr -cr "${APP_PATH}"

if [ "${IDENTITY}" = "-" ]; then
    codesign --force --deep --sign - "${APP_PATH}"
    exit 0
fi

MAIN_EXECUTABLE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
    "${APP_PATH}/Contents/Info.plist")"
MAIN_EXECUTABLE_PATH="${APP_PATH}/Contents/MacOS/${MAIN_EXECUTABLE}"

# Sign every Mach-O component before the containing app bundle. Hardened
# runtime and a trusted timestamp are required for Apple notarization. Skip
# CFBundleExecutable here because codesign resolves that path to the containing
# bundle; signing the outer app below signs the main executable after all nested
# code is ready.
while IFS= read -r -d '' component; do
    if [ "${component}" != "${MAIN_EXECUTABLE_PATH}" ] && \
       file -b "${component}" | grep -q 'Mach-O'; then
        codesign --force --sign "${IDENTITY}" \
            --options runtime --timestamp "${component}"
    fi
done < <(find "${APP_PATH}/Contents" -type f -print0)

codesign --force --sign "${IDENTITY}" \
    --options runtime --timestamp "${APP_PATH}"
