import sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from preflight_game import preflight
with tempfile.TemporaryDirectory(prefix='rg35xx 中文 path ') as t:
    d=Path(t); (d/'system').mkdir();(d/'script').mkdir()
    errs,warns=preflight(d)
    assert any('gameconfig' in e for e in errs),errs
    (d/'gameconfig.txt').write_text('imagesize,320,240\nstartscript,start\n',encoding='utf8')
    errs,warns=preflight(d)
    assert any('script/start.txt' in e for e in errs),errs
    (d/'script/start.txt').write_text('#say test\n',encoding='utf8')
    errs,warns=preflight(d)
    assert any('default.ttf' in e for e in errs),errs
    (d/'system/default.ttf').write_bytes(b'fake-for-layout-check')
    errs,warns=preflight(d)
    assert not errs and any('save/' in w for w in warns),(errs,warns)
    (d/'save').mkdir()
    errs,warns=preflight(d)
    assert not errs,(errs,warns)
    (d/'gameconfig.txt').write_text('imagesize,invalid,xx\nstartscript,start\n')
    errs,_=preflight(d)
    assert any('imagesize' in e for e in errs),errs
    (d/'gameconfig.txt').write_text('imagesize,1024,768\nstartscript,start\n')
    errs,warns=preflight(d)
    assert not errs and any('exceeds' in w for w in warns),(errs,warns)
print('PASS: game asset preflight: config, start script, font, UTF-8 path, save, invalid dims, excessive dims')
