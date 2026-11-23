"""Future-kit original road sidecar exporter. Stdlib + adjacent source_data.
Requires user NFSUNDER only; constants are bundled canonical numeric tables.
No GC ELF, RAM snapshot, workstation path, renderer or mesh conversion.
"""
from pathlib import Path
import argparse,struct,json,hashlib,math,tempfile,shutil
from source_data import inspect
O=Path(__file__).parent
sha=lambda b:hashlib.sha256(b).hexdigest()
TRACKS=('L4RA','L4RB','L4RC','L4RD','L4RF','L4RG')
def constants(path=None):
 c=json.loads(Path(path or O/'road_constants.json').read_text())
 if c['version']!=1 or c['terrain_count']!=29 or c['noise_count']!=16 or len(c['terrain_coeffs'])!=29 or any(len(r)!=6 for r in c['terrain_coeffs'])or len(c['noise_grid'])!=16:raise ValueError('canonical table shape')
 values=[x for row in c['terrain_coeffs']for x in row]+c['noise_grid']
 if not all(isinstance(x,(int,float))and math.isfinite(x)for x in values):raise ValueError('canonical table numbers')
 terrain=struct.pack('<174f',*values[:174]);noise=struct.pack('<16f',*values[174:])
 if sha(terrain)!=c['terrain_numeric_sha256_LE']or sha(noise)!=c['noise_numeric_sha256_LE']:raise ValueError('canonical table identity')
 return terrain,noise,c

def project(game,tracks):
 plans=[]
 for track in tracks:
  if track not in TRACKS:raise ValueError('track not in qualified metadata/export set')
  s,lod,bounds,groups,trees,n=inspect(game,track)
  available={t['section']for t in trees};missing=[sec for sec in bounds if sec+lod not in available]
  plans.append((s,lod,bounds,groups,trees,n,{'track':track,'projected_sidecar_bytes':n,'topology_trees':len(trees),'polygons':sum(t['polygons']for t in trees),'boundaries':len(bounds),'boundary_tree_available':len(bounds)-len(missing),'boundary_tree_unavailable_sections':missing,'groups':len(groups)//32,'metadata_bytes_read':s.archive.read_bytes,'stream_logical_size':s.size}))
 return plans

def export_bundle(game,output,tracks=TRACKS,source_label='user-disc'):
 output=Path(output)
 if output.exists():raise FileExistsError('output must be a new isolated directory')
 plans=project(game,tracks);projected=sum(p[5]for p in plans)
 # Limit this isolated numeric export; larger requests require measured policy.
 if projected>=100*1024*1024:raise ValueError('projected numeric output >=100MiB; inspect projection/admit resource slot before extending exporter scope')
 terrain,noise,c=constants();output.parent.mkdir(parents=True,exist_ok=True)
 stage=Path(tempfile.mkdtemp(prefix='.'+output.name+'-',dir=output.parent));reports=[]
 try:
  for s,lod,bounds,groups,trees,n,meta in plans:
   blob=bytearray(struct.pack('<4s7I',b'N2TR',1,lod,len(groups)//32,len(trees),len(bounds),29,16));blob+=groups+terrain+noise
   for section,(box,pts)in bounds.items():blob+=struct.pack('<2I4f',section,len(pts),*box)+b''.join(struct.pack('<2f',*p)for p in pts)
   ids=[]
   for t in trees:
    pol,refs,cells=s.load_arrays(t);blob+=struct.pack('<8s6I2f',t['name'].encode(),t['section'],t['columns'],t['rows'],t['polygons'],t['references'],t['cells'],*t['origin'])+pol+refs+cells
    masked=b''.join(pol[k:k+6]+b'\0\0'+pol[k+8:k+32]for k in range(0,len(pol),32))
    ids.append({k:v for k,v in t.items()if k not in('header','leaves')}|{'header_sha256':sha(t['header']),'polygons_sha256':sha(pol),'polygons_masked_mutable_index_sha256':sha(masked),'references_sha256':sha(refs),'cells_sha256':sha(cells),'leaf_stream_ranges':[[a-s.base,z]for a,z in t['leaves']]})
   if len(blob)!=n:raise ValueError('sidecar differs from header projection')
   name=s.track_name+'.n2tr';(stage/name).write_bytes(blob)
   report=meta|{'source_label':source_label,'source_game':str(Path(game)),'ZDIR_sha256':s.archive.zdir_sha256,'track_logical_file':f'TRACKS/{s.track_name}.BUN','stream_logical_file':f'TRACKS/STREAM{s.track_name}.BUN','track_sha256':sha(s.track),'stream_copies':[{'archive':p.name,'base':base,'size':size}for p,base,size in s.copies],'topology_source_bytes_read_total':s.archive.read_bytes,'sidecar':name,'sidecar_bytes':len(blob),'sidecar_sha256':sha(blob),'LOD_offset':lod,'group_names':[groups[a+9:a+32].split(b'\0')[0].decode('ascii')for a in range(0,len(groups),32)],'trees':ids,'availability_contract':'PVS boundary without a corresponding physical tree remains recorded; runtime returns unavailable4 there. No synthetic tree/plane or beta scope decision is introduced.'}
   (stage/(s.track_name+'-source.json')).write_text(json.dumps(report,indent=2)+'\n');reports.append(report)
  result={'status':'ISOLATED_USER_SOURCE_ORIGINAL_ROAD_EXPORT_NO_EXECUTABLE_OR_RAM_DEPENDENCY','exporter_version':2,'wire_version':1,'source_label':source_label,'projected_sidecar_bytes':projected,'actual_sidecar_bytes':sum(x['sidecar_bytes']for x in reports),'canonical_terrain_sha256_LE':c['terrain_numeric_sha256_LE'],'canonical_noise_sha256_LE':c['noise_numeric_sha256_LE'],'sources':reports,'limits':['No game Core autoenable, generator/publication policy, mesh conversion or platform memory approval.','Canonical760B tables are cross-original-qualified developer constants; geometry/groups always come from the supplied user NFSUNDER.','All exported topology arrays retain original order. Some PVS boundaries do not match a tree under the current section+LOD association; availability is explicit rather than fitted.','Resident sidecars for A/D are approximately6MB each; PSP heap/streaming/performance remain integration gates.','Runtime query semantics are identical to V1; the bounded4312 comparison is not global cross-track parity.']}
  (stage/'MANIFEST.json').write_text(json.dumps(result,indent=2)+'\n');stage.rename(output);return result
 except BaseException:
  shutil.rmtree(stage);raise
if __name__=='__main__':
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--game',type=Path,required=True,help='User extracted NFSUNDER containing ZDIR.BIN/ZZDATA*.BIN');a.add_argument('--output',type=Path,required=True,help='New output directory');a.add_argument('--tracks',nargs='+',default=list(TRACKS),choices=TRACKS);a.add_argument('--source-label',default='user-disc');a.add_argument('--project-only',action='store_true');args=a.parse_args()
 if args.project_only:
  plans=project(args.game,args.tracks);r={'projected_sidecar_bytes':sum(x[5]for x in plans),'tracks':[x[6]for x in plans]}
 else:r=export_bundle(args.game,args.output,tuple(args.tracks),args.source_label);r={k:v for k,v in r.items()if k!='sources'}
 print(json.dumps(r,indent=2))
