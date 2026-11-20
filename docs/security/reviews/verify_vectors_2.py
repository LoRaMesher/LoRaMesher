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
ee,em,ke,km=[kd(knode,l) for l in (b'LMEE',b'LMEM',b'LMKE',b'LMKM')]
kre=cmac(knode,b'LMRE\x00'); krm=cmac(knode,b'LMRM\x00')
print('K_re',kre.hex().upper()=='30E06F4E6E136E109E51029C8EDEE484','K_rm',krm.hex().upper()=='140EA8FFE794FEE93CBE35BF73FE178D')
# V1: 13-byte body
P14=struct.pack('<HBHHBIBB',0xBEEF,40,950,0x0A01,2,1234,5,12); assert P14[11]==0
P=P14[:11]+P14[12:]
Hd=struct.pack('<HHBB',0xFFFF,0x0A02,0xCE,13)
A=bytes([2])+struct.pack('<H',0x0A02)+bytes([0x23,1,0,0])+bytes(9)
C=ctr(he,A,P); f=Hd+C+bytes([0x23,1,0])+cmac(hm,A+Hd+C)[:4]
print('V1',f.hex().upper()=='FFFF020ACE0DD42A669BCCD9CDAFE4FB2AFE8823010086E3AF6E',len(f))
def c2(dst,src,pt,P,tx,c,B=b''):
    Hd=struct.pack('<HHBB',dst,src,0x88|pt,len(P)); A=bytes([1])+struct.pack('<HI',tx,c)+bytes(9); C=ctr(he,A,P)
    return Hd+C+struct.pack('<HI',tx,c)+cmac(hm,A+Hd+C+B)[:4]
N=H('C0FFEE42')
a=c2(0x0A01,0x0A03,0x42,struct.pack('<BHHB',1,0x0A02,0x0A02,0)+N,0x0A03,0)
b=c2(0x0A01,0x0A03,0x42,struct.pack('<BHHB',1,0x0A01,0x0A02,1),0x0A02,2)
c=c2(0x0A03,0x0A01,0x43,struct.pack('<HBBHHB',0xBEEF,1,0,0x0A03,0,2),0x0A02,3,N)
print('V5a',a.hex().upper()=='010A030ACA0A566DDDC4E6B7A23E0525030A00000000241A769C',len(a))
print('V5b',b.hex().upper()=='010A030ACA060173303374AC020A02000000789342C9',len(b))
print('V5c',c.hex().upper()=='030A010ACB09E8BDEC55C0D6D23B05020A03000000C3D5D4EB',len(c))
Ar=bytes([9])+struct.pack('<HI',0x0A03,7)+bytes(9); rtag=cmac(krm,Ar+b'\x01')[:8]
req=struct.pack('<HHBB',0x0A02,0x0A03,0xD3,13)+b'\x01'+struct.pack('<I',7)+rtag
k3=bytes(range(0xB0,0xC0)); Ag=bytes([0x0A])+struct.pack('<HI',0x0A03,7)+b'\x03'+bytes(8)
cg=ctr(kre,Ag,k3); gt=cmac(krm,Ag+cg)[:8]
gr=struct.pack('<HHBB',0x0A03,0x0A02,0xD4,25)+b'\x03'+cg+gt
print('V7req',req.hex().upper()=='020A030AD30D0107000000261F520C68E88AFD',len(req))
print('V7grant',gr.hex().upper()=='030A020AD41903DAC4A8CE528B55C9F0BB2FEB5697BD6BBAC34B148A020B19',len(gr))
def toa(n,sf=9,bw=125e3,cr=3):
    ts=(2**sf)/bw*1000; ps=8+max(math.ceil((8*n-4*sf+28+16)/(4*sf))*(cr+4),0)
    return (12.25+ps)*ts
for n in (19,22,25,26,31,38,50): print(n,round(toa(n)),end='; ')
print()
# ramp simulation: crash at any point, check never value>=persisted limit used, and cost 3n+1
def run(B,n):
    limit=0; nxt=0; k=0; maxcost=0
    # simulate: reserve when needed (first need), refill after seal if limit-next<=b//4
    b=0
    for i in range(n):
        if k==0 or nxt>=limit:
            k+=1; b=min(2**(k-1),B); limit=nxt+b
        assert nxt<limit; nxt+=1
        if limit-nxt<=b//4:
            k+=1; b=min(2**(k-1),B); limit=nxt+b
        maxcost=max(maxcost,limit)
    return limit
w=max((run(64,n)-(3*n+1),n) for n in range(1,2000)); print('max(limit-(3n+1)) B=64',w)
w=max((run(256,n)-(3*n+1),n) for n in range(1,5000)); print('B=256',w)
print([ (n,run(64,n)) for n in (1,2,3,4,5,48)])
y=365.25*86400
for name,vals in (('bcn',2**24-1),('ctl',2**32-1),('data',2**30-1)):
    boots=vals//3; print(name,boots,boots*77/y,'y',boots*5/86400,'d',boots*5/y,'y', vals*77/y)
