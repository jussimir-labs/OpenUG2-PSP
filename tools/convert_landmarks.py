#!/usr/bin/env python3
"""Small original building LODs, streamed independently of roads and near scenery.

Writes an optional <world>-landmarks.n2cy beside an unchanged world archive.
The grid, textures, section gates and 64 m seams match that world's archive.
Only FAR blobs exist. --bbox limits an investigation; omit it for the city.
"""
import argparse, collections, hashlib, json, math, struct
from pathlib import Path
import convert_city as cc
import convert_world as cw
import n2world as nw
from n2cy_bounds import with_bounds, batches as geometry_batches


def with_dependencies(blob):
    """N2CY v2 optional LOD extension: sorted texture indices after bounds.

    Header words +8/+12 store offset/count. Legacy readers ignore the tail;
    new readers use it for admission and verify it against the batch headers.
    """
    if not blob:return blob
    found,end=geometry_batches(blob)
    tab=struct.unpack_from('<I',blob,4)[0]
    if not tab or tab!=end or tab+len(found)*16!=len(blob):
        raise ValueError('LOD dependencies require a complete bounds table')
    textures=sorted({struct.unpack_from('<I',blob,at)[0] for at,_,_,_ in found})
    out=bytearray(blob)
    struct.pack_into('<2I',out,8,len(out),len(textures))
    out.extend(struct.pack(f'<{len(textures)}I',*textures))
    out.extend(bytes((-len(out))%16))
    return bytes(out)


def selected(inst):
    # Buildings and their lit facades; leave roads, foliage, props and
    # moving structures with their existing owners.
    return (inst.name.upper().startswith('XB_') and not inst.flags & 0x10
            and inst.keys[0] and inst.radius >= 12
            and inst.bmax[2] - inst.bmin[2] >= 6)


def tower(inst, height):
    """A building at least height metres tall (its box), in a LOD section
    or a detailed one: what stands against the sky past the city's range.
    The 4104's, 4126's and 4145's XB_UC_TallCondo_1a_RB_00 (LOD sections
    530 and 534, 105 m, its two halves) came in at 180 m as a column."""
    return selected(inst) and inst.bmax[2] - inst.bmin[2] >= height


def build(game, city, out, bbox=None, track='L4RA'):
    if out.resolve()==city.resolve():raise ValueError('sidecar must not replace the city archive')
    with city.open('rb') as f:
        header=f.read(64)
        magic,version,cell,i0,j0,w,h,nt,*_=struct.unpack_from('<4sIf2i3I4f2I',header)
        if magic!=b'N2CY' or version!=2 or cell!=64:
            raise ValueError('expected N2CY v2, 64 m grid')
        to=struct.unpack_from('<I',header,52)[0];f.seek(to)
        texture_index={k:(o,s) for k,o,s in struct.iter_unpack('<3I',f.read(nt*12))}
        texture_flags={}
        for key,(offset,size) in texture_index.items():
            f.seek(offset+8);texture_flags[key]=struct.unpack('<I',f.read(4))[0]
    stream=nw.Stream(game,track);cc._init(game,track)
    cells=collections.defaultdict(lambda:collections.defaultdict(list))
    instances=[]
    groups=nw.scenery_groups(stream.track)
    grouped=set().union(*(v for k,v in groups.items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM'))) if groups else set()
    for inst in cc.gather_instances(stream,cw.SKIP):
        if (inst.section,inst.index) in grouped:continue
        if not selected(inst):continue
        if bbox and (inst.bmax[0]<bbox[0] or inst.bmax[1]<bbox[1] or inst.bmin[0]>bbox[2] or inst.bmin[1]>bbox[3]):continue
        obj=cc._object(inst.keys[2] or inst.keys[1] or inst.keys[0]) or cc._object(inst.keys[0])
        if obj is None:continue
        place=nw.place(inst);points=[place(*v[:3]) for v in obj.verts]
        gate=cc.gated(inst.section);triangles=0
        for ids,tex in obj.subs:
            if tex not in texture_flags:continue
            flags=texture_flags[tex]
            if flags & cw.F_WET:continue
            for at in range(0,len(ids),3):
                tri=[]
                for index in ids[at:at+3]:
                    x,y,z=points[index];_,_,_,color,u,v=obj.verts[index]
                    tri.append((x,y,z,u,v,(color>>16)&255,(color>>8)&255,color&255))
                xs,ys=[p[0] for p in tri],[p[1] for p in tri]
                for ci in range(max(i0,math.floor(min(xs)/cell)),min(i0+w-1,math.floor(max(xs)/cell))+1):
                    for cj in range(max(j0,math.floor(min(ys)/cell)),min(j0+h-1,math.floor(max(ys)/cell))+1):
                        pieces=nw.clip_square(tuple(tri),ci*cell,cj*cell,(ci+1)*cell,(cj+1)*cell)
                        if pieces:cells[ci,cj][tex,flags,gate].extend(pieces);triangles+=len(pieces)
        instances.append({'name':inst.name,'section':inst.section,'model':obj.name,'triangles':triangles})
    used=sorted({key[0] for b in cells.values() for key in b});remap={key:i for i,key in enumerate(used)}
    texoff=64+w*h*24;data=bytearray((texoff+len(used)*12+15)&~15)
    def put(blob):
        data.extend(bytes((-len(data))%16));at=len(data);data.extend(blob);return at,len(blob)
    geometry_bytes=0;batches=0
    for (ci,cj),batch in sorted(cells.items()):
        converted={(remap[t],f,g):tris for (t,f,g),tris in batch.items()}
        cc.mark_sheets(converted)   # back-to-back sheets: their backs culled, as the city's
        blob=with_dependencies(with_bounds(cc.pack_geometry(converted,ci*cell,cj*cell)))
        if not blob:continue
        offset,size=put(blob);geometry_bytes+=size;batches+=struct.unpack_from('<I',blob)[0]
        struct.pack_into('<6I',data,64+((cj-j0)*w+ci-i0)*24,0,0,offset,size,0,0)
    texture_bytes=0
    with city.open('rb') as f:
        for i,key in enumerate(used):
            offset,size=texture_index[key];f.seek(offset);blob=f.read(size)
            if len(blob)!=size:raise ValueError('truncated city texture')
            offset,size=put(blob);texture_bytes+=size;struct.pack_into('<3I',data,texoff+i*12,key,offset,size)
    data[:64]=header
    struct.pack_into('<I',data,28,len(used));struct.pack_into('<2I',data,48,64,texoff)
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
    with city.open('rb') as f:city_sha=hashlib.file_digest(f,'sha256').hexdigest()
    report={'track':track,'landmark_dependencies_version':1,
            'source_city':str(city.resolve()),'source_city_sha256':city_sha,
            'bbox':bbox,'instances':instances,'cells':len(cells),'batches':batches,'textures':len(used),
            'geometry_bytes':geometry_bytes,'texture_bytes':texture_bytes,'bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'grid':[i0,j0,w,h]}
    out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return {k:v for k,v in report.items() if k!='instances'}


WHITE = 0xFFFFFFF0   # the flat batches' texture key: 16 x 8 texels of white


def white_blob():
    pal = bytes((255, 255, 255, 255)) + bytes(1020)
    return struct.pack('<3If', 16, 8, 0, 1.0) + pal + bytes(16 * 8)


def mean_colour(blob):
    """A texture blob's (convert_shell.texture_blob) mean RGB, 0-1."""
    w, h = struct.unpack_from('<2I', blob)
    pal, idx = blob[16:16 + 1024], blob[16 + 1024:16 + 1024 + w * h]
    count = collections.Counter(idx)
    return tuple(sum(pal[i * 4 + c] * n for i, n in count.items()) / (255 * w * h) for c in range(3))


def build_lod(game, city, out, bbox=None, cluster=12.0, cluster_up=32.0, flat=True, tile=64, buildings=False, towers=0.0):
    """The original's own far scenery instead of whole buildings: its LOD
    sections (n + 20: terrain, roads and big shells, which its PVS lists for
    the areas far from a drivable section; convert_shell.py's module doc),
    their _1B_ model, on the city's 64 m grid, each cell's batches simplified
    by vertex clustering (walls on a cluster-metre grid, up-facing triangles
    on a cluster_up one) and their textures at most convert_shell.SHELL_TEX
    texels. Same sidecar layout, bounds and dependencies as build().

    flat: the opaque batches untextured, each vertex's colour times its
    texture's mean (one white texture for them all), so a cell draws as one
    batch a section instead of one a texture: seen past 180 m through the
    haze, 32 texels are about one colour already, and the per-batch binds
    (CLUT, texture, matrix) cost the 4104 1.6 ms of GE a frame in the
    calibrated PPSSPP, for ~5 triangles a batch. Cut-outs, blended and
    additive batches keep their textures.

    tile: the cells' size, a multiple of the city's on its lattice (256: a
    block of 4 x 4 city cells, one read for 16: at 64 m the 4104 asked
    ~12 reads a second of the Memory Stick, ~4 ms each, beside the city's).

    buildings: the city's own big buildings too (selected(), their lightest
    model), which the LOD sections lack: in the 4104 and the 4142 those
    added at most a few hundred pixels past the city's range.

    towers: only the buildings this many metres tall or more (tower()),
    from any section, nothing else: the skyline alone, a fraction of the
    whole layer's batches and fill."""
    import convert_shell as cs
    if out.resolve()==city.resolve():raise ValueError('sidecar must not replace the city archive')
    with city.open('rb') as f:
        header=f.read(64)
        magic,version,cell,i0,j0,w,h,nt,*_=struct.unpack_from('<4sIf2i3I4f2I',header)
        if magic!=b'N2CY' or version!=2 or cell!=64:
            raise ValueError('expected N2CY v2, 64 m grid')
    if tile!=64:
        f=int(tile//64)
        if f*64!=tile or f<1:raise ValueError('tile: a multiple of 64 m')
        i1,j1=i0+w,j0+h;i0,j0=i0//f,j0//f;w,h=-(-i1//f)-i0,-(-j1//f)-j0;cell=float(tile)
        header=bytearray(header);struct.pack_into('<f2i2I',header,8,cell,i0,j0,w,h);header=bytes(header)
    cc._init(game);stream=cc._stream
    groups=nw.scenery_groups(stream.track)
    grouped=set().union(*(v for k,v in groups.items() if 'BARRIERS_' in k or k.startswith('FREE_ROAM'))) if groups else set()
    sources=cw.texture_index(stream,game);tex_flags={}
    for key,tpk in sources.items():
        usage,blend=cw.draw_mode(tpk,key)
        tex_flags[key]=cw.F_ADD if blend==2 else cw.F_BLEND if usage==2 and blend==1 else cw.F_ALPHA if usage==1 else 0
    for key in (0x3394fe62,0x2e95ce7c):tex_flags.pop(key,None)
    for key in cc.KEYED_BLACK:
        if key in tex_flags:tex_flags[key]=cw.F_ALPHA
    cells=collections.defaultdict(lambda:collections.defaultdict(list));instances=[]
    for inst in cc.gather_instances(stream,cw.SKIP):
        lod=cs.lod_section(inst.section)
        if towers:
            if (inst.section,inst.index) in grouped or not tower(inst,towers):continue
        elif (inst.section,inst.index) in grouped or not (lod or buildings and selected(inst)):continue
        if bbox and (inst.bmax[0]<bbox[0] or inst.bmax[1]<bbox[1] or inst.bmin[0]>bbox[2] or inst.bmin[1]>bbox[3]):continue
        base=cc._object(inst.keys[0])
        if base is None:continue
        if lod:obj=(cc._object(inst.keys[1]) if inst.keys[1] else None) or base
        else:obj=cc._object(inst.keys[2] or inst.keys[1] or inst.keys[0]) or base
        place=nw.place(inst);points=[place(*v[:3]) for v in obj.verts]
        gate=cc.gated(inst.section);triangles=0
        for ids,tex in obj.subs:
            if tex not in tex_flags:continue
            for at in range(0,len(ids),3):
                tri=[]
                for index in ids[at:at+3]:
                    x,y,z=points[index];_,_,_,color,u,v=obj.verts[index]
                    tri.append((x,y,z,u,v,(color>>16)&255,(color>>8)&255,color&255))
                xs,ys=[p[0] for p in tri],[p[1] for p in tri]
                for ci in range(max(i0,math.floor(min(xs)/cell)),min(i0+w-1,math.floor(max(xs)/cell))+1):
                    for cj in range(max(j0,math.floor(min(ys)/cell)),min(j0+h-1,math.floor(max(ys)/cell))+1):
                        pieces=nw.clip_square(tuple(tri),ci*cell,cj*cell,(ci+1)*cell,(cj+1)*cell)
                        if pieces:cells[ci,cj][tex,gate,not lod].extend(pieces);triangles+=len(pieces)
        instances.append({'name':inst.name,'section':inst.section,'model':obj.name,'triangles':triangles})
    blobs={}
    def blob_of(key):
        if key==WHITE:return white_blob()
        if key not in blobs:blobs[key]=cs.texture_blob(sources,tex_flags,key)
        return blobs[key]
    if flat:tex_flags[WHITE]=0
    for k,batch in list(cells.items()):
        merged=collections.defaultdict(list)
        for (t,g,b),tris in batch.items():
            if not flat or tex_flags[t] or b:merged[t,g].extend(tris);continue   # buildings keep their textures: flat, a lit facade was a grey slab (the 4101's waterfront)
            m=mean_colour(blob_of(t))
            merged[WHITE,g].extend(tuple(p[:5]+(p[5]*m[0],p[6]*m[1],p[7]*m[2]) for p in tri) for tri in tris)
        cells[k]=merged
    before=sum(len(t) for b in cells.values() for t in b.values())
    for batch in cells.values():
        for key in list(batch):
            batch[key]=cs.cluster(batch[key],cluster,cluster_up)
            if not batch[key]:del batch[key]
    for k in [k for k,b in cells.items() if not b]:del cells[k]
    after=sum(len(t) for b in cells.values() for t in b.values())
    used=sorted({key[0] for b in cells.values() for key in b});remap={key:i for i,key in enumerate(used)}
    texoff=64+w*h*24;data=bytearray((texoff+len(used)*12+15)&~15)
    def put(blob):
        data.extend(bytes((-len(data))%16));at=len(data);data.extend(blob);return at,len(blob)
    geometry_bytes=0;batches=0
    for (ci,cj),batch in sorted(cells.items()):
        converted={(remap[t],tex_flags[t],g):tris for (t,g),tris in batch.items()}
        cc.mark_sheets(converted)   # back-to-back sheets: their backs culled, as the city's
        blob=with_dependencies(with_bounds(cc.pack_geometry(converted,ci*cell,cj*cell)))
        if not blob:continue
        offset,size=put(blob);geometry_bytes+=size;batches+=struct.unpack_from('<I',blob)[0]
        struct.pack_into('<6I',data,64+((cj-j0)*w+ci-i0)*24,0,0,offset,size,0,0)
    texture_bytes=0
    for i,key in enumerate(used):
        offset,size=put(blob_of(key));texture_bytes+=size;struct.pack_into('<3I',data,texoff+i*12,key,offset,size)
    data[:64]=header
    struct.pack_into('<I',data,28,len(used));struct.pack_into('<2I',data,48,64,texoff)
    out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
    with city.open('rb') as f:city_sha=hashlib.file_digest(f,'sha256').hexdigest()
    report={'track':'L4RA','mode':(f'towers {towers:g} m' if towers else 'lod-sections')+(' flat' if flat else ''),'tile':cell,'landmark_dependencies_version':1,'cluster':[cluster,cluster_up],
            'source_city':str(city.resolve()),'source_city_sha256':city_sha,
            'bbox':bbox,'instances':instances,'cells':len(cells),'batches':batches,'textures':len(used),
            'triangles_before':before,'triangles':after,
            'geometry_bytes':geometry_bytes,'texture_bytes':texture_bytes,'bytes':len(data),
            'sha256':hashlib.sha256(data).hexdigest(),'grid':[i0,j0,w,h]}
    out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return {k:v for k,v in report.items() if k!='instances'}


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game',type=Path,required=True);ap.add_argument('--city',type=Path,required=True)
    ap.add_argument('--track',default='L4RA',choices=('L4RA','L4RB','L4RC','L4RD','L4RE','L4RF','L4RG','L4RH'),
                    help='source world matching --city; L4RA for the city, other levels for closed tracks')
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--bbox',type=float,nargs=4)
    ap.add_argument('--lod',action='store_true',help="the original's LOD sections, simplified (build_lod), not whole buildings")
    ap.add_argument('--cluster',type=float,default=12.0);ap.add_argument('--cluster-up',type=float,default=32.0)
    ap.add_argument('--textured',action='store_true',help='--lod: every batch with its texture (no flat colours)')
    ap.add_argument('--tile',type=int,default=64,help="--lod: the cells' size, a multiple of the city's 64 m")
    ap.add_argument('--buildings',action='store_true',help="--lod: the city's big buildings too (selected())")
    ap.add_argument('--towers',type=float,default=0,help='--lod: only the buildings this many metres tall or more (tower())')
    a=ap.parse_args()
    if a.lod:print(json.dumps(build_lod(a.game,a.city,a.out,a.bbox,a.cluster,a.cluster_up,not a.textured,a.tile,a.buildings,a.towers),indent=2))
    else:print(json.dumps(build(a.game,a.city,a.out,a.bbox,a.track),indent=2))
