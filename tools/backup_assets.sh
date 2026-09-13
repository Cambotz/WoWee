#!/usr/bin/env bash
#
# Backup extracted WoW assets (Data/) to a compressed archive.
# Usage: ./tools/backup_assets.sh [DATA_DIR] [BACKUP_DIR]
#
# If not provided, the script will prompt for both paths.

set -euo pipefail

DEFAULT_DATA="./Data"
DEFAULT_BACKUP="$HOME/.local/share/wowee/backups"

# --- Resolve data directory ---
DATA_DIR="${1:-}"
if [ -z "$DATA_DIR" ]; then
    read -rp "Data directory to back up [$DEFAULT_DATA]: " DATA_DIR
    DATA_DIR="${DATA_DIR:-$DEFAULT_DATA}"
fi

if [ ! -d "$DATA_DIR" ]; then
    echo "Error: Data directory not found: $DATA_DIR" >&2
    exit 1
fi

# What an extracted tree looks like, which is not what this used to check for.
# It wanted manifest.json at the top, and extraction has never written one
# there: the manifest is per-expansion, under expansions/<name>/manifest.json.
# So this refused every real extraction it was ever pointed at, and the only
# way to keep a copy was to tar the directory by hand.
if [ ! -d "$DATA_DIR/expansions" ] && [ ! -f "$DATA_DIR/manifest.json" ]; then
    echo "Error: $DATA_DIR has no expansions/ directory and no manifest.json -" >&2
    echo "       that does not look like an extracted asset tree." >&2
    echo "       Point this at the Data directory extraction wrote." >&2
    exit 1
fi

# --- Resolve backup directory ---
BACKUP_DIR="${2:-}"
if [ -z "$BACKUP_DIR" ]; then
    read -rp "Backup destination [$DEFAULT_BACKUP]: " BACKUP_DIR
    BACKUP_DIR="${BACKUP_DIR:-$DEFAULT_BACKUP}"
fi

mkdir -p "$BACKUP_DIR"

# --- Build archive name with timestamp ---
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
ARCHIVE="$BACKUP_DIR/wowee_assets_$TIMESTAMP.tar.zst"

# --- Measure source size ---
DATA_SIZE=$(du -sh "$DATA_DIR" | cut -f1)
echo "Backing up $DATA_DIR ($DATA_SIZE) ..."
echo "Destination: $ARCHIVE"

# --- Compress ---
if command -v zstd &>/dev/null; then
    tar cf - -C "$(dirname "$DATA_DIR")" "$(basename "$DATA_DIR")" | zstd -T0 -3 -o "$ARCHIVE"
elif command -v pigz &>/dev/null; then
    ARCHIVE="$BACKUP_DIR/wowee_assets_$TIMESTAMP.tar.gz"
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
    tar cf - -C "$(dirname "$DATA_DIR")" "$(basename "$DATA_DIR")" | pigz -p "$NPROC" > "$ARCHIVE"
else
    ARCHIVE="$BACKUP_DIR/wowee_assets_$TIMESTAMP.tar.gz"
    tar czf "$ARCHIVE" -C "$(dirname "$DATA_DIR")" "$(basename "$DATA_DIR")"
fi

ARCHIVE_SIZE=$(du -sh "$ARCHIVE" | cut -f1)
echo "Done: $ARCHIVE ($ARCHIVE_SIZE)"
