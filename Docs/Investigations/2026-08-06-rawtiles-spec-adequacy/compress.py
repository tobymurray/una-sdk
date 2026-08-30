import struct, zlib, lzma, bz2, collections, subprocess, os, tempfile, math
P="/Users/tobymurray/git/una-sdk/Docs/Investigations/2026-08-05-rawtiles-device-proof-artifacts/stanley.rawtiles"
b=open(P,'rb').read()
D=struct.unpack_from('<H',b,60)[0]; N=struct.unpack_from('<I',b,88)[0]; IO=struct.unpack_from('<I',b,92)[0]
tiles=[]
for i in range(N):
    o=IO+20*i; x,y,off,ln=struct.unpack_from('<IIII',b,o+4); tiles.append(b[off:off+ln])
RAW=sum(len(t) for t in tiles)

def rle(d, Pw=1):                      # spec 9.11 canonical, pixel units
    out=bytearray(); i=0; n=len(d)//Pw; lit=[]
    def flush():
        nonlocal_out=out
        while lit:
            c=lit[:128]; del lit[:128]; nonlocal_out.append(len(c)-1)
            for px in c: nonlocal_out.extend(px)
    while i<n:
        px=d[i*Pw:(i+1)*Pw]; j=i+1
        while j<n and d[j*Pw:(j+1)*Pw]==px and j-i<128: j+=1
        run=j-i
        if run>=3: flush(); out.append(0x80|(run-1)); out.extend(px); i=j
        else: lit.append(px); i+=1
    flush(); return bytes(out)

def sub_filter(d):                     # PNG "Sub" per row
    out=bytearray()
    for r in range(D):
        row=d[r*D:(r+1)*D]; prev=0
        for v in row: out.append((v-prev)&0xFF); prev=v
    return bytes(out)

def cli(name, args, data):
    with tempfile.NamedTemporaryFile(delete=False) as f: f.write(data); p=f.name
    try:
        r=subprocess.run(args+[p,'-o',p+'.out'],capture_output=True)
        if not os.path.exists(p+'.out'): return None
        s=os.path.getsize(p+'.out'); os.remove(p+'.out'); return s
    finally: os.remove(p)

def pal4(d):                           # lossy: top-16 colours, nearest merge in ABGR2222 space
    cnt=collections.Counter(d); keep=[v for v,_ in cnt.most_common(16)]
    def dist(a,c):
        return sum((((a>>s)&3)-((c>>s)&3))**2 for s in (0,2,4))
    lut={v:min(range(len(keep)),key=lambda k:dist(v,keep[k])) for v in cnt}
    out=bytearray()
    for i in range(0,len(d),2): out.append((lut[d[i]]<<4)|lut[d[i+1]])
    return bytes(out), len(keep)

rows=[]
def add(label, sizes, note):
    tot=sum(sizes); rows.append((label, tot, tot/RAW*100, note))

add("raw ABGR2222 (compression = None)", [len(t) for t in tiles], "0 B decoder state")
add("spec RLE (§ 9.11)", [len(rle(t)) for t in tiles], "~30 lines C, O(1) RAM, row-streamable")
add("RLE then deflate-9", [len(zlib.compress(rle(t),9)) for t in tiles], "headroom probe (not a proposal)")
add("deflate-9 (raw)", [len(zlib.compress(t,9)) for t in tiles], "32 KiB window + out; tinfl ~2 KB code")
add("deflate-9 + Sub filter", [len(zlib.compress(sub_filter(t),9)) for t in tiles], "PNG-equivalent")
add("bzip2-9", [len(bz2.compress(t,9)) for t in tiles], "too big for MCU")
add("LZMA", [len(lzma.compress(t,preset=9)) for t in tiles], "too big for MCU")
lz=[cli('lz4',['lz4','-9','-f','-q'],t) for t in tiles]
if all(lz): add("LZ4 -9 (frame)", lz, "~200 lines; needs full tile out-buffer")
zs=[cli('zstd',['zstd','-19','-f','-q'],t) for t in tiles]
if all(zs): add("zstd -19", zs, "~60 KB code — reference only")
p4=[pal4(t) for t in tiles]
add("4bpp palette (LOSSY, 16 col)", [len(a) for a,_ in p4], "16 B palette + 4->8 LUT expand")
add("4bpp palette + RLE (LOSSY)", [len(rle(a)) for a,_ in p4], "LUT expand + RLE")
add("4bpp palette + deflate (LOSSY)", [len(zlib.compress(a,9)) for a,_ in p4], "lower bound probe")

print(f"12 real tiles, 256x256 ABGR2222, {RAW} B raw ({RAW/1024:.0f} KiB)\n")
print(f"{'scheme':34} {'bytes':>9} {'% raw':>7}  {'x smaller':>9}  decoder cost")
print("-"*110)
for l,t,p,n in rows: print(f"{l:34} {t:9,} {p:6.1f}% {RAW/t:8.2f}x  {n}")
