import math, struct
KB=1024
print("== RAM budget ==")
tile = 256*256  # ABGR2222 = 1 B/px
print(f"one 256px ABGR2222 tile = {tile} B = {tile/KB:.0f} KiB")
print(f"3x3 mosaic = {9*tile} B = {9*tile/KB:.0f} KiB vs app RAM 256 KiB -> {9*tile/(256*KB):.2f}x over")
pack = 787008
print(f"stanley pack = {pack} B = {pack/KB:.1f} KiB -> {pack/(256*KB):.2f}x app RAM")
print(f"RLE @27% of raw: tile ~{tile*0.27/KB:.1f} KiB, 3x3 compressed-resident ~{9*tile*0.27/KB:.0f} KiB (fits 256 KiB w/ row-streaming decode)")
print(f"one raw tile (64 KiB) + framebuffer 57600 B + code/stack: 1-tile cache feasible, 2 tiles = 128 KiB tight")
print("\n== float lat/lon resolution (Stanley Park lat 49.30, lon -123.14) ==")
def ulp(x):
    import math
    m,e = math.frexp(abs(x))
    return math.ldexp(1.0, e-24)  # float32 24-bit mantissa
lat, lon = 49.30, -123.14
ulat, ulon = ulp(lat), ulp(lon)
m_per_deg_lat = 111320.0
m_per_deg_lon = 111320.0*math.cos(math.radians(lat))
print(f"float ULP(lat 49.3)={ulat:.3e} deg = {ulat*m_per_deg_lat:.2f} m; ULP(lon -123.14)={ulon:.3e} deg = {ulon*m_per_deg_lon:.2f} m")
for z in (14,15,16,17,18):
    res = 156543.03392*math.cos(math.radians(lat))/(2**z)
    print(f"z{z}: ground res {res:.2f} m/px; lon-ULP = {ulon*m_per_deg_lon/res:.2f} px, lat-ULP = {ulat*m_per_deg_lat/res:.2f} px")
print("global-pixel framing: world width at z in px = 2^z*256 = 2^(z+8); float32 mantissa 24 bits -> integer pixel exact only to 2^24 -> z16 is the last zoom where a float global pixel coord resolves 1px at map edge")
print("\n== flash budget ==")
for z in (15,16):
    res = 156543.03392*math.cos(math.radians(lat))/(2**z)
    tile_m = res*256
    n = math.ceil(10000/tile_m)
    tiles = n*n
    print(f"10x10 km @ z{z}: tile covers {tile_m:.0f} m -> {n}x{n}={tiles} tiles; raw {tiles*64/1024:.1f} MiB; RLE@27% {tiles*64*0.27/1024:.1f} MiB (NOR=2 MiB)")
zsum = sum((math.ceil(10000/(156543.03392*math.cos(math.radians(lat))/(2**z)*256)))**2 for z in range(12,17))
print(f"z12..z16 pyramid 10x10km: ~{zsum} tiles, RLE ~{zsum*64*0.27/1024:.1f} MiB")
