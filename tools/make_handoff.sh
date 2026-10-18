#!/usr/bin/env bash
# Build the return package for the reviewer, in the ONE order that verifies.
#
#   bash tools/make_handoff.sh
#
# Why a wrapper exists
# ------------------
# Three files must agree, and they are written in a specific order:
#
#   dist/MANIFEST.sha256   every packaged file's digest (travels inside the zip)
#   dist/SHA256SUMS         the zip's and the bundle's digests (for the sender)
#   SHA256SUMS              the whole TREE (tracked files), which INCLUDES the
#                           two dist/ artifacts
#
# So the tree manifest hashes the ZIP, and the zip must exist first. Run the
# other way round and `sha256sum -c SHA256SUMS` reports the (older) zip as
# FAILED, which reads like a damaged package. This script fixes the order and
# then verifies all three, so "it verifies" is a fact and not a recollection.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
export LC_ALL=C

echo "[1/6] the package (its digests are inputs to the tree manifest)"
python3 tools/make_return_package.py | tail -4

echo "[2/6] the tree manifest (it hashes the zip, so it runs AFTER)"
python3 tools/make_tree_manifest.py | tail -1

echo "[3/6] in-zip manifest, read back from the archive"
python3 tools/verify_package.py | tail -2

echo "[4/6] zip vs working tree (freshness + manifest line endings)"
python3 tools/check_package_freshness.py | tail -2

echo "[5/6] do the README's numbers match this package?"
python3 tests/test_readme_claims.py

echo "[6/6] GNU sha256sum -c, the way a reviewer runs it"
( cd dist && sha256sum -c SHA256SUMS ) 2>&1 | sed 's/^/   /'
ENTRIES="$(wc -l < SHA256SUMS)"
sha256sum -c SHA256SUMS >/dev/null 2>&1 \
  && echo "   root SHA256SUMS: ${ENTRIES} entries OK" \
  || { echo "   root SHA256SUMS FAILED" >&2; exit 1; }

# An unpacked copy of the archive, checked with the manifest that travels in
# it: this is what a reviewer does, and it is the one check the in-zip reader
# cannot stand in for.
TMP="$(mktemp -d)"
python3 - "$TMP" <<'PY'
import sys, zipfile
with zipfile.ZipFile("dist/CPyMO_v023_review.zip") as z:
    z.extractall(sys.argv[1])
PY
( cd "$TMP" && sha256sum -c MANIFEST.sha256 >/dev/null 2>&1 ) \
  && echo "   unpacked MANIFEST.sha256: OK" \
  || { echo "   unpacked MANIFEST.sha256 FAILED" >&2; exit 1; }
rm -rf "$TMP"

echo
echo "READY: send dist/$(ls -1 dist | grep -E '_review\.zip$' | sort | tail -1)"
