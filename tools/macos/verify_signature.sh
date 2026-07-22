#!/bin/bash
set -euo pipefail

APP_PATH="${1:?usage: verify_signature.sh <app> [identity]}"
IDENTITY="${2:--}"

codesign --verify --deep --strict --verbose=2 "${APP_PATH}"

checked=0
while IFS= read -r -d '' component; do
    if ! file -b "${component}" | grep -q 'Mach-O'; then
        continue
    fi

    checked=$((checked + 1))
    codesign --verify --strict --verbose=2 "${component}"

    if [ "${IDENTITY}" != "-" ]; then
        details="$(codesign --display --verbose=4 "${component}" 2>&1)"
        if ! grep -Fq "Authority=${IDENTITY}" <<<"${details}"; then
            echo "ERROR: ${component} is not signed by ${IDENTITY}" >&2
            exit 1
        fi
        if ! grep -Eq '^CodeDirectory .* flags=.*\(runtime\)' <<<"${details}"; then
            echo "ERROR: ${component} is missing the hardened runtime flag" >&2
            exit 1
        fi
    fi
done < <(find "${APP_PATH}/Contents" -type f -print0)

if [ "${checked}" -eq 0 ]; then
    echo "ERROR: no signed Mach-O components found in ${APP_PATH}" >&2
    exit 1
fi

echo "Verified ${checked} signed Mach-O component(s) in ${APP_PATH}."
