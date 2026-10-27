"""Standalone, strict little-endian NFSUNDER topology source reader.
Stdlib only; opens user ZDIR/ZZDATA ranges, never game executables or RAM.
Only original numeric topology leaves are read, not render meshes/textures.
"""
from pathlib import Path
import struct,hashlib,math

def game_hash(name):
 h=0xffffffff
 for c in name.upper().replace('/','\\').encode('ascii'):h=(h*33+c)&0xffffffff
 return h

def chunks(data,start=0,end=None,depth=0):
 end=len(data)if end is None else end
 if depth>16 or not 0<=start<=end<=len(data):raise ValueError('chunk bounds/depth')
 while start+8<=end:
  tag,n=struct.unpack_from('<II',data,start);p=start+8
  if n>end-p:raise ValueError('chunk outside container')
  yield tag,p,n
  if tag&0x80000000:yield from chunks(data,p,p+n,depth+1)
  start=p+n
 if start!=end:raise ValueError('partial chunk header')

class Archive:
 def __init__(self,game):
  self.game=Path(game);z=(self.game/'ZDIR.BIN').read_bytes()
  if len(z)%24:raise ValueError('ZDIR length')
  self.entries=list(struct.iter_unpack('<6I',z));self.zdir_sha256=hashlib.sha256(z).hexdigest();self.read_bytes=0
 def ranges(self,name):
  rows=[e for e in self.entries if e[0]==game_hash(name)]
  if not rows:raise ValueError('missing source logical file '+name)
  result=[]
  for _,archive,sector,_,size,_ in rows:
   if archive>9:raise ValueError('archive index')
   path=self.game/f'ZZDATA{archive}.BIN';base=sector*2048
   if base+size>path.stat().st_size:raise ValueError('source logical range outside archive')
   result.append((path,base,size))
  return result
 def read(self,path,at,n):
  with path.open('rb')as f:f.seek(at);b=f.read(n)
  if len(b)!=n:raise ValueError('short archive read')
  self.read_bytes+=n;return b
 def logical(self,name):
  result=None
  for path,base,n in self.ranges(name):
   if n>32*1024*1024:raise ValueError('unexpected track metadata size')
   b=self.read(path,base,n)
   if result is not None and result!=b:raise ValueError('different duplicate metadata copies')
   result=b
  return result

class Stream:
 def __init__(self,game,track):
  self.archive=Archive(game);self.track_name=track;self.track=self.archive.logical(f'TRACKS/{track}.BUN')
  tables=[self.track[p:p+n]for tag,p,n in chunks(self.track)if tag==0x34110]
  if len(tables)!=1 or len(tables[0])%80:raise ValueError('stream table shape')
  self.sections={}
  for at in range(0,len(tables[0]),80):
   row=tables[0][at:at+80];name=row[:8].split(b'\0')[0].decode('ascii');kind,off,n=struct.unpack_from('<3I',row,0x10)
   if kind==1:
    if name in self.sections:raise ValueError('duplicate stream section name')
    self.sections[name]=(off,n)
  # Multiple streaming copies have the same logical layout. Read first source
  # extent, record all extents; no huge stream copy or hashing pass is needed.
  self.copies=self.archive.ranges(f'TRACKS/STREAM{track}.BUN');self.path,self.base,self.size=self.copies[0]
  for off,n in self.sections.values():
   if off+n>self.size:raise ValueError('section outside stream')
 def topologies(self):
  result=[]
  with self.path.open('rb')as f:
   def read(at,n):
    f.seek(at);b=f.read(n)
    if len(b)!=n:raise ValueError('short topology header read')
    self.archive.read_bytes+=n;return b
   for name,(off,size)in self.sections.items():
    at=self.base+off;end=at+size
    # On all qualified streams TopologyTree container is a top-level chunk.
    # Other containers (render/scenery) are skipped as opaque numeric ranges.
    while at+8<=end:
     tag,n=struct.unpack('<II',read(at,8));p=at+8
     if n>end-p:raise ValueError('stream chunk outside section')
     if tag==0x80034130:
      sub={};q=p
      while q+8<=p+n:
       t,z=struct.unpack('<II',read(q,8));a=q+8
       if z>p+n-a or t in sub:raise ValueError('topology subchunk length/duplicate')
       sub[t]=(a,z);q=a+z
      if q!=p+n:raise ValueError('topology partial header')
      if 0x34131 not in sub:raise ValueError('missing topology header')
      ha,hn=sub[0x34131];h=read(ha,hn)
      if len(h)<16:raise ValueError('short topology header')
      section,cols,rows=struct.unpack_from('<H2xHH',h);ox,oy=struct.unpack_from('<2f',h,8)
      if not all(math.isfinite(x)for x in(ox,oy)):raise ValueError('nonfinite topology origin')
      leaves=[sub.get(t,(0,0))for t in(0x34134,0x34136,0x34137)]
      np,nr,nc=[z for a,z in leaves]
      if np%32 or nr%4 or nc%4 or nc!=cols*rows*4:raise ValueError('topology array sizes')
      result.append({'name':name,'section':section,'columns':cols,'rows':rows,'origin':[ox,oy],'header':h,'leaves':leaves,'source_section_offset':off,'source_section_bytes':size,'container_offset_in_stream':p-8-self.base,'container_bytes':n+8,'polygons':np//32,'references':nr//4,'cells':nc//4})
     at=p+n
    if at!=end:raise ValueError('partial stream section chunk')
  return result
 def load_arrays(self,t):
  arrays=[self.archive.read(self.path,a,n)if n else b''for a,n in t['leaves']];pol,refs,cells=arrays
  if any(idx>=len(pol)//32 for idx,mask in struct.iter_unpack('<HH',refs)):raise ValueError('polygon reference')
  if any(start+count>len(refs)//4 for start,count in struct.iter_unpack('<HH',cells)):raise ValueError('cell reference')
  return arrays

def drivable_sections(track):
 containers=[(p,n)for t,p,n in chunks(track)if t==0x80034150]
 if len(containers)!=1:raise ValueError('visible-section container count')
 p,n=containers[0];sub={t:track[a:a+z]for t,a,z in chunks(track,p,p+n)}
 if 0x34152 not in sub or 0x34153 not in sub:raise ValueError('missing visible sections')
 lod=struct.unpack_from('<i',sub.get(0x34151,b'\0'*4))[0];b=sub[0x34152];at=0;bounds={}
 while at+36<=len(b):
  sec,n=struct.unpack_from('<hh',b,at+8);box=struct.unpack_from('<4f',b,at+12)
  if sec<0 or n<0 or n>1024 or at+36+8*n>len(b)or sec in bounds:raise ValueError('boundary header')
  pts=[struct.unpack_from('<2f',b,at+36+8*k)for k in range(n)]
  if not all(math.isfinite(x)for v in[box]+pts for x in v):raise ValueError('nonfinite boundary')
  bounds[sec]=(box,pts);at+=36+8*n
 if at!=len(b)or len(sub[0x34153])%164:raise ValueError('visible-section layout')
 return lod,bounds

def inspect(game,track):
 s=Stream(game,track);lod,bounds=drivable_sections(s.track);groups=b''.join(s.track[p:p+n]for t,p,n in chunks(s.track)if t==0x3412f)
 if len(groups)%32 or len(groups)//32>256:raise ValueError('group shape')
 trees=s.topologies()
 if not trees:raise ValueError('no top-level original topology trees')
 if len({t['section']for t in trees})!=len(trees):raise ValueError('duplicate topology section')
 needed={sec+lod for sec in bounds};available={t['section']for t in trees}
 # Some PVS boundaries have no tree under the current section+LOD association.
 # Preserve them and report availability; never synthesize a tree or plane.
 projected=32+len(groups)+29*24+64+sum(24+8*len(p)for b,p in bounds.values())+sum(40+sum(n for a,n in t['leaves'])for t in trees)
 return s,lod,bounds,groups,trees,projected
