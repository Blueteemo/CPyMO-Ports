#!/usr/bin/env python3
"""Rebuild the packaged outline subset from the pinned upstream release ZIP."""
from io import BytesIO
from pathlib import Path
import hashlib
import sys
from zipfile import ZipFile

from fontTools import subset

archive = Path(sys.argv[1])
output = Path(__file__).resolve().parents[1] / "assets" / "fonts"
with archive.open("rb") as stream:
    assert hashlib.file_digest(stream, "sha256").hexdigest() == (
        "2d13900c20b57a01c00eea13b1f050a96a3392d1db7fbf2b1b6b8aee59ca7859"
    )
with ZipFile(archive) as release:
    data = release.read("fusion-pixel-12px-monospaced-zh_hans.ttf")
font = subset.load_font(BytesIO(data), subset.Options())
points = [int(item[2:], 16) for item in (output / "story-codepoints.txt").read_text().split()]
options = subset.Options()
options.hinting = False
options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 13, 14, 16, 17]
builder = subset.Subsetter(options)
builder.populate(unicodes=points)
builder.subset(font)
labels = {1: "CPyMO Story Pixel", 3: "CPyMO Story Pixel 12SC v1",
          4: "CPyMO Story Pixel Regular", 6: "CPyMOStoryPixel-Regular",
          16: "CPyMO Story Pixel"}
for record in list(font["name"].names):
    if record.nameID in labels:
        font["name"].setName(labels[record.nameID], record.nameID,
                             record.platformID, record.platEncID, record.langID)
font.save(output / "story-fusion-12.ttf")
assert len(font.getBestCmap()) == len(points) == 7600
