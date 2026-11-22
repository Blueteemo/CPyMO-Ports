#!/usr/bin/env python3
"""Build the chosen Regular subset, preserving the validated em metrics/cmap."""
import argparse,hashlib,json
from fontTools.ttLib import TTFont
from fontTools import subset
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
f=TTFont(a.source,recalcTimestamp=False);ref=TTFont(a.reference);needed=set(ref.getBestCmap());assert not needed-set(f.getBestCmap())
scale=f['head'].unitsPerEm/ref['head'].unitsPerEm
for field in ['ascent','descent','lineGap']:setattr(f['hhea'],field,round(getattr(ref['hhea'],field)*scale))
for field in ['sTypoAscender','sTypoDescender','sTypoLineGap','usWinAscent','usWinDescent']:setattr(f['OS/2'],field,round(getattr(ref['OS/2'],field)*scale))
opts=subset.Options();opts.recalc_timestamp=False;opts.name_IDs=['*'];opts.name_languages=['*'];s=subset.Subsetter(options=opts);s.populate(unicodes=needed);s.subset(f)
names={1:'CPyMO CJK Regular',2:'Regular',3:'CPyMO-CJK-Regular-2.005-RG35XX0245',4:'CPyMO CJK Regular',5:'Version 2.005; CPyMO RG35XX v0.24.5 Regular subset',6:'CPyMOCJK-Regular',16:'CPyMO CJK Regular',17:'Regular'}
for n in f['name'].names:
 if n.nameID in names:n.string=names[n.nameID].encode(n.getEncoding())
if 'CFF ' in f:
 cff=f['CFF '].cff;cff.fontNames=['CPyMOCJK-Regular'];cff.topDictIndex[0].FullName='CPyMO CJK Regular';cff.topDictIndex[0].FamilyName='CPyMO CJK Regular'
 for i,fd in enumerate(getattr(cff.topDictIndex[0],'FDArray',[])):
  if hasattr(fd,'FontName'):fd.FontName=f'CPyMOCJK-Regular-FD{i}'
f.save(a.output);check=TTFont(a.output);assert set(check.getBestCmap())==needed
receipt={'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'reference_sha256':hashlib.sha256(a.reference.read_bytes()).hexdigest(),'output_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'cmap_codepoints':len(needed),'metrics':'normalized to previous Medium em, proportional to unitsPerEm','layout':'native Regular GSUB/GPOS subset; no tables copied between faces','reserved_names_removed':True}
a.output.with_suffix('.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
