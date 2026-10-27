"""Small deterministic N2OG source-data export; no typed C ABI serialization.
Only user NFSUNDER GLOBALB + adjacent canonical NOS constants are required.
"""
from pathlib import Path
import argparse,struct,json,hashlib,tempfile,os
from source_data import Archive,chunks
O=Path(__file__).parent;RAW=0x890;NAME=24;HEADER=64
NAMES=('PEUGOT','FOCUS','COROLLA','240SX','MIATA','CIVIC','HUMMER','NAVIGATOR','ESCALADE','TIBURON','SENTRA','CELICA','IS300','SUPRA','GOLF','A3','RSX','ECLIPSE','TT','RX8','350Z','G35','3000GT','GTO','MUSTANGGT','SKYLINE','LANCEREVO8','RX7','IMPREZAWRX')
sha=lambda b:hashlib.sha256(b).hexdigest()
def build(game):
 archive=Archive(game);globalb=archive.logical('GLOBAL/GLOBALB.BUN');tables=[globalb[p:p+n]for t,p,n in chunks(globalb)if t==0x34600];packages=[globalb[p:p+n]for t,p,n in chunks(globalb)if t==0x34a15]
 if len(tables)!=1 or tables[0][:8]!=b'\x11'*8 or(len(tables[0])-8)%RAW or len(packages)!=1 or len(packages[0])!=30*0x17c:raise ValueError('original numeric source table shape')
 table=tables[0][8:];records={}
 for at in range(0,len(table),RAW):
  raw=table[at:at+RAW];name=raw[:32].split(b'\0')[0].decode('ascii')
  if name in records:raise ValueError('duplicate original model')
  records[name]=raw
 by_id=sorted((struct.unpack_from('<I',raw,0x840)[0],name)for name,raw in records.items()if struct.unpack_from('<I',raw,0x840)[0]<=30 and name not in('CORSA','PEUGOT106'))
 if tuple(n for i,n in by_id)!=NAMES:raise ValueError('source does not contain the qualified29US player models in source order')
 constants=json.loads((O/'nitro_constants.json').read_text());curve=struct.pack('<9f',*constants['NOSCurve']);levels=struct.pack('<3f',*constants['GameBrakerNosTorqueMultiplier']);nitro=curve+levels
 if sha(curve)!=constants['curve_sha256_LE']or sha(levels)!=constants['levels_sha256_LE']:raise ValueError('canonical Nitro identity')
 recordoff=HEADER;regoff=recordoff+len(NAMES)*(NAME+RAW);nosoff=regoff+len(packages[0]);total=nosoff+len(nitro)
 blob=bytearray(struct.pack('<4s15I',b'N2OG',1,0x01020304,HEADER,len(NAMES),NAME,RAW,30,0x17c,9,3,recordoff,regoff,nosoff,total,0));rows=[]
 for name in NAMES:
  raw=records[name];blob+=name.encode().ljust(NAME,b'\0')+raw;rows.append({'model':name,'original_model_id':struct.unpack_from('<I',raw,0x840)[0],'record_sha256':sha(raw)})
 blob+=packages[0]+nitro
 if len(blob)!=total:raise ValueError('N2OG projected layout differs')
 return bytes(blob),{'status':'ISOLATED_RAW_LE_ORIGINAL_PHYSICS_ASSET_NOT_GAMEPLAY_ENABLED','magic':'N2OG','wire_version':1,'source_game':str(Path(game)),'source_GLOBALB_sha256':sha(globalb),'models':rows,'registry_section':'34a15','registry_bytes':len(packages[0]),'registry_sha256':sha(packages[0]),'NOS_numeric_bytes':len(nitro),'asset_bytes':len(blob),'asset_sha256':sha(blob),'offsets':{'records':recordoff,'registry':regoff,'NOS':nosoff},'dependencies':['user NFSUNDER/ZDIR.BIN+ZZDATA*.BIN','export_physics.py','source_data.py','nitro_constants.json'],'no_C_ABI_structs_written':True,'no_runtime_executable_or_RAM_dependency':True,'limits':['This is a new source-data sidecar, not the existing parts-*.n2op format. No production/gameplay/kit references are changed.','Raw source and registry are immutable; dynamic CarPerf composition/classification/mode/Nitro finalization happen after lookup, never by caching a derived stock OGProfile.','Canonical source fleet is29US player models only; unknown/opponent names return unavailable rather than silently borrowing another model.']}
def export(game,output):
 output=Path(output)
 if output.exists():raise FileExistsError('new output file required')
 blob,manifest=build(game);output.parent.mkdir(parents=True,exist_ok=True)
 fd,name=tempfile.mkstemp(prefix='.'+output.name+'-',dir=output.parent)
 try:
  with os.fdopen(fd,'wb')as f:f.write(blob)
  os.replace(name,output)
 except BaseException:
  Path(name).unlink(missing_ok=True);raise
 output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();r=export(a.game,a.output);print(json.dumps({k:v for k,v in r.items()if k not in('models','limits')},indent=2))
