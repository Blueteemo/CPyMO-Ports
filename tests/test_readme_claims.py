#!/usr/bin/env python3
"""Cross-check the numbers README_FOR_REVIEWER.md claims against the artefacts.

A hand-written summary drifts; this makes the drift a checkable failure. It
reads the claims OUT of the README and compares them with the zip and
dist/SHA256SUMS, so editing one without the other fails here.
"""
from __future__ import annotations

import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STALE = "STALE"


def main(argv: list[str]) -> int:
    version = argv[1] if len(argv) > 1 else "v023"
    zpath = ROOT / "dist" / f"CPyMO_{version}_review.zip"
    bpath = ROOT / "dist" / f"CPyMO_{version}_history.bundle"
    readme = ROOT / "README_FOR_REVIEWER.md"
    if not zpath.is_file():
        # A clone without dist/ (it is gitignored) has nothing to compare
        # against; that is a SKIP, not a failure, but it says so.
        if not readme.is_file():
            print(f"SKIP: neither {zpath} nor {readme} exists")
            return 0
        print(f"SKIP: no package at {zpath}; "
              f"build one with tools/make_handoff.sh")
        return 0
    if not readme.is_file():
        print(f"missing {readme}", file=sys.stderr)
        return 2

    text = readme.read_text(encoding="utf-8")
    with zipfile.ZipFile(zpath) as z:
        names = z.namelist()
        entries = len(names)
        files = sum(1 for n in names if not n.endswith("/"))
        covered = len(z.read("MANIFEST.sha256").decode("utf-8").splitlines())

    zip_sha = hashlib.sha256(zpath.read_bytes()).hexdigest()
    bundle_sha = (hashlib.sha256(bpath.read_bytes()).hexdigest()
                  if bpath.is_file() else None)
    zip_mb = zpath.stat().st_size / 1e6

    failures = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal failures
        print(("  ok    " if cond else "  FAIL  ") + msg)
        if not cond:
            failures += 1

    print(f"actual: entries={entries} files={files} manifest covers={covered}")
    print(f"actual: zip={zip_mb:.2f} MB  {zip_sha[:16]}...")
    if bundle_sha:
        print(f"actual: bundle={bpath.stat().st_size/1e6:.2f} MB  "
              f"{bundle_sha[:16]}...")

    # The counts change whenever the packaged FILE SET changes (a new test is
    # added and the same sentence in the README goes stale). When it does,
    # print the fix command AND the exact sentences, so the correction is a
    # replacement and not a re-derivation.
    expected_entry = f"zip 里 **{entries} 个条目**"
    expected_cover = f"`MANIFEST.sha256` 覆盖其中 **{covered}** 条"
    if f"**{entries} 个条目**" not in text or \
       f"**{covered}** 条" not in text:
        print()
        print("  --- run: python3 tools/sync_readme_counts.py ---")
        print(f"  {expected_entry}（没有目录条目，条目即文件），")
        print(f"  {expected_cover} —— 也就是除它自己以外的**每一个**")
        print("  ---")

    check(f"{entries} 个条目" in text, f"README states the entry count ({entries})")
    check(str(covered) in text or f"{covered} 条" in text,
          f"README states the manifest coverage ({covered})")

    # The README deliberately does NOT quote the zip's digest: it is INSIDE
    # the zip, so quoting it would move the value it quotes (a fixed-point
    # problem). It must name dist/SHA256SUMS as the authority instead, and
    # quote only a digest that packaging cannot change -- the candidate core's.
    check("dist/SHA256SUMS" in text,
          "the README points at dist/SHA256SUMS as the authority")
    # ...and, for the same reason, must NOT quote the zip's own digest:
    # writing it down changes it.
    check(zip_sha not in text,
          "the README does not quote the zip's digest (which would move)")
    core = (ROOT / "research/candidate_v023_soft/APPS/"
            "CPyMO_RG35XX_uClibc_Test/cpymo_libretro.so")
    if core.is_file():
        core_sha = hashlib.sha256(core.read_bytes()).hexdigest()
        check(core_sha in text,
              f"the README quotes the candidate core digest ({core_sha[:16]}...)")
    # The bundle IS a fixed point (it covers git history, not the tree), so
    # quoting it is allowed; if quoted, it must be right.
    if bundle_sha and re.search(r"\b" + bundle_sha[:16], text):
        check(bundle_sha in text, "the README's bundle digest is current")

    # The README's ORDER of the fixes: it must not claim the zip is a fixed
    # point without the timestamp fix being in the packager.
    packer = (ROOT / "tools" / "make_return_package.py").read_text(
        encoding="utf-8")
    if "固定时间戳" in text or "fixed date" in text or "mtime" in text:
        check("date_time" in packer or "ZipInfo" in packer,
              "the 'fixed timestamp' claim matches the packager's code")

    # The failing-definition trap: `543 行只对应 542 个文件名` and
    # `542 entries, 0 failures` are about the ROOT manifest, not the zip.
    root_manifest = (ROOT / "SHA256SUMS")
    root_lines = (0 if not root_manifest.is_file()
                  else len(root_manifest.read_text(
                      encoding="utf-8").splitlines()))
    known = {entries, files, covered, root_lines}

    # It must also not claim counts that the archive does not have: a stale
    # "515 个文件" survived one rebuild and read as truth.
    for n in set(re.findall(r"(\d{3}) 个(?:条目|文件|行)", text)
                 + re.findall(r"->\s+(\d{3}) entries", text)):
        check(int(n) in known,
              f"the README's {n} matches the artefacts "
              f"(zip entries={entries}, covered={covered}, "
              f"root manifest={root_lines})")

    # The README's ORDER of the README: it claims dist/SHA256SUMS is the
    # authority, which is only true because the HANDOFF script writes the
    # package before the tree manifest. Check the script says the same.
    handoff = (ROOT / "tools" / "make_handoff.sh").read_text(encoding="utf-8")
    check(handoff.index("make_return_package.py")
          < handoff.index("make_tree_manifest.py"),
          "make_handoff.sh packages BEFORE it writes the tree manifest")

    claimed = re.search(r"~\s*([\d.]+)\s*MB[^\n]*review", text) or \
        re.search(r"review\.zip[^\n]*~\s*([\d.]+)\s*MB", text)
    if claimed:
        want = float(claimed.group(1))
        check(abs(want - zip_mb) < 0.05,
              f"README's size for the zip matches ({want} vs {zip_mb:.2f} MB)")

    print()
    print(f"{'FAIL' if failures else 'PASS'} readme-numeric-claims "
          f"({failures} mismatch)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
