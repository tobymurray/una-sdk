import math
def lat2y(lat,z):
    lat=max(min(lat,85.05112877980659),-85.05112877980659); r=math.radians(lat)
    return int((1.0-math.log(math.tan(r)+1/math.cos(r))/math.pi)/2.0*(1<<z))
def lon2x(lon,z): return int((lon+180.0)/360.0*(1<<z))
def tiles(b,z):
    lo0,la0,lo1,la1=b; x0,x1=lon2x(lo0,z),lon2x(lo1,z); y0,y1=lat2y(la1,z),lat2y(la0,z)
    return max(min(x1,(1<<z)-1)-max(x0,0)+1,0)*max(min(y1,(1<<z)-1)-max(y0,0)+1,0)
R={'Vancouver city':(-123.27,49.19,-123.02,49.32),'Greater Vancouver':(-123.4,49.0,-122.4,49.4),
   'Switzerland':(5.96,45.82,10.49,47.81),'United Kingdom':(-8.65,49.86,1.77,60.86),
   'France (metro)':(-5.14,41.33,9.56,51.09),'CONUS':(-124.8,24.4,-66.9,49.4)}
CAP=2**32-1
print(f"{'region':20} {'zooms':>6} {'tiles':>11} | {'None':>9} {'RLE':>9} {'deflate':>9} | fits in 4 GiB with")
print("-"*95)
for n_,b in R.items():
    for zmax in (14,16):
        n=sum(tiles(b,z) for z in range(0,zmax+1))
        f=lambda r:(292+20*n+n*16384*r+4)/2**30
        ok=[k for k,r in (('None',1.0),('RLE',0.324),('deflate',0.130)) if f(r)*2**30<=CAP]
        print(f"{n_:20} {'0-'+str(zmax):>6} {n:11,} | {f(1.0):8.2f}G {f(.324):8.2f}G {f(.130):8.2f}G | {', '.join(ok) if ok else 'NOTHING'}")
