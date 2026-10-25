"""Reading the game files the mod is built from.

MT archives (ARC), the stages' gathering-point sets (IPS) and their item lot tables (IPL), the item table, and the
rGUI layout of the map icons.
"""
import os
import struct
import zlib
from collections import Counter

NATIVE = os.path.join('romfs', 'nativeNX')
MAP_COM = os.path.join(NATIVE, 'loc', 'arc', 'stage', 'map_com')
TYPE_IPS, TYPE_IPL, TYPE_GUI = 0x52776AB1, 0x25FD693F, 0x22948394


# ---- MT archives

def arc_entries(arc):
    magic, _version, count = struct.unpack_from('<4sHH', arc, 0)
    assert magic == b'ARC\0', 'not an MT archive'
    out = []
    for i in range(count):
        off = 12 + i * 0x50
        name = arc[off:off + 0x40].split(b'\0')[0].decode('latin1')
        type_hash, csize, dsize, data_off = struct.unpack_from('<IIII', arc, off + 0x40)
        out.append((off, name, type_hash, csize, dsize, data_off))
    return out


def arc_file(path, name, type_hash):
    arc = open(path, 'rb').read()
    for _off, n, th, csize, _dsize, data_off in arc_entries(arc):
        if n.lower() == name.lower() and th == type_hash:
            return zlib.decompress(arc[data_off:data_off + csize])
    return None


def source(root, rel, skip_mod=None):
    """The file the game loads for rel: the last mod in folder order that has it, else the game's own."""
    mods = os.path.join(root, 'mods')
    for name in sorted(os.listdir(mods), key=str.lower, reverse=True):
        path = os.path.join(mods, name, rel)
        if name != skip_mod and os.path.isfile(path):
            return path
    return os.path.join(root, 'data', rel)


# ---- items

def item_table(root):
    data = arc_file(source(root, os.path.join(NATIVE, 'loc', 'arc', 'resident.arc')), 'table\\itemData', 0x7C4883A8)
    count = struct.unpack_from('<I', data, 4)[0]
    # Each 44-byte entry has the item's icon (cmn_icon cell) at +16 and its color at +17.
    return [dict(icon=data[8 + 44 * i + 16], color=data[8 + 44 * i + 17]) for i in range(count)]


# ---- gathering points

def parse_ips(d):
    """Gathering points per area: lot index, world position and type bytes (class, model)."""
    assert d[:4] == b'IPS\0', 'not an IPS file'
    points = []
    for area in range(struct.unpack_from('<I', d, 8)[0]):
        off, count = struct.unpack_from('<II', d, 0x34 + area * 8)
        for i in range(count):
            o = 0x34 + off + i * 36
            lot, x, y, z = struct.unpack_from('<Ifff', d, o)
            points.append(dict(area=area, lot=lot, pos=(x, y, z), cls=d[o + 30], model=d[o + 31]))
    return points


def parse_ipl(d):
    """Item lots: (weight, quantity, item) per lot, as the point's lot index picks them."""
    assert d[:4] == b'IPL\0', 'not an IPL file'
    lots = []
    for i in range(struct.unpack_from('<I', d, 0x10)[0]):
        off, count = struct.unpack_from('<II', d, 0x24 + i * 8)
        lots.append([struct.unpack_from('<BBH', d, 0x24 + off + 4 * k) for k in range(count)])
    return lots


CLASS_MINING, CLASS_BUG, CLASS_HIDDEN = 2, 3, 254
MODEL_HIDDEN = 37
COLOR_GRAY = 10

# The icons the map can show: item icons (cells of the common icon texture). Anything else shows as a bag.
ICON_MUSHROOM, ICON_HERB, ICON_HONEY, ICON_BUG, ICON_WEB, ICON_BERRY, ICON_ORE, ICON_BONE = 1, 3, 5, 6, 7, 8, 9, 14
ICON_EGG, ICON_DUNG, ICON_BAIT, ICON_STONE, ICON_WOOD = 15, 16, 18, 42, 93
KINDS = [ICON_ORE, ICON_BONE, ICON_BUG, ICON_HERB, ICON_MUSHROOM, ICON_HONEY, ICON_BERRY, ICON_WEB,
         ICON_EGG, ICON_DUNG, ICON_STONE, ICON_BAIT, ICON_WOOD]
BAG = None
# What a spot's model (IPS byte 31) looks like, where that settles it.
MODEL_KIND = {1: ICON_BERRY, 2: ICON_BERRY, 3: ICON_HERB, 4: ICON_HERB, 5: ICON_MUSHROOM, 6: ICON_MUSHROOM,
              7: ICON_BONE, 8: ICON_BONE, 9: ICON_ORE, 10: ICON_ORE, 11: ICON_HONEY, 12: ICON_HONEY, 20: ICON_WEB}


def point_icon(p, lotsets, items):
    """The (kind, color) a point shows: kind is one of KINDS or BAG, color the item color index.

    Mining and bug spots show ore and bug, other spots what their model looks like, else their most likely
    item's icon; the color is that of the most likely item of that icon. None for points never seen as a spot:
    class 254 and model 37 ones sit in nearly every area of every map.
    """
    if p['cls'] == CLASS_HIDDEN or p['model'] == MODEL_HIDDEN:
        return None
    score = Counter()
    for lots in lotsets:
        if p['lot'] < len(lots):
            for weight, _qty, item in lots[p['lot']]:
                score[(items[item]['icon'], items[item]['color'])] += weight
    if not score:
        return None
    kind = {CLASS_MINING: ICON_ORE, CLASS_BUG: ICON_BUG}.get(p['cls']) or MODEL_KIND.get(p['model'])
    if kind is None:
        top_icon = score.most_common(1)[0][0][0]
        kind = top_icon if top_icon in KINDS else BAG
    same = [(k, v) for k, v in score.items() if k[0] == kind]
    color = max(same, key=lambda kv: kv[1])[0][1] if same else score.most_common(1)[0][0][1]
    if kind in (ICON_ORE, ICON_BUG, ICON_BONE) and not same:
        color = COLOR_GRAY
    return kind, color


def stage_points(root, items):
    """[(map number, [(point, (icon, color))])] for every map with gathering points."""
    out = []
    for n in range(1, 28):
        path = source(root, os.path.join(MAP_COM, 'm%02d.arc' % n))
        if not os.path.isfile(path):
            continue
        ips = arc_file(path, 'stage\\m%02dd\\m%02dd' % (n, n), TYPE_IPS)
        if ips is None:
            continue
        lotsets = []
        for rank in ('low', 'high', 'g'):
            ipl = arc_file(path, 'stage\\m%02dd\\m%02dd_%s' % (n, n, rank), TYPE_IPL)
            if ipl:
                lotsets.append(parse_ipl(ipl))
        shown = [(p, point_icon(p, lotsets, items)) for p in parse_ips(ips)]
        out.append((n, [(p, ic) for p, ic in shown if ic is not None]))
    return out


# ---- rGUI (MHGU layout, after Fexty12573's mhw-gui-editor)

HEADER = ('instanceID flowID variableID startInstanceIndex animationNum sequenceNum objectNum objSequenceNum '
          'initParamNum paramNum keyNum instanceNum flowNum flowProcessNum flowInputNum flowSwitchNum flowFunctionNum '
          'actionNum inputConditionNum switchConditionNum switchOperatorNum variableNum textureNum fontNum fontFilterNum '
          'messageNum guiResourceNum generalResourceNum cameraSettingNum instExeParamNum vertexBufferSize optionBitFlag '
          'viewSize_Width viewSize_Height startFlowIndex animationOffset sequenceOffset objectOffset objSequenceOffset '
          'initParamOffset paramOffset instanceOffset flowOffset flowProcessOffset flowInputOffset flowSwitchOffset '
          'flowFunctionOffset actionOffset inputConditionOffset switchOperatorOffset switchConditionOffset '
          'variableOffset textureOffset fontOffset fontFilterOffset messageOffset guiResourceOffset '
          'generalResourceOffset cameraSettingOffset stringOffset keyOffset keyValue8Offset keyValue32Offset '
          'keyValue128Offset extendDataOffset instExeParamOffset vertexOffset').split()
HEADER_AT = 0x18
ANIM, SEQ, OBJ, OBJSEQ, INITPARAM = 24, 12, 36, 16, 20
OBJ_TYPE_TEXTURE = 459477923


class Gui:
    def __init__(self, data):
        assert data[:4] == b'GUI\0', 'not an rGUI file'
        self.d = bytearray(data)
        self.h = dict(zip(HEADER, struct.unpack_from('<%dI' % len(HEADER), data, HEADER_AT)))

    def field(self, name):
        return HEADER_AT + 4 * HEADER.index(name)

    def set(self, name, value):
        self.h[name] = value
        struct.pack_into('<I', self.d, self.field(name), value)

    def string(self, off):
        a = self.h['stringOffset'] + off
        return self.d[a:self.d.index(0, a)].decode('latin1')

    def row(self, table, size, i):
        a = self.h[table + 'Offset'] + i * size
        return self.d[a:a + size]

    def anim(self, i):
        _id, objs, seqs, _draw, _anim, root, name, seq_index = struct.unpack_from('<IHHHHIII', self.row('animation', ANIM, i))
        return dict(objs=objs, seqs=seqs, root=root, name=self.string(name), seq_index=seq_index)

    def seq(self, i):
        sid, _frames, name = struct.unpack_from('<III', self.row('sequence', SEQ, i))
        return dict(id=sid, name=self.string(name))

    def obj(self, i):
        v = struct.unpack_from('<IBBHiiIIIIi', self.row('object', OBJ, i))
        return dict(name=self.string(v[6]), type=v[7], ip_index=v[8], ip_num=v[1], os_index=v[9])

    def oseq(self, i):
        _attr, ip_num, _p_num, _loop, _frames, ip_index, _p_index = struct.unpack_from('<HBBhhII', self.row('objSequence', OBJSEQ, i))
        return dict(ip_num=ip_num, ip_index=ip_index)

    def iparam_name(self, i):
        return self.string(struct.unpack_from('<I', self.row('initParam', INITPARAM, i), 12)[0])

    def iparam_u32(self, i):
        return struct.unpack_from('<I', self.d, self.h['keyValue32Offset'] + struct.unpack_from('<I', self.row('initParam', INITPARAM, i), 16)[0])[0]

    def texture_size(self, tex_id):
        """The size a texture is laid out in (the GUI's texture table), whatever size the texture file has."""
        for i in range(self.h['textureNum']):
            tid, _meta, _left, _top, w, h = struct.unpack_from('<IIHHHH', self.row('texture', 64, i))
            if tid == tex_id:
                return w, h
        raise KeyError(tex_id)


def _append(d, blob, align=16):
    while len(d) % align:
        d.append(0)
    at = len(d)
    d += blob
    return at


def add_icon_sequences(data, icons, first_id, name_fmt='gather_%02d'):
    """Adds one sequence per (rect, color) in icons to the map icons' "icn" animation.

    Each sequence cuts the icon object's texture (the map icon texture) to rect (x0, y0, x1, y1, in the GUI's
    layout size of that texture) and tints it (r, g, b, a), as the game's own em_otosimono sequence does. (A
    sequence that also switches the texture keeps the texture's default cut.) New tables go to the end of the file
    and the header points at them; the old ones stay unused.
    """
    g = Gui(data)
    h = g.h
    n_anim = h['animationNum']
    icn = g.anim(n_anim - 1)
    assert icn['name'] == 'icn' and icn['seq_index'] + icn['seqs'] == h['sequenceNum'], 'unexpected map_icon layout'
    objs = list(range(icn['root'], icn['root'] + icn['objs']))
    assert objs[-1] == h['objectNum'] - 1 and len(objs) == 2, 'unexpected map_icon objects'
    root_obj, tex_obj = (g.obj(i) for i in objs)
    assert tex_obj['type'] == OBJ_TYPE_TEXTURE
    assert root_obj['os_index'] + icn['seqs'] == tex_obj['os_index'] and tex_obj['os_index'] + icn['seqs'] == h['objSequenceNum']

    # Template: the dropped-item sequence (color + rect).
    names = [g.seq(icn['seq_index'] + s)['name'] for s in range(icn['seqs'])]
    drop = names.index('em_otosimono')
    drop_os = g.oseq(tex_obj['os_index'] + drop)
    tmpl = {g.iparam_name(drop_os['ip_index'] + k): bytes(g.row('initParam', INITPARAM, drop_os['ip_index'] + k))
            for k in range(drop_os['ip_num'])}
    assert sorted(tmpl) == ['VertexColor', 'mTextureRect'], sorted(tmpl)
    root_tmpl = bytes(g.row('objSequence', OBJSEQ, root_obj['os_index'] + drop))
    tex_tmpl = bytes(g.row('objSequence', OBJSEQ, tex_obj['os_index'] + drop))

    d = g.d
    k = len(icons)
    vec = bytearray()
    for rect, color in icons:
        vec += struct.pack('<4f', *color) + struct.pack('<4f', *rect)
    kv128 = _append(d, bytes(vec)) - h['keyValue128Offset']
    strs = bytearray()
    name_offs = []
    for i in range(k):
        name_offs.append(len(strs))
        strs += (name_fmt % i).encode() + b'\0'
    str_base = _append(d, bytes(strs), 4) - h['stringOffset']

    seqs = bytearray(d[h['sequenceOffset']:h['sequenceOffset'] + SEQ * h['sequenceNum']])
    for i in range(k):
        seqs += struct.pack('<III', first_id + i, 0, str_base + name_offs[i])

    params = bytearray(d[h['initParamOffset']:h['initParamOffset'] + INITPARAM * h['initParamNum']])
    first_param = h['initParamNum']
    for i in range(k):
        for name, value in (('VertexColor', kv128 + 32 * i), ('mTextureRect', kv128 + 32 * i + 16)):
            row = bytearray(tmpl[name])
            struct.pack_into('<I', row, 16, value)
            params += row

    old = d[h['objSequenceOffset']:h['objSequenceOffset'] + OBJSEQ * h['objSequenceNum']]
    split = OBJSEQ * tex_obj['os_index']
    oseqs = bytearray(old[:split])
    for i in range(k):
        oseqs += root_tmpl
    oseqs += old[split:]
    for i in range(k):
        row = bytearray(tex_tmpl)
        struct.pack_into('<I', row, 8, first_param + 2 * i)
        oseqs += row

    g.set('sequenceOffset', _append(d, bytes(seqs)))
    g.set('initParamOffset', _append(d, bytes(params)))
    g.set('objSequenceOffset', _append(d, bytes(oseqs)))
    g.set('sequenceNum', h['sequenceNum'] + k)
    g.set('initParamNum', h['initParamNum'] + 2 * k)
    g.set('objSequenceNum', h['objSequenceNum'] + 2 * k)
    struct.pack_into('<H', d, h['animationOffset'] + (n_anim - 1) * ANIM + 6, icn['seqs'] + k)
    struct.pack_into('<I', d, h['objectOffset'] + objs[1] * OBJ + 28, tex_obj['os_index'] + k)
    _append(d, b'')
    struct.pack_into('<I', d, 8, len(d))
    return bytes(d)


# ---- textures (RGBA8, Switch block-linear: 64-byte x 8-row GOBs, 16 GOBs per block)

def _gob_addr(xb, y, width_bytes, bh=16):
    gobs_x = (width_bytes + 63) // 64
    block = ((y // 8) // bh * gobs_x + xb // 64) * bh + (y // 8) % bh
    xr, yr = xb % 64, y % 8
    return block * 512 + ((xr // 32) << 8) + ((yr // 2) << 6) + (((xr % 32) // 16) << 5) + ((yr % 2) << 4) + xr % 16


def tex_rgba(tex):
    """(width, height, linear RGBA) of an RGBA8 texture file."""
    w2 = struct.unpack_from('<I', tex, 8)[0]
    w, h = (w2 >> 6) & 0x1FFF, (w2 >> 19) & 0x1FFF
    assert tex[0xD] == 7, 'not an RGBA8 texture'
    out, wb = bytearray(w * h * 4), w * 4
    for y in range(h):
        for x in range(0, wb, 16):
            a = 0x18 + _gob_addr(x, y, wb)
            out[y * wb + x:y * wb + x + 16] = tex[a:a + 16]
    return w, h, out


def tex_with_rgba(tex, w, h, rgba):
    tex, wb = bytearray(tex), w * 4
    for y in range(h):
        for x in range(0, wb, 16):
            a = 0x18 + _gob_addr(x, y, wb)
            tex[a:a + 16] = rgba[y * wb + x:y * wb + x + 16]
    return bytes(tex)


def blit_scaled(dst, dw, dx, dy, dsize, src, sw, sx, sy, ssize):
    """Copies a ssize square of src to a dsize square of dst, averaging (alpha-weighted) what each pixel covers."""
    f = ssize / dsize
    for j in range(dsize):
        for i in range(dsize):
            x0, x1, y0, y1 = i * f, (i + 1) * f, j * f, (j + 1) * f
            acc = [0.0] * 4
            area = 0.0
            for yy in range(int(y0), min(ssize, int(-(-y1 // 1)))):
                wy = min(y1, yy + 1) - max(y0, yy)
                for xx in range(int(x0), min(ssize, int(-(-x1 // 1)))):
                    wgt = wy * (min(x1, xx + 1) - max(x0, xx))
                    p = ((sy + yy) * sw + sx + xx) * 4
                    a = src[p + 3] / 255
                    acc[0] += src[p] * a * wgt; acc[1] += src[p + 1] * a * wgt; acc[2] += src[p + 2] * a * wgt
                    acc[3] += a * wgt
                    area += wgt
            q = ((dy + j) * dw + dx + i) * 4
            alpha = acc[3] / area
            dst[q:q + 4] = bytes((round(acc[0] / acc[3]) if acc[3] else 0, round(acc[1] / acc[3]) if acc[3] else 0,
                                  round(acc[2] / acc[3]) if acc[3] else 0, round(alpha * 255)))


def replace_in_arc(arc, name, type_hash, data):
    """The archive with one entry's data replaced; the new data is appended, every other entry stays put."""
    arc = bytearray(arc)
    for off, n, th, _csize, dsize, _data_off in arc_entries(arc):
        if n == name and th == type_hash:
            packed = zlib.compress(data, 9)
            new_off = len(arc)
            arc += packed
            struct.pack_into('<III', arc, off + 0x44, len(packed), (dsize & ~0x1FFFFFFF) | len(data), new_off)
            return bytes(arc)
    raise KeyError(name)
