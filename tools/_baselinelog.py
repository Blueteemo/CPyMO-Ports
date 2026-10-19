#!/usr/bin/env python3
"""Is the v0.23 baseline device log inside the handoff?"""
import subprocess
import zipfile
from pathlib import Path

ROOT = Path("/c/Users/DHJ/Downloads/CPyMO_RG35XX_LocalResearch_v0.21R1")
if not ROOT.is_dir():
    ROOT = Path(r"C:\Users\DHJ\Downloads\CPyMO_RG35XX_LocalResearch_v0.21R1")

print("=== what 10.19* entries are in the v023 zip? ===")
z = zipfile.ZipFile(ROOT / "dist" / "CPyMO_v023_review.zip")
hits = [n for n in z.namelist() if "10.19" in n]
for h in hits:
    print("   ", h)
print("total:", len(hits))

print()
print("=== what is on disk under that folder? ===")
for p in sorted((ROOT / "10.19开发任务").rglob("*")):
    if p.is_file():
        print(f"   {p.relative_to(ROOT).as_posix()}")

print()
print("=== tracked? ===")
GIT = r"C:\Program Files\Git\cmd\git.exe"
r = subprocess.run([GIT, "ls-files", "10.19开发任务"], cwd=ROOT,
                   capture_output=True, text=True, encoding="utf-8")
print(r.stdout.strip() or "(nothing tracked)")

print()
print("=== in the root manifest? ===")
text = (ROOT / "SHA256SUMS").read_text(encoding="utf-8")
for line in text.splitlines():
    if "10.19" in line:
        print("   ", line.split(" *", 1)[1])
