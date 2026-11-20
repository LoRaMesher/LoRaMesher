#!/usr/bin/env python3
"""Throwaway generator for the LoRaMesher security spec: airtime table,
subslot band check and test vectors. Needs the `cryptography` package."""
import struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import cmac as _cmac, hashes, hmac as _hmac


def aes(key, block):
    assert len(block) == 16
    e = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return e.update(block) + e.finalize()


def cmac(key, msg):
    c = _cmac.CMAC(algorithms.AES(key))
    c.update(msg)
    return c.finalize()


def hmac256(key, msg):
    h = _hmac.HMAC(key, hashes.SHA256())
    h.update(msg)
    return h.finalize()


def hx(b):
    return b.hex().upper()


# ---------------------------------------------------------------- airtime
def toa(n, sf=9, bw=125.0, crd=7, pre=8, crc=1, ih=0):
    tsym = (1 << sf) / (bw * 1000.0) * 1000.0
    de = 1 if tsym >= 16.0 else 0
    num = 8 * n - 4 * sf + 28 + 16 * crc - 20 * ih
    den = 4 * (sf - 2 * de)
    blocks = (num + den - 1) // den if num > 0 else 0
    sym = 8 + blocks * (crd - 4 + 4)
    return int((pre + 4.25) * tsym + sym * tsym + 0.5)


def subslot(slot, guard, nmax, t):
    """Returns (n kept, offsets, list of subslots where CanFitInSlot fails)."""
    def layout(n):
        tg = n * guard
        if tg >= slot:
            return None
        tr = min(30, slot // 10)
        if tg + tr >= slot:
            tr = 0
        txw = (slot - tg - tr) // n
        if txw == 0:
            return None
        return guard + txw
    eff = 1
    for n in range(nmax, 0, -1):
        sd = layout(n)
        if sd is not None and (n - 1) * sd + guard + t <= slot:
            eff = n
            break
    sd = layout(eff)
    offs = [k * sd + guard for k in range(eff)]
    bad = [k for k, o in enumerate(offs) if o + t + 20 > slot]
    return eff, offs, bad


# ---------------------------------------------------------------- crypto
def kdf(parent, label, epoch):
    return cmac(parent, label + bytes([epoch]))


def nonce_block(domain, sender, counter):
    return bytes([domain]) + struct.pack('<H', sender) + struct.pack('<I', counter) + bytes(9)


def ctr_xor(key, a, data):
    out = bytearray()
    j = 1
    for i in range(0, len(data), 16):
        ks = aes(key, a[:14] + struct.pack('>H', j))
        chunk = data[i:i + 16]
        out += bytes(x ^ y for x, y in zip(chunk, ks))
        j += 1
    return bytes(out), None


def wire_type(plain, epoch):
    return 0x80 | ((epoch & 1) << 3) | plain


def base_header(dst, src, wtype, size):
    return struct.pack('<HHBB', dst, src, wtype, size)


def seal_control(keys, epoch, dst, src, ptype, body, counter, beacon=False, tx=None, bound=b''):
    """tx is set for JOIN_REQUEST / JOIN_RESPONSE (class C2). `bound` is
    appended to the MIC input only (join nonce of the final JOIN_RESPONSE)."""
    if beacon:
        assert len(body) == 14 and body[11] == 0
        body = body[:11] + body[12:]          # delay sent in 3 bytes
    h = base_header(dst, src, wire_type(ptype, epoch), len(body))
    sender = tx if tx is not None else src
    a = nonce_block(0x02 if beacon else 0x01, sender, counter)
    c, _ = ctr_xor(keys['he'], a, body)
    full = cmac(keys['hm'], a + h + c + bound)
    if beacon:
        trailer = struct.pack('<I', counter)[:3] + full[:4]
    elif tx is not None:
        trailer = struct.pack('<H', tx) + struct.pack('<I', counter) + full[:4]
    else:
        trailer = struct.pack('<I', counter) + full[:4]
    return dict(H=h, A=a, C=c, cmac=full, trailer=trailer, frame=h + c + trailer, wire_body=body)


def data_payload_layer(ke, km, domain, h, x, body, src, counter):
    a = nonce_block(domain, src, counter)
    c, _ = ctr_xor(ke, a, body)
    x_im = bytes(3) + x[3:]
    full = cmac(km, a + h + x_im + c)
    return a, c, x_im, full


def data_hop_layer(keys, h, x, c, ctr_field, tag, src, dst):
    pre = h + x + c + ctr_field + tag
    full = cmac(keys['hm'], b'\x06' + pre)
    hmic = full[:4]
    sample = ctr_field + tag + hmic + struct.pack('<HH', src, dst)
    mask_block = aes(keys['hp'], sample)
    xm = bytes(p ^ m for p, m in zip(x, mask_block))
    return dict(hop_cmac=full, hmic=hmic, sample=sample, mask_block=mask_block,
                X_masked=xm, frame=h + xm + c + ctr_field + tag + hmic)


def show(title, d, order):
    print(f'\n### {title}')
    for k in order:
        v = d[k]
        print(f'{k:14s} = {hx(v) if isinstance(v, (bytes, bytearray)) else v}')


def main():
    print('== airtime SF9 BW125 CR4/7 preamble 8 CRC on explicit header')
    for n in (12, 20, 22, 26, 30, 40, 42, 58, 68, 115):
        print(n, toa(n))
    print('== airtime by size 7..115')
    prev = None
    start = 7
    for n in range(7, 117):
        t = toa(n) if n <= 115 else None
        if t != prev:
            if prev is not None:
                eff, offs, bad = subslot(950, 50, 5, prev)
                print(f'{start:3d}-{n - 1:3d} B  ToA {prev:3d} ms  subslots {eff} offs {offs} unsafe {bad}')
            start, prev = n, t

    # ------------------------------------------------------------ keys
    k_dep = bytes(range(0x40, 0x60))
    k_mesh = hmac256(k_dep, b'LMM1')
    gw, n1, n2 = 0x0A01, 0x0A02, 0x0A03
    k_node_n3 = hmac256(k_mesh, b'LMN1' + struct.pack('<H', n2) + b'\x00')[:16]
    print('\n== server side derivation')
    print('K_dep        =', hx(k_dep))
    print('K_mesh       =', hx(k_mesh))
    print('K_node[0A03] =', hx(k_node_n3), '(generation 0)')

    epoch = 1
    k_net = bytes(range(16))
    keys = {
        'he': kdf(k_net, b'LMHE', epoch), 'hm': kdf(k_net, b'LMHM', epoch),
        'hp': kdf(k_net, b'LMHP', epoch), 'ge': kdf(k_net, b'LMGE', epoch),
        'gm': kdf(k_net, b'LMGM', epoch),
    }
    k_ee = kdf(k_node_n3, b'LMEE', epoch)
    k_em = kdf(k_node_n3, b'LMEM', epoch)
    k_ke = kdf(k_node_n3, b'LMKE', epoch)
    k_km = kdf(k_node_n3, b'LMKM', epoch)
    print('\n== node side derivation, epoch', epoch)
    print('K_net =', hx(k_net))
    for k in ('he', 'hm', 'hp', 'ge', 'gm'):
        print(f'K_{k}  =', hx(keys[k]))
    print('K_ee  =', hx(k_ee))
    print('K_em  =', hx(k_em))
    print('K_ke  =', hx(k_ke))
    print('K_km  =', hx(k_km))
    print('CMAC check RFC 4493 ex.2:',
          hx(cmac(bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c'),
                  bytes.fromhex('6bc1bee22e409f96e93d7e117393172a'))))

    # ------------------------------------------------------------ V1 beacon
    body = struct.pack('<HBHHBIBB', 0xBEEF, 40, 950, gw, 2, 1234, 5, 12)
    v = seal_control(keys, epoch, 0xFFFF, n1, 0x46, body, 0x000123, beacon=True)
    v['body'] = body
    show('V1 SYNC_BEACON forwarded by 0x0A02, C_bcn = 0x000123', v,
         ['body', 'wire_body', 'H', 'A', 'C', 'cmac', 'trailer', 'frame'])
    print('length', len(v['frame']))

    # ------------------------------------------------------------ V2 routing
    rt = struct.pack('<HBBBBB', gw, 7, 2, 0x00, 2, 1)
    rt += struct.pack('<HBBBBBBH', gw, 1, 230, 2, 0x01, 0, 220, gw)
    rt += struct.pack('<HBBBBBBH', n2, 1, 200, 1, 0x00, 2, 190, n2)
    v = seal_control(keys, epoch, 0xFFFF, n1, 0x32, rt, 0x00000457)
    v['body'] = rt
    a = v['A']
    v['S1'] = aes(keys['he'], a[:14] + b'\x00\x01')
    v['S2'] = aes(keys['he'], a[:14] + b'\x00\x02')
    show('V2 ROUTE_TABLE fragment from 0x0A02, 2 entries, C_ctl = 0x00000457', v,
         ['H', 'body', 'A', 'S1', 'S2', 'C', 'cmac', 'trailer', 'frame'])
    print('length', len(v['frame']))

    # ------------------------------------------------------------ V3 data e2e
    app = b'temp=21.5C'
    counter = 0x00000029
    ctr_field = struct.pack('<I', counter | 0x80000000)
    h = base_header(gw, n2, wire_type(0x11, epoch), 4 + len(app))
    x1 = struct.pack('<HBB', n1, 10, 0x37)
    a, c, x_im, full = data_payload_layer(k_ee, k_em, 0x04, h, x1, app, n2, counter)
    tag = full[:4]
    print('\n### V3 DATA end to end 0x0A03 -> 0x0A01 via relay 0x0A02, C_data = 0x29, E = 1')
    print('app payload    =', hx(app))
    print('H              =', hx(h))
    print('ctr field      =', hx(ctr_field))
    print('A_p            =', hx(a))
    print('S1             =', hx(aes(k_ee, a[:14] + b'\x00\x01')))
    print('C              =', hx(c))
    print('X_im           =', hx(x_im))
    print('payload cmac   =', hx(full))
    print('tag            =', hx(tag))
    hop1 = data_hop_layer(keys, h, x1, c, ctr_field, tag, n2, gw)
    hop1['X'] = x1
    show('V3 hop 1, sent by 0x0A03 (next_hop 0x0A02, ttl 10)', hop1,
         ['X', 'hop_cmac', 'hmic', 'sample', 'mask_block', 'X_masked', 'frame'])
    print('length', len(hop1['frame']))
    x2 = struct.pack('<HBB', gw, 9, 0x37)
    hop2 = data_hop_layer(keys, h, x2, c, ctr_field, tag, n2, gw)
    hop2['X'] = x2
    show('V3 hop 2, sent by relay 0x0A02 (next_hop 0x0A01, ttl 9)', hop2,
         ['X', 'hop_cmac', 'hmic', 'sample', 'mask_block', 'X_masked', 'frame'])
    print('length', len(hop2['frame']))

    # ------------------------------------------------------------ V4 broadcast
    app = b'hello mesh'
    counter = 0x0000002A
    ctr_field = struct.pack('<I', counter)
    h = base_header(0xFFFF, n2, wire_type(0x12, epoch), 4 + len(app))
    x = struct.pack('<HBB', 0xFFFF, 10, 0x38)
    a, c, x_im, full = data_payload_layer(keys['ge'], keys['gm'], 0x03, h, x, app, n2, counter)
    tag = full[:4]
    print('\n### V4 DATA_BROADCAST from 0x0A03, C_data = 0x2A, E = 0')
    print('app payload    =', hx(app))
    print('H              =', hx(h))
    print('ctr field      =', hx(ctr_field))
    print('A_p            =', hx(a))
    print('C              =', hx(c))
    print('X_im           =', hx(x_im))
    print('payload cmac   =', hx(full))
    hop = data_hop_layer(keys, h, x, c, ctr_field, tag, n2, 0xFFFF)
    hop['X'] = x
    show('V4 on air', hop, ['X', 'hop_cmac', 'hmic', 'sample', 'mask_block', 'X_masked', 'frame'])

    # ------------------------------------------------------------ V5 join
    join_nonce = bytes.fromhex('C0FFEE42')
    jr = struct.pack('<BHHB', 1, n1, n1, 0) + join_nonce
    v = seal_control(keys, epoch, gw, n2, 0x42, jr, 0x00000000, tx=n2)
    v['body'] = jr
    show('V5a JOIN_REQUEST sent by joiner 0x0A03 to sponsor 0x0A02, C_ctl(0A03) = 0', v,
         ['H', 'body', 'A', 'C', 'cmac', 'trailer', 'frame'])
    print('length', len(v['frame']))
    jr = struct.pack('<BHHB', 1, gw, n1, 1)
    v = seal_control(keys, epoch, gw, n2, 0x42, jr, 0x00000002, tx=n1)
    v['body'] = jr
    show('V5b JOIN_REQUEST forwarded by 0x0A02 (nonce removed), C_ctl(0A02) = 2', v,
         ['H', 'body', 'A', 'C', 'cmac', 'trailer', 'frame'])
    print('length', len(v['frame']))
    jp = struct.pack('<HBBHHB', 0xBEEF, 1, 0, n2, 0, 2)
    v = seal_control(keys, epoch, n2, gw, 0x43, jp, 0x00000003, tx=n1, bound=join_nonce)
    v['body'] = jp
    show('V5c final JOIN_RESPONSE sponsor 0x0A02 -> joiner 0x0A03, C_ctl(0A02) = 3, nonce in MIC', v,
         ['H', 'body', 'A', 'C', 'cmac', 'trailer', 'frame'])
    print('length', len(v['frame']))

    # ------------------------------------------------------------ V6 key update
    k_new = bytes(range(0xA0, 0xB0))
    new_epoch, flags = 2, 0
    kcv = cmac(k_km, b'\x08' + struct.pack('<H', n2) + bytes([new_epoch, flags]) + k_new)[:8]
    body = bytes([new_epoch, flags]) + k_new + kcv
    counter = 0x00000100
    ctr_field = struct.pack('<I', counter | 0x80000000)
    h = base_header(n2, gw, wire_type(0x51, epoch), 4 + len(body))
    x = struct.pack('<HBB', n1, 10, 0x05)
    a, c, x_im, full = data_payload_layer(k_ke, k_km, 0x05, h, x, body, gw, counter)
    tag = full[:4]
    print('\n### V6 KEY_UPDATE 0x0A01 -> 0x0A03 (new epoch 2), C_data(gw) = 0x100')
    print('kcv            =', hx(kcv))
    print('body           =', hx(body))
    print('H              =', hx(h))
    print('A_p            =', hx(a))
    print('C              =', hx(c))
    print('payload cmac   =', hx(full))
    hop = data_hop_layer(keys, h, x, c, ctr_field, tag, gw, n2)
    hop['X'] = x
    show('V6 on air', hop, ['X', 'hop_cmac', 'hmic', 'sample', 'mask_block', 'X_masked', 'frame'])
    print('length', len(hop['frame']))

    # ------------------------------------------------------------ V7 recovery
    k_re = cmac(k_node_n3, b'LMRE' + b'\x00')
    k_rm = cmac(k_node_n3, b'LMRM' + b'\x00')
    c_rec, held, new_ep = 0x00000007, 1, 3
    k_net3 = bytes(range(0xB0, 0xC0))
    a_r = nonce_block(0x09, n2, c_rec)
    rtag = cmac(k_rm, a_r + bytes([held]))[:8]
    req_body = bytes([held]) + struct.pack('<I', c_rec) + rtag
    req = base_header(n1, n2, 0xD3, len(req_body)) + req_body
    a_g = bytearray(nonce_block(0x0A, n2, c_rec)); a_g[7] = new_ep; a_g = bytes(a_g)
    cg, _ = ctr_xor(k_re, a_g, k_net3)
    gtag = cmac(k_rm, a_g + cg)[:8]
    grant_body = bytes([new_ep]) + cg + gtag
    grant = base_header(n2, n1, 0xD4, len(grant_body)) + grant_body
    print('\n### V7 recovery of 0x0A03 (holds epoch 1) through sponsor 0x0A02, network at epoch 3')
    for k, val in (('K_re', k_re), ('K_rm', k_rm), ('A_r', a_r), ('request tag', rtag),
                   ('KEY_REQUEST', req), ('K_net[3]', k_net3), ('A_g', a_g), ('C_g', cg),
                   ('grant tag', gtag), ('KEY_GRANT', grant)):
        print(f'{k:14s} = {hx(val)}')
    print('lengths', len(req), len(grant))
    up = b'\x00' + struct.pack('<H', n2) + req_body
    down = b'\x01' + struct.pack('<H', n2) + grant_body
    print('KEY_RELAY up body  =', hx(up), 'frame bytes', 6 + 4 + len(up) + 12)
    print('KEY_RELAY down body=', hx(down), 'frame bytes', 6 + 4 + len(down) + 12)
    for n in (19, 31, 38, 50, 26, 25, 22):
        print('toa', n, toa(n))


if __name__ == '__main__':
    main()
