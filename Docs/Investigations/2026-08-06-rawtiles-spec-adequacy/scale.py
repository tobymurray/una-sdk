import math
def lat2y(lat, z):
    lat = max(min(lat, 85.05112877980659), -85.05112877980659)
    r = math.radians(lat)
    return int((1.0 - math.log(math.tan(r) + 1/math.cos(r))/math.pi) / 2.0 * (1 << z))
def lon2x(lon, z):
    return int((lon + 180.0)/360.0 * (1 << z))
def tiles(b, z):
    lon0, lat0, lon1, lat1 = b
    x0, x1 = lon2x(lon0, z), lon2x(lon1, z)
    y0, y1 = lat2y(lat1, z), lat2y(lat0, z)   # y0 = north
    nx = min(x1, (1<<z)-1) - max(x0,0) + 1
    ny = min(y1, (1<<z)-1) - max(y0,0) + 1
    return max(nx,0)*max(ny,0)

REGIONS = {
 "Stanley Park ~4x3 km":      (-123.16, 49.29, -123.11, 49.32),
 "Vancouver city ~20x14 km":  (-123.27, 49.19, -123.02, 49.32),
 "Greater Vancouver ~90km":   (-123.4, 49.0, -122.4, 49.4),
 "Switzerland":               (5.96, 45.82, 10.49, 47.81),
 "United Kingdom":            (-8.65, 49.86, 1.77, 60.86),
 "France (metro)":            (-5.14, 41.33, 9.56, 51.09),
 "CONUS":                     (-124.8, 24.4, -66.9, 49.4),
}
CAP = 2**32 - 1
for dim in (128, 256):
    bpp = 1
    tb = dim*dim*bpp
    print(f"\n=== tile_dim={dim}px ABGR2222 ({tb} B/tile, uncompressed) ===")
    print(f"{'region':28} {'zooms':>9} {'tiles':>12} {'index MB':>9} {'pack':>11} {'>4GiB?':>7}")
    for name, b in REGIONS.items():
        for zr in [(0,14),(0,15),(0,16),(0,17)]:
            n = sum(tiles(b, z) for z in range(zr[0], zr[1]+1))
            size = 292 + 20*n + n*tb + 4
            idx = 20*n/1e6
            flag = "YES" if size > CAP else ""
            print(f"{name:28} {str(zr):>9} {n:12,} {idx:9.1f} {size/2**20:9.1f}MiB {flag:>7}")
        print()
