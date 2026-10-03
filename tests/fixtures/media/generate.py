"""Owned PointyPointer fixtures, generated without third-party artwork."""
import struct, zlib, pathlib
root = pathlib.Path(__file__).parent
def png(w, h, pixel):
    def chunk(t, d):
        return struct.pack('>I', len(d))+t+d+struct.pack('>I', zlib.crc32(t+d))
    row=pixel*w if len(pixel)==4 else pixel
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(b'\0'+row for _ in range(h))))+chunk(b'IEND',b'')
(root/'alpha.png').write_bytes(png(3,1,b'\xff\x80\x40\x80'+b'\xff\xff\xff\0'*2))
def gif(frames, loop=0, w=3, h=1):
    data=b'GIF89a'+struct.pack('<HHBBB',w,h,0x81,0,0)+bytes([0,0,0,255,0,0,0,255,0,0,0,255])
    if loop is not None: data+=b'!\xff\x0bNETSCAPE2.0\x03\x01'+struct.pack('<H',loop)+b'\0'
    for x,pixels,disposal,delay in frames:
        data+=b'!\xf9\x04'+bytes([(disposal<<2)|1])+struct.pack('<H',delay)+b'\0\0'
        data+=b','+struct.pack('<HHHHB',x,0,len(pixels),1,0)
        codes=[]
        for p in pixels: codes += [4,p]
        codes += [5]
        bits=sum(c << (3*i) for i,c in enumerate(codes))
        payload=bits.to_bytes((len(codes)*3+7)//8,'little')
        data+=bytes([2,len(payload)])+payload+b'\0'
    return data+b';'
# First red stays; disposal 2 clears green; disposal 3 restores before blue.
(root/'disposal.gif').write_bytes(gif([(0,[1,0,0],1,5),(1,[2],2,5),(2,[3],3,5),(1,[2],1,0)]))
(root/'finite.gif').write_bytes(gif([(0,[1,0,0],2,5),(0,[0,2,0],1,5)],1))
(root/'changing-alpha.gif').write_bytes(gif([(0,[1,1,1],2,10),(0,[0,0,0],1,10)],0))
for i in range(7):
    (root/f'budget-{i}.png').write_bytes(png(4096,4096,bytes([i+1,0,0,255])))
