import struct, zlib, subprocess, os, tempfile
P="/Users/tobymurray/git/una-sdk/Docs/Investigations/2026-08-05-rawtiles-device-proof-artifacts/stanley.rawtiles"
b=open(P,'rb').read(); D=struct.unpack_from('<H',b,60)[0]; N=struct.unpack_from('<I',b,88)[0]; IO=struct.unpack_from('<I',b,92)[0]
tiles=[]
for i in range(N):
    o=IO+20*i; x,y,off,ln=struct.unpack_from('<IIII',b,o+4); tiles.append(b[off:off+ln])
RAW=sum(map(len,tiles))

def defl(d, wbits):
    c=zlib.compressobj(9, zlib.DEFLATED, -wbits)   # raw deflate, no header
    return c.compress(d)+c.flush()

print(f"=== deflate ratio vs decoder window (12x 256px ABGR2222 tiles, {RAW//1024} KiB raw) ===")
print(f"{'windowBits':>10} {'window RAM':>11} {'bytes':>9} {'% raw':>7} {'x smaller':>10}")
for wb in range(9,16):
    tot=sum(len(defl(t,wb)) for t in tiles)
    print(f"{wb:>10} {2**wb//1024 if 2**wb>=1024 else 2**wb:>8}{'KiB' if 2**wb>=1024 else 'B  ':>3} {tot:9,} {tot/RAW*100:6.1f}% {RAW/tot:9.2f}x")

print(f"\n=== tile size vs compression (same pixels, re-cut) ===")
def cut(t, src, dst):
    out=[]
    for by in range(0,src,dst):
        for bx in range(0,src,dst):
            out.append(bytes(b for r in range(dst) for b in t[(by+r)*src+bx:(by+r)*src+bx+dst]))
    return out
for dim in (256,128,64):
    sub=[s for t in tiles for s in (cut(t,256,dim) if dim<256 else [t])]
    for wb,lbl in ((11,'2 KiB win'),(15,'32 KiB win')):
        tot=sum(len(defl(s,wb)) for s in sub)
        r=sum(len(defl(s,wb)) for s in sub)/RAW*100
        print(f"  tile_dim={dim:3} n={len(sub):4}  deflate({lbl:10}) -> {tot:8,} B  {r:5.1f}% of raw")
    tot_rle=RAW
print(f"\n=== LZ4 (block, per tile) ===")
for lvl in ('-1','-9','-12'):
    tot=0; ok=True
    for t in tiles:
        with tempfile.NamedTemporaryFile(delete=False,suffix='.bin') as f: f.write(t); p=f.name
        r=subprocess.run(['lz4',lvl,'-f','-q','--no-frame-crc',p,p+'.lz4'],capture_output=True)
        if os.path.exists(p+'.lz4'): tot+=os.path.getsize(p+'.lz4'); os.remove(p+'.lz4')
        else: ok=False
        os.remove(p)
    if ok: print(f"  lz4 {lvl:4} -> {tot:8,} B  {tot/RAW*100:5.1f}%  {RAW/tot:.2f}x")
