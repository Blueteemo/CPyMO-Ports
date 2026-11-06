#!/usr/bin/env python3
"""Extend the stable font's coverage from the same upstream face/version."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--game', type=Path, action='append', required=True)
p.add_argument('--output', type=Path, required=True)
args = p.parse_args()
if args.output.exists():
    p.error('Output must be a new file')
reference = TTFont(args.reference)
font = TTFont(args.source, recalcTimestamp=False)
if font['head'].unitsPerEm != reference['head'].unitsPerEm:
    raise ValueError('Upstream unitsPerEm does not match the stable font')
# The validated handheld font normalizes its vertical em metrics. Preserve
# those exact metrics; using the untouched upstream hhea would shrink every
# existing glyph despite keeping the nominal font-size setting unchanged.
for field in ['ascent', 'descent', 'lineGap']:
    setattr(font['hhea'], field, getattr(reference['hhea'], field))
for field in ['sTypoAscender', 'sTypoDescender', 'sTypoLineGap', 'usWinAscent', 'usWinDescent']:
    setattr(font['OS/2'], field, getattr(reference['OS/2'], field))
old = set(reference.getBestCmap())
needed = set(range(32, 127)) | old
needed.update(map(ord, '返回游戏列表未保存的进度将丢失已读变灰开关游戏启动或运行失败退出CPyMO↑↓'))
for game in args.game:
    for path in [game / 'gameconfig.txt', *sorted((game / 'script').glob('*.txt'))]:
        needed.update(ord(c) for c in path.read_text(encoding='utf-8-sig') if ord(c) >= 32 and not c.isspace())
missing = needed - set(font.getBestCmap())
if missing:
    raise ValueError('Upstream face lacks required characters: ' + ''.join(map(chr, sorted(missing))))
options = subset.Options()
options.recalc_timestamp = False
options.name_IDs = ['*']
options.name_languages = ['*']
options.name_legacy = True
# Limit layout closure to the stable font's policy, rather than pulling in
# all upstream Japanese/Korean/vertical alternates for the same codepoints.
for tag in ['GPOS', 'GSUB', 'kern']:
    if tag in reference:
        font[tag] = copy.deepcopy(reference[tag])
    elif tag in font:
        del font[tag]
options.layout_features = ['*']
subsetter = subset.Subsetter(options=options)
subsetter.populate(unicodes=needed, glyphs=reference.getGlyphOrder())
subsetter.subset(font)
if not set(reference.getGlyphOrder()) <= set(font.getGlyphOrder()):
    raise ValueError('Reference layout glyphs were not retained')
# Keep the validated kerning policy as well as individual glyphs. Rebuilding
# GPOS from default upstream subset options can change Latin pair spacing.
for tag in ['GPOS', 'kern']:
    if tag in reference:
        font[tag] = copy.deepcopy(reference[tag])
    elif tag in font:
        del font[tag]
# The OFL reserves "Source". Rename derivative font identity while retaining
# upstream copyright, license and identical glyph outlines/weight.
names = {1: 'CPyMO CJK Medium', 2: 'Regular', 3: 'CPyMO-CJK-Medium-2.005-RG35XX0244',
         4: 'CPyMO CJK Medium', 5: 'Version 2.005; CPyMO RG35XX v0.24.4 coverage subset',
         6: 'CPyMOCJK-Medium', 16: 'CPyMO CJK Medium', 17: 'Regular'}
for record in font['name'].names:
    if record.nameID in names:
        record.string = names[record.nameID].encode(record.getEncoding())
if 'CFF ' in font:
    cff = font['CFF '].cff
    cff.fontNames = ['CPyMOCJK-Medium']
    cff.topDictIndex[0].FullName = 'CPyMO CJK Medium'
    cff.topDictIndex[0].FamilyName = 'CPyMO CJK Medium'
    for index, fd in enumerate(getattr(cff.topDictIndex[0], 'FDArray', [])):
        if hasattr(fd, 'FontName'):
            fd.FontName = f'CPyMOCJK-Medium-FD{index}'
font.save(args.output)
check = TTFont(args.output)
assert needed <= set(check.getBestCmap())
receipt = {'reference_sha256': hashlib.sha256(args.reference.read_bytes()).hexdigest(),
           'upstream_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
           'output_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
           'reference_codepoints': len(old), 'output_codepoints': len(check.getBestCmap()),
           'added_codepoints': [f'U+{n:04X}' for n in sorted(set(check.getBestCmap())-old)],
           'required_missing': [], 'status': 'coverage verified; raster parity checked separately'}
args.output.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
