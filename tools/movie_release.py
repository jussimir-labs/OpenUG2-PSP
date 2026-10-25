#!/usr/bin/env python3
"""Check movie availability for a COMPLETE candidate, without playing/converting.

Reads headers, indexes and small metadata only. No city/JPEG/MP3 payload hashing,
no device access, and no changes to skip/quick-entry controls. Generic run fixtures
need not contain movies; callers opt into this complete-candidate gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys

CONTRACT = Path(__file__).with_name('movie_release_contract.json')
HEADER = struct.Struct('<4s9I')
MAX_INDEX = 256 * 1024
MAX_METADATA = 2 * 1024 * 1024


def _need(condition, reason):
    if not condition:
        raise ValueError(reason)


def _small(path, limit):
    with Path(path).open('rb') as f:
        raw=f.read(limit+1)
    _need(len(raw)<=limit, 'Metadata too large: '+str(path))
    return raw


def _token(info):
    return info.st_dev,info.st_ino,info.st_size,info.st_mtime_ns,info.st_ctime_ns


def _movie(path, fmt):
    with path.open('rb') as stream:
        before=os.fstat(stream.fileno())
        raw=stream.read(HEADER.size)
        _need(len(raw)==HEADER.size, path.name+': truncated header')
        magic,version,fps,w,h,frames,rate,sound,index_at,sound_at=HEADER.unpack(raw)
        _need(magic==b'N2MV' and (version,fps,w,h,rate)==
              (fmt['version'],fmt['fps'],fmt['width'],fmt['height'],fmt['audio_rate']),
              path.name+': incompatible movie format')
        index_bytes=frames*8
        _need(frames>0 and sound>0 and index_bytes<=MAX_INDEX,
              path.name+': invalid/beyond-budget frame count or empty audio')
        _need(HEADER.size<=index_at and index_at+index_bytes<=sound_at<before.st_size,
              path.name+': index/audio bounds')
        stream.seek(index_at);index=stream.read(index_bytes)
        _need(len(index)==index_bytes,path.name+': truncated frame index')
        for offset,size in struct.iter_unpack('<II',index):
            _need(0<size<=65536 and index_at+index_bytes<=offset and offset+size<=sound_at,
                  path.name+': JPEG frame outside picture area or decoder size limit')
        # Converter v3 starts MP3 directly (no ID3 tag): one MPEG1/LayerIII
        # header verifies declared 44100Hz stereo cheaply, not decodability.
        stream.seek(sound_at);mp3=stream.read(4)
        _need(len(mp3)==4,path.name+': truncated MP3 header')
        bits=int.from_bytes(mp3,'big')
        _need(bits>>21==0x7ff and ((bits>>19)&3)==3 and ((bits>>17)&3)==1
              and ((bits>>10)&3)==0 and 0<((bits>>12)&15)<15 and ((bits>>6)&3)!=3,
              path.name+': expected MPEG1 LayerIII 44100Hz stereo prefix')
        _need(_token(before)==_token(os.fstat(stream.fileno()))==_token(path.stat()),
              path.name+': changed during inspection')
    return dict(bytes=before.st_size,frames=frames,audio_frames=sound,
                metadata_bytes_read=HEADER.size+index_bytes+4)


def _triggers(raw, required):
    names={name[6:-5] for name in required}
    references=[]
    # main.c movie_queue uses fgets(line,96), %15s tokens and bit1+line
    # in a32-bit seen mask; blank/comment physical lines also consume a bit.
    lines=raw.splitlines(keepends=True)
    _need(len(lines)<=31 and b'\0' not in raw,'movies.txt exceeds31 physical lines or contains NUL')
    for physical in lines:
        _need(len(physical)<=95,'movies.txt line exceeds the runtime fgets buffer')
        line=physical.decode('ascii').strip()
        if not line or line.startswith('#'):continue
        fields=line.split();kind=fields[0]
        _need(all(len(field)<=15 for field in fields),'movies.txt token exceeds runtime15-byte limit')
        expected={'pre':4,'post':4,'place':3,'stage':3,'final':4}.get(kind)
        _need(expected is not None and len(fields)==expected,'Invalid movies.txt trigger: '+line)
        _need(fields[-1] in names,'movies.txt refers to an unavailable movie: '+fields[-1])
        if kind in ('pre','post'):
            _need(fields[1].isdigit() and fields[2].isdigit(),'Invalid event/stage trigger: '+line)
        elif kind=='stage':_need(fields[1].isdigit(),'Invalid stage trigger: '+line)
        elif kind=='final':_need(fields[1].isdigit() and fields[2] in ('won','lost'),'Invalid final trigger: '+line)
        elif kind=='place':_need(fields[1] in ('crib','carlot'),'Invalid place trigger: '+line)
        references.append(fields[-1])
    _need(bool(references),'movies.txt has no triggers')
    return references


def verify(folder):
    """Return structural evidence, or raise ValueError/OSError. Does not launch."""
    folder=Path(folder)
    contract_raw=_small(CONTRACT,65536);contract=json.loads(contract_raw)
    required=contract['required_movies'];opening=contract['opening_movies']
    _need(contract.get('schema')==1 and len(required)==28 and len(set(required))==28
          and len(opening)==4 and len(set(opening))==4 and set(opening)<=set(required),
          'Invalid complete-movie contract')
    _need(all(re.fullmatch(r'movie-[A-Z0-9_]+\.n2mv',n) for n in required),'Unsafe movie contract name')
    _need(contract['format']==dict(version=3,fps=15,width=480,height=272,audio_rate=44100),
          'Unsupported contract format; update validator with reviewed format change')
    manifest=None;manifest_bytes=0
    if (folder/'.assets.json').exists():
        raw=_small(folder/'.assets.json',MAX_METADATA);manifest_bytes=len(raw);manifest=json.loads(raw)
        files=manifest['files'];digest=hashlib.sha256(json.dumps(files,sort_keys=True,ensure_ascii=False).encode()).hexdigest()
        _need(digest==manifest['digest'],'Asset manifest digest mismatch')
        _need(len({n.lower() for n in files})==len(files),'Case collision in asset manifest')
        for name in required+['movies.txt']:
            _need(re.fullmatch('[0-9a-f]{64}',files.get(name,'')) is not None,'Required movie absent from manifest: '+name)
    movies={name:_movie(folder/name,contract['format']) for name in required}
    triggers_raw=_small(folder/'movies.txt',65536)
    refs=_triggers(triggers_raw,required)
    trigger_sha=hashlib.sha256(triggers_raw).hexdigest()
    if manifest is not None:
        _need(manifest['files']['movies.txt']==trigger_sha,'movies.txt differs from frozen manifest')
    return dict(state='available-structurally-checked',profile=contract['profile'],movies=len(movies),
                opening_movies=len(opening),movie_files=movies,trigger_references=len(refs),movies_txt_sha256=trigger_sha,
                contract_sha256=hashlib.sha256(contract_raw).hexdigest(),
                reference_snapshot=contract['reference_snapshot'],
                inspected_snapshot=manifest['digest'] if manifest else None,
                metadata_bytes_read=len(contract_raw)+manifest_bytes+len(triggers_raw)+sum(m['metadata_bytes_read'] for m in movies.values()),
                full_payload_hashes='deferred; manifest movie hashes are declarations, not verified bytes',
                playback='not requested; skipping and direct-race controls unchanged',
                physical_validation='pending',
                limits=['Structural availability only; no JPEG/MP3 decode or complete-film proof',
                        'No whole-folder atomic snapshot; freeze/publish must verify final payload identities',
                        'Reference hashes identify the historical baseline; compatible revised assets are permitted'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('assets',type=Path)
    args=parser.parse_args()
    try:print(json.dumps(verify(args.assets),indent=2))
    except (OSError,ValueError,KeyError,TypeError,struct.error) as error:
        print(json.dumps(dict(state='unavailable',error=str(error))),file=sys.stderr);return 2
    return 0

if __name__=='__main__':raise SystemExit(main())
