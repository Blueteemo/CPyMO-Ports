#!/usr/bin/env python3
"""Read-only SISX payload audit against a predeclared expected install list.
Narrow parser for this no-embedded/no-condition package. Field numbers/layout
follow the recovered GnuPoc makesis-4 sisfield.h, not filesystem enumeration.
"""
from pathlib import Path
import json,struct,zlib,hashlib,sys
from startup_asset import checked_startup, verify_embedded_startup
canonical_startup = checked_startup()
embedded_startup_sha256 = None
R=Path(sys.argv[1]).resolve()
expected=json.loads((R/'EXPECTED_PAYLOAD.json').read_text())
p=R/(sys.argv[2] if len(sys.argv)>2 else 'CPyMO_S60v3_1.1.1.sisx');b=p.read_bytes()
def fields(data):
 out=[];o=0
 while o<len(data):
  assert o+8<=len(data),'Truncated field header'
  t,n=struct.unpack_from('<II',data,o);assert n<0x80000000,'Large field unsupported by bounded audit'
  end=o+8+n;assert end<=len(data),'Truncated field payload'
  out.append((t,data[o+8:end]));o=end+((-n)&3)
 assert o==len(data)
 return out
def only(data,t):
 a=fields(data);assert len(a)==1 and a[0][0]==t,(t,[x[0] for x in a]);return a[0][1]
def arr(data,t):
 assert struct.unpack_from('<I',data)[0]==t
 out=[];o=4
 while o<len(data):
  n=struct.unpack_from('<I',data,o)[0];assert n<0x80000000
  end=o+4+n;assert end<=len(data);out.append(data[o+4:end]);o=end+((-n)&3)
 assert o==len(data)
 return out
def decompress(v):
 algorithm,n=struct.unpack_from('<IQ',v);raw=v[12:]
 if algorithm==1:raw=zlib.decompress(raw)
 else:assert algorithm==0
 assert len(raw)==n
 return raw
contents=only(b[16:],12);children=dict(fields(contents));controller=only(decompress(children[3]),13)
controller_fields=dict(fields(controller));install=fields(controller_fields[28]);assert len(install)==3 and all(x[0]==2 for x in install)
records=arr(install[0][1],24);assert arr(install[1][1],13)==[],'Embedded SIS present';assert arr(install[2][1],26)==[],'Conditional operations present'
data_units=arr(only(children[30],2),31);assert len(data_units)==1
file_blobs=arr(only(data_units[0],2),32)
assert len(records)==len(expected['files'])==18
assert len(file_blobs)==len(expected['files'])
expected_by_dest={x['destination']:x for x in expected['files']};seen=set();rows=[]
for rec in records:
 tags=fields(rec[:-28]);assert tags[0][0]==1
 dest=tags[0][1].decode('utf-16le');op,options,compressedlen,uncompressedlen,index=struct.unpack('<IIQQI',rec[-28:])
 assert op==1 and options==0,'Run/Text/Null/FR or unknown operation'
 assert dest in expected_by_dest and dest not in seen,dest
 seen.add(dest);wanted=expected_by_dest[dest]
 payload=decompress(only(file_blobs[index],3));digest=hashlib.sha256(payload).hexdigest()
 assert len(payload)==uncompressedlen==wanted['bytes'],dest
 assert digest==wanted['sha256'],dest
 if dest.lower().endswith('.exe'):
  assert embedded_startup_sha256 is None, 'Unexpected second executable'
  embedded_startup_sha256=verify_embedded_startup(payload,canonical_startup)
 assert '/python/' not in dest.replace('\\','/').lower(),dest
 rows.append({'destination':dest,'operation':op,'options':options,'data_index':index,'bytes':len(payload),'sha256':digest,'matches_predeclared_expected':True})
assert seen==set(expected_by_dest)
assert embedded_startup_sha256 is not None, 'No packaged EXE to verify'
info_fields=fields(controller_fields[14][:-2]);uid=struct.unpack('<I',info_fields[0][1])[0];assert uid==int(expected['uid'],16)
version=struct.unpack_from('<3i', dict(info_fields)[4]);assert '.'.join(map(str,version))==expected['version'],version
result={'scope':'Static parser audit of actual signed SISX bytes; not physical-phone SWI/signature/uninstall execution','sisx_file':p.name,'sisx_sha256':hashlib.sha256(b).hexdigest(),'sisx_bytes':len(b),'uid':hex(uid),'version':'.'.join(map(str,version)),'expected_payload_count':len(expected['files']),'actual_payload_count':len(rows),'all_expected_payloads_match':True,'embedded_startup_sha256':embedded_startup_sha256,'only_install_operations':True,'embedded_packages':0,'conditional_blocks':0,'original_game_roots_owned_by_package':False,'files':rows}
(R/'PACKED_PAYLOAD_AUDIT.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='files'},indent=2))
