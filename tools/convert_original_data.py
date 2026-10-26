#!/usr/bin/env python3
"""Produce three original physics source sidecars from the user's NFSUNDER.
No city conversion, source executable/RAM, Core enablement or install step.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,tempfile,sys
sys.dont_write_bytecode=True
from export_physics import export as export_physics
from export_road import export_bundle
ROOT=Path(__file__).resolve().parents[1]
OUTPUTS=('original-physics.n2og','road-L4RA.n2tr','road-L4RC.n2tr')
def produce(game,out):
 out=Path(out);out.mkdir(parents=True,exist_ok=True)
 stage=Path(tempfile.mkdtemp(prefix='.original-source-',dir=out.parent))
 try:
  physics=export_physics(game,stage/OUTPUTS[0])
  road=export_bundle(game,stage/'topology',tracks=('L4RA','L4RC'),source_label='user-NFSUNDER')
  for track in('L4RA','L4RC'):
   (stage/'topology'/f'{track}.n2tr').replace(stage/f'road-{track}.n2tr')
  files={name:{'bytes':(stage/name).stat().st_size,'sha256':hashlib.sha256((stage/name).read_bytes()).hexdigest()}for name in OUTPUTS}
  # Publish only complete source products. Re-running replaces these three
  # deterministic files, never clears output/saves or any other product.
  for name in OUTPUTS:os.replace(stage/name,out/name)
  return {'status':'ORIGINAL_SOURCE_DATA_PRODUCED_NOT_GAMEPLAY_APPROVAL','files':files,'source_GLOBALB_sha256':physics['source_GLOBALB_sha256'],'road_wire_version':road['wire_version'],'Core_enabled_by_producer':False}
 finally:
  shutil.rmtree(stage)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',required=True,type=Path,help='Extracted user NFSUNDER');p.add_argument('--out',type=Path,default=ROOT/'dist/PSP/GAME/NFSU2VIEW');a=p.parse_args();r=produce(a.game,a.out);print(json.dumps(r,indent=2))
if __name__=='__main__':main()
