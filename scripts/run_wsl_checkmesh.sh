#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <case_dir> [checkMesh_args...]" >&2
    exit 2
fi

RAW_DIR="$1"
shift

if [[ "$RAW_DIR" =~ ^([A-Za-z]):[\\/](.*)$ ]]; then
    DRIVE="${BASH_REMATCH[1],,}"
    REST="${BASH_REMATCH[2]//\\//}"
    CASE_DIR="/mnt/${DRIVE}/${REST}"
else
    CASE_DIR="${RAW_DIR//\\//}"
fi

OPENFOAM_RC=""
for candidate in \
    /usr/lib/openfoam/openfoam*/etc/bashrc \
    /opt/openfoam*/etc/bashrc \
    "$HOME"/OpenFOAM/*/etc/bashrc; do
    if [ -f "$candidate" ]; then
        OPENFOAM_RC="$candidate"
        break
    fi
done

if [ -n "$OPENFOAM_RC" ]; then
    set +u
    # shellcheck disable=SC1090
    source "$OPENFOAM_RC" >/dev/null 2>&1 || true
    set -u
fi

if ! command -v checkMesh >/dev/null 2>&1; then
    echo "ERROR: OpenFOAM checkMesh executable not found in PATH." >&2
    exit 127
fi

cd "$CASE_DIR"
exec checkMesh "$@"
