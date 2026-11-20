# independent: pure-python AES-128, own CMAC, built from spec text only
import hmac, hashlib, struct, math
sbox=[0]*256
def _init():
    p=q=1
    while True:
        p = p ^ ((p<<1)&0xff) ^ (0x1b if p&0x80 else 0)
        q ^= q<<1; q ^= q<<2; q ^= q<<4; q&=0xff
        if q&0x80: q^=0x09
        x = q ^ ((q<<1|q>>7)&0xff) ^ ((q<<2|q>>6)&0xff) ^ ((q<<3|q>>5)&0xff) ^ ((q<<4|q>>4)&0xff)
        sbox[p]=(x^0x63)&0xff
        if p==1: break
    sbox[0]=0x63
_init()
def xt(a): return ((a<<1)^0x1b)&0xff if a&0x80 else a<<1
def aes(key,blk):
    w=[list(key[i:i+4]) for i in range(0,16,4)]; rc=1
    for i in range(4,44):
        t=list(w[i-1])
        if i%4==0:
            t=t[1:]+t[:1]; t=[sbox[b] for b in t]; t[0]^=rc; rc=xt(rc)
        w.append([a^b for a,b in zip(w[i-4],t)])
    rk=[sum(w[4*r:4*r+4],[]) for r in range(11)]
    s=[a^b for a,b in zip(blk,rk[0])]
    for r in range(1,11):
        s=[sbox[b] for b in s]
        s=[s[(i+4*(i%4))%16] for i in range(16)]
        if r<10:
            n=[]
            for c in range(4):
                a=s[4*c:4*c+4]; t=a[0]^a[1]^a[2]^a[3]
                n+=[a[i]^t^xt(a[i]^a[(i+1)%4]) for i in range(4)]
            s=n
        s=[a^b for a,b in zip(s,rk[r])]
    return bytes(s)
assert aes(bytes(range(16)),bytes.fromhex('00112233445566778899aabbccddeeff')).hex()=='69c4e0d86a7b0430d8cdb78070b4c55a'
def dbl(b):
    n=int.from_bytes(b,'big')<<1
    if n>>128: n=(n&((1<<128)-1))^0x87
    return n.to_bytes(16,'big')
def cmac(k,m):
    k1=dbl(aes(k,bytes(16))); k2=dbl(k1)
    n=max(1,-(-len(m)//16)); last=m[16*(n-1):]
    if len(last)==16: last=bytes(a^b for a,b in zip(last,k1))
    else:
        last=last+b'\x80'+bytes(15-len(last)); last=bytes(a^b for a,b in zip(last,k2))
    x=bytes(16)
    for i in range(n-1): x=aes(k,bytes(a^b for a,b in zip(x,m[16*i:16*i+16])))
    return aes(k,bytes(a^b for a,b in zip(x,last)))
K=bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
assert cmac(K,b'').hex()=='bb1d6929e95937287fa37d129b756746'
assert cmac(K,bytes.fromhex('6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411')).hex()=='dfa66747de9ae63030ca32611497c827'
def ctr(k,A,m):
    o=b''
    for j in range(0,len(m),16):
        s=aes(k,A[:14]+struct.pack('>H',j//16+1)); o+=bytes(a^b for a,b in zip(m[j:j+16],s))
    return o
H=lambda s:bytes.fromhex(s.replace(' ',''))
knet=bytes(range(16)); e=1
kd=lambda p,l:cmac(p,l+bytes([e]))
he,hm,hp,ge,gm=[kd(knet,l) for l in (b'LMHE',b'LMHM',b'LMHP',b'LMGE',b'LMGM')]
kdep=bytes(range(0x40,0x60)); kmesh=hmac.new(kdep,b'LMM1',hashlib.sha256).digest()
knode=hmac.new(kmesh,b'LMN1\x03\x0a\x00',hashlib.sha256).digest()[:16]
print('kmesh',kmesh.hex().upper()==  '9518225659933114877EA98D05C6C3F49882C16FDAC302062C6F8858B3FADC1F', 'knode',knode.hex().upper()=='6F77DB437E50C7A24B5446B6F480043C')
print('K_he',he.hex().upper()=='3F190153DF3104694764132CA0361034','K_hp',hp.hex().upper()=='2977A7AE47612A1374D011B64BADF207')
# V1 beacon
P=struct.pack('<HBHHBIBB',0xBEEF,40,950,0x0A01,2,1234,5,12)
Hd=struct.pack('<HHBB',0xFFFF,0x0A02,0x80|8|0x46,14)
A=bytes([2])+struct.pack('<H',0x0A02)+bytes([0x23,0x01,0x00,0x00])+bytes(9)
C=ctr(he,A,P); mic=cmac(hm,A+Hd+C)[:3]; f=Hd+C+bytes([0x23,1,0])+mic
print('V1',f.hex().upper()=='FFFF020ACE0ED42A669BCCD9CDAFE4FB2AFB81F32301005B2772',len(f))
# V5 join
Hd=struct.pack('<HHBB',0x0A01,0x0A03,0x80|8|0x42,6); P=struct.pack('<BHHB',1,0x0A02,0x0A02,1)
A=bytes([1])+struct.pack('<HI',0x0A02,2)+bytes(9); C=ctr(he,A,P)
f=Hd+C+struct.pack('<HI',0x0A02,2)+cmac(hm,A+Hd+C)[:4]
print('V5',f.hex().upper()=='010A030ACA060170303374AC020A0200000064766412')
# V3
ee,em,ke,km=[kd(knode,l) for l in (b'LMEE',b'LMEM',b'LMKE',b'LMKM')]
def dframe(dst,src,pt,X,body,c,E,dom,kenc,kmac):
    Hd=struct.pack('<HHBB',dst,src,0x80|8|pt,len(X)+len(body))
    A=bytes([dom])+struct.pack('<HI',src,c)+bytes(9); C=ctr(kenc,A,body)
    xim=bytes(3)+X[3:]; tag=cmac(kmac,A+Hd+xim+C)[:4]; cf=struct.pack('<I',c|(E<<31))
    hmic=cmac(hm,b'\x06'+Hd+X+C+cf+tag)[:4]
    mask=aes(hp,cf+tag+hmic+struct.pack('<HH',src,dst))
    return Hd+bytes(a^b for a,b in zip(X,mask))+C+cf+tag+hmic
f1=dframe(0x0A01,0x0A03,0x11,H('020A0A37'),b'temp=21.5C',0x29,1,4,ee,em)
f2=dframe(0x0A01,0x0A03,0x11,H('010A0937'),b'temp=21.5C',0x29,1,4,ee,em)
print('V3h1',f1.hex().upper()=='010A030A990E2366EEB6204E67B19EF7FDA87A3A290000805E7BC82CCF6288E4')
print('V3h2',f2.hex().upper()=='010A030A990E58BA570C204E67B19EF7FDA87A3A290000805E7BC82C7F2F90E2')
f=dframe(0xFFFF,0x0A03,0x12,H('FFFF0A38'),b'hello mesh',0x2A,0,3,ge,gm)
print('V4',f.hex().upper()=='FFFF030A9A0EDA3C6BB5E1BFFE005EF6AF1B37A12A0000004F6D5274BFD0F592')
k2=H('A0A1A2A3A4A5A6A7A8A9AAABACADAEAF'); kcv=cmac(km,b'\x08\x03\x0a\x02\x00'+k2)[:8]
f=dframe(0x0A03,0x0A01,0x51,H('020A0A05'),b'\x02\x00'+k2+kcv,0x100,1,5,ke,km)
print('V6',f.hex().upper()=='030A010AD91E4246256A892C29831AFBF2DD1512B9FA14EEB8B1852CD4578B2822E8BA5C000100808CC2C9BCFC9B89C8',len(f))
# V2
rt=struct.pack('<HBBBBB',0x0A01,7,2,0,2,1)+struct.pack('<HBBBBBBH',0x0A01,1,230,2,1,0,220,0x0A01)+struct.pack('<HBBBBBBH',0x0A03,1,200,1,0,2,190,0x0A03)
Hd=struct.pack('<HHBB',0xFFFF,0x0A02,0x80|8|0x32,len(rt)); A=bytes([1])+struct.pack('<HI',0x0A02,0x457)+bytes(9); C=ctr(he,A,rt)
f=Hd+C+struct.pack('<I',0x457)+cmac(hm,A+Hd+C)[:4]
print('V2',f.hex().upper()=='FFFF020ABA1BD2FDA910C18F3255A05278A8B3F3C6BA949FBCD5619BA24EC88CF95704000074482AA8')
def toa(n,sf=9,bw=125e3,cr=3):
    ts=(2**sf)/bw*1000; ps=8+max(math.ceil((8*n-4*sf+28+16)/(4*sf))*(cr+4),0)
    return (12.25+ps)*ts
for n in (20,22,26,27,30,48,101,107,108,115): print(n,round(toa(n)),end='; ')
