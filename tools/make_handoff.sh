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

# The chicken-and-egg here is real but shallow. The README states three
# counts, and two of them are properties of the package's FILE SET:
#
#   * the zip's entry count and MANIFEST coverage  -> the packaged file set
#   * the root manifest's LINE count               -> `git ls-files`
#
# None of the three depends on the README's own BYTES, so the counts can be
# settled exactly: package once so dist/ exists, bring the root manifest up to
# date (its line count is final once the file set is), write the README, then
# package the final archive from the same file set. A final --check proves it
# converged instead of asserting it.
echo "[1/9] a bootstrap package (so dist/ exists and the file set is known)"
python3 tools/make_return_package.py | tail -2

echo "[2/9] the tree manifest, up to date for that file set"
python3 tools/make_tree_manifest.py | tail -1

echo "[3/9] sync the README's three counts to the final numbers"
python3 tools/sync_readme_counts.py | tail -1

echo "[4/9] the final package (same file set; counts still hold)"
python3 tools/make_return_package.py | tail -4

echo "[5/9] the tree manifest again (the zip digest it records changed)"
python3 tools/make_tree_manifest.py | tail -1

echo "[6/9] in-zip manifest, read back from the archive"
python3 tools/verify_package.py | tail -2

echo "[7/9] zip vs the whole tree"
python3 tools/check_package_freshness.py | tail -2
python3 tests/test_manifest_coverage.py | tail -1

echo "[8/9] the README converged? the bundle carries the source?"
python3 tools/sync_readme_counts.py --check | tail -1
python3 tests/test_readme_claims.py | tail -1
python3 tests/test_bundle_covers_source.py | tail -1

echo "[9/9] GNU sha256sum -c, the way a reviewer runs it"
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
echo "NOTE: the ZIP is a fixed point of the file tree -- it carries no git"
echo "      metadata -- so its digest survives a later commit. The BUNDLE"
echo "      embeds the branch tip and HEAD, so its digest is tied to the"
echo "      commit it was built from; commit these two lines together with"
echo "      the sources. A LATER commit changes the bundle digest."
echo "READY: send dist/$(ls -1 dist | grep -E '_review\.zip$' | sort | tail -1)"
