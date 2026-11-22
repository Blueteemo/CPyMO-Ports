"""Prepare an optional font comparison; does not modify the deployed font."""
import argparse,hashlib,json
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools import subset
p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);p.add_argument('--source',type=Path,required=True);p.add_argument('--name',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
ref=TTFont(a.reference);f=TTFont(a.source,recalcTimestamp=False);needed=set(ref.getBestCmap());missing=sorted(needed-set(f.getBestCmap()));scale=f['head'].unitsPerEm/ref['head'].unitsPerEm
for field in ['ascent','descent','lineGap']:setattr(f['hhea'],field,round(getattr(ref['hhea'],field)*scale))
for field in ['sTypoAscender','sTypoDescender','sTypoLineGap','usWinAscent','usWinDescent']:setattr(f['OS/2'],field,round(getattr(ref['OS/2'],field)*scale))
opts=subset.Options();opts.recalc_timestamp=False;opts.name_IDs=['*'];opts.name_languages=['*'];sub=subset.Subsetter(options=opts);sub.populate(unicodes=needed);sub.subset(f)
for n in f['name'].names:
 if n.nameID in [1,3,4,6,16]:n.string=('CPyMOPreview'+a.name).encode(n.getEncoding())
if 'CFF ' in f:
 cff=f['CFF '].cff;cff.fontNames=['CPyMOPreview'+a.name];cff.topDictIndex[0].FullName='CPyMOPreview'+a.name;cff.topDictIndex[0].FamilyName='CPyMOPreview'+a.name
f.save(a.output)
print(json.dumps({'source_sha256':hashlib.sha256(a.source.read_bytes()).hexdigest(),'preview_sha256':hashlib.sha256(a.output.read_bytes()).hexdigest(),'reference_cmap_missing':[f'U+{c:04X}'for c in missing]},indent=2))
