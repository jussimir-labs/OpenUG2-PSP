"""Offline-only EA MicroTalk decoder for the user's PS2 speech streams.

The pinned public decoder and both upstream notices ship with the builder.
The existing PSP speech reader remains EA-XA/32 kHz: conversion happens here.
"""
from pathlib import Path
import subprocess
import tempfile
from native_runtime import helper

HERE = Path(__file__).resolve().parent

class Decoder:
    def __enter__(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='nfsu2-microtalk-')
        self.exe = Path(self.tmp.name)/'mt2pcm'
        try:
            self.exe=helper('mt2pcm',self.exe,[HERE/'mt2pcm.c',HERE/'vendor/utk/utkdec.c'],['-std=c11'])
            self.encoder=Path(self.tmp.name)/'xaencode'
            self.encoder=helper('xaencode',self.encoder,[HERE/'xaencode.c'],['-std=c11'])
        except BaseException:
            self.tmp.cleanup()
            raise
        return self

    def __exit__(self,*exc):
        self.tmp.cleanup()

    def decode(self, stream, samples):
        if not 0 < samples <= 22050*300:
            raise ValueError('MicroTalk: invalid line length')
        p = subprocess.run([str(self.exe),str(samples)],input=stream,capture_output=True,timeout=15)
        if p.returncode or len(p.stdout)!=samples*2:
            raise ValueError('MicroTalk decoding failed: '+p.stderr.decode(errors='replace'))
        import struct
        return list(struct.unpack(f'<{samples}h',p.stdout))

    def encode_xa(self, pcm):
        import struct
        data=struct.pack(f'<{len(pcm)}h',*pcm)
        p=subprocess.run([str(self.encoder)],input=data,capture_output=True,timeout=15)
        if p.returncode or len(p.stdout)!=(len(pcm)+27)//28*15:
            raise ValueError('EA-XA encoding failed')
        return p.stdout
