#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/scripts/install-common.sh"

[[ -d $BACKUP ]] || fail "No script installation backup found at $BACKUP."
check_backup
begin_transaction
restore "$BACKUP"
register_steam
ROLLBACK=0
rm -rf "$BACKUP"
echo "Removed macsteam and restored Steam's original files and signatures."
echo "Your Lua, config, and logs are preserved in: $STATE"
