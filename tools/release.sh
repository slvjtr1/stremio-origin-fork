#!/bin/bash
# A release in one step:  tools/release.sh VERSION
# Sets the version (pkg/appinfo.json and the What's new card, which must match) and builds the IPK
# (tools/package.sh) with its checksum: dist/<id>_<version>_arm.ipk and .sha256. Both are the assets of the
# GitHub release v<version>; the app's own update check reads the latest release (src/core/update.c).
# No git, no push: review, then commit, push and make the release yourself. The version must go up every time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="${1:?usage: tools/release.sh VERSION}"
ID=tv.stremioorigin.app
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must look like 1.0.1"; exit 1; }

PY=; for c in python3 python; do if "$c" -c 1 >/dev/null 2>&1; then PY=$c; break; fi; done; [ -n "$PY" ] || { echo "python needed"; exit 1; }
"$PY" - "$ROOT" "$VER" <<'EOF'
import re, sys
root, ver = sys.argv[1], sys.argv[2]
for f in ('pkg/appinfo.json', 'assets/whatsnew.json'): # the version alone, the files as they are written
    p = root + '/' + f; t = open(p, encoding='utf-8').read(); n = re.subn(r'("version"\s*:\s*")[^"]*(")', r'\g<1>' + ver + r'\g<2>', t, count=1)
    if n[1] != 1: sys.exit('no version in ' + f)
    open(p, 'w', encoding='utf-8', newline='\n').write(n[0])
EOF

"$ROOT/tools/build.sh" webos # always built now: the version just set is the one inside, and its debug build is kept below
rm -f "$ROOT/dist/${ID}_${VER}_arm.ipk"
"$ROOT/tools/package.sh" >/dev/null
IPK="$ROOT/dist/${ID}_${VER}_arm.ipk"
[ -f "$IPK" ] || { echo "no $IPK"; exit 1; }
SHA=$("$PY" -c "import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())" "$IPK")
( cd "$ROOT/dist" && printf '%s *%s\n' "$SHA" "$(basename "$IPK")" > "$(basename "$IPK").sha256" )
mkdir -p "$ROOT/dist/dbg/$VER" && { cp "$ROOT/build/webos/stremioorigin.dbg" "$ROOT/dist/dbg/$VER/" 2>/dev/null || true; } # kept per version: a shared crash report names its code from it (getlogs)
ls -l "$IPK"; cat "$IPK.sha256"
