#!/usr/bin/env python3
"""Original free-roam shop polygons -> shopzones.n2sz (N2SZ v1).

Separate mandatory input for shop entry. Leaves career.n2cr, city, doors,
barriers and ShopNIS unchanged. Game XY is stored without swapping so the
GameCube's half-open ray test retains its exact boundary convention.
Header <4s3I>: magic/version=1/shops/vertices; shop <24sIHHI4f>:
name, trigger handle, vertex count, original zone type, first vertex,
original bbox(minX,minY,maxX,maxY); then contiguous <2f> game XY vertices.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from n2formats import archived, chunks
import n2source

MAX_SHOPS, MAX_POINTS = 32, 16
SHOP_KINDS = {'CARLOT01', 'PERFSHOP01', 'PARTSSHOP01', 'PAINTSHOP01', 'AUDIOSHOP01', 'CRIB01'}

def career_shop_names(data):
    if len(data)<12 or data[:4]!=b'N2CA':raise ValueError('invalid career header')
    version,count=struct.unpack_from('<2I',data,4)
    if not 2<=version<=9 or count>256:raise ValueError('unsupported career version/count')
    pos=12+count*12
    for size,limit in ((16,6),(26,24),(4,40),(20,256),(40,32)):
        if pos+4>len(data):raise ValueError('truncated career table')
        n=struct.unpack_from('<I',data,pos)[0];pos+=4
        if n>limit or pos+n*size>len(data):raise ValueError('career table bounds')
        if size==40:
            names=[]
            for i in range(n):
                raw=data[pos+i*40:pos+i*40+24]
                if b'\0' not in raw:raise ValueError('unterminated shop name')
                names.append(raw.split(b'\0')[0].decode('ascii'))
            if len(set(names))!=n:raise ValueError('duplicate career shop')
            return names
        pos+=n*size
    raise ValueError('no shops')

def shop_descriptors(globalb):
    tables=[globalb[p:p+n] for t,p,n in chunks(globalb) if t==0x34a12]
    if len(tables)!=1 or len(tables[0])%160:raise ValueError('shop descriptor table')
    result={}
    for at in range(0,len(tables[0]),160):
        r=tables[0][at:at+160];name=r[:32].split(b'\0')[0].decode('ascii')
        kind=r[64:80].split(b'\0')[0].decode('ascii').split('.')[0]
        if name in result:raise ValueError('duplicate shop descriptor')
        result[name]=(struct.unpack_from('<I',r,60)[0],kind if kind in SHOP_KINDS else None)
    return result

def path_zones(path,needed):
    tables=[path[p:p+n] for t,p,n in chunks(path) if t==0x3414a]
    if len(tables)!=1:raise ValueError('free-roam zone table')
    data=tables[0];at=0;result={}
    while at<len(data):
        if len(data)-at<68:raise ValueError('truncated zone header')
        n,size=struct.unpack_from('<2H',data,at+64)
        if size<68 or size>len(data)-at or n>(size-68)//8:raise ValueError('zone record bounds')
        typ=struct.unpack_from('<I',data,at)[0]
        handle=struct.unpack_from('<I',data,at+48)[0]
        if handle in needed:
            handle=struct.unpack_from('<I',data,at+48)[0]
            if not 3<=n<=MAX_POINTS or handle in result:raise ValueError('shop zone vertices/duplicate')
            bounds=struct.unpack_from('<4f',data,at+32)
            points=[struct.unpack_from('<2f',data,at+68+8*i) for i in range(n)]
            if not all(math.isfinite(v) for v in bounds+tuple(v for p in points for v in p)):raise ValueError('nonfinite zone')
            xs,ys=zip(*points)
            if bounds!=(min(xs),min(ys),max(xs),max(ys)):raise ValueError('original zone bbox mismatch')
            area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))
            if area==0:raise ValueError('degenerate shop zone')
            result[handle]=(typ,bounds,points)
        at+=size
    return result

def build(game,names):
    if not 0<len(names)<=MAX_SHOPS or len(set(names))!=len(names):raise ValueError('shop names/count')
    glob=archived(game,'GLOBAL/GLOBALB.BUN')
    path=archived(game,'TRACKS/ROUTESL4RA/PathsFreeRoam.bin')
    desc=shop_descriptors(glob)
    if any(name not in desc for name in names):raise ValueError('shop not in descriptors')
    zones=path_zones(path,{desc[name][0] for name in names});records=[];points=[];report=[]
    for name in names:
        raw=name.encode('ascii')
        if not raw or len(raw)>=24 or name not in desc:raise ValueError('runtime shop name')
        handle,want=desc[name]
        if handle not in zones or want is None:raise ValueError(f'no original zone for {name}')
        typ,bounds,vertices=zones[handle]
        if typ!=14:raise ValueError(f'expected original shop TrackPathZone type14: {name}/{typ}')
        first=len(points);records.append(struct.pack('<24sIHHI4f',raw,handle,len(vertices),typ,first,*bounds))
        points.extend(vertices);report.append(dict(name=name,handle=handle,type=typ,bounds_game=bounds,points_game=vertices))
    blob=struct.pack('<4s3I',b'N2SZ',1,len(records),len(points))+b''.join(records)+b''.join(struct.pack('<2f',*p) for p in points)
    return blob,dict(format='N2SZ',version=1,bytes=len(blob),shops=report,
                     globalb_sha256=hashlib.sha256(glob).hexdigest(),paths_free_roam_sha256=hashlib.sha256(path).hexdigest(),
                     blob_sha256=hashlib.sha256(blob).hexdigest(),height='2D original polygon; existing port ground guard is separate')

def write(game,names,out):
    blob,report=build(game,names);out=Path(out);out.mkdir(parents=True,exist_ok=True)
    tmp=out/'shopzones.n2sz.tmp';tmp.write_bytes(blob);tmp.replace(out/'shopzones.n2sz')
    (out/'shopzones.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    n2source.game_args(ap)
    ap.add_argument('--career',type=Path,required=True)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();r=write(a.game,career_shop_names(a.career.read_bytes()),a.out)
    print(f"N2SZ v1: {len(r['shops'])} shops, {r['bytes']} bytes")
if __name__=='__main__':main()
