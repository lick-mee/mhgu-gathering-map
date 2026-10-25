"""Reading the game files the point table is built from.

MT archives (ARC), the maps' gathering-point sets (IPS) and their item lot tables (IPL), and the item table.
"""
import os
import struct
import zlib
from collections import Counter

NATIVE = os.path.join('romfs', 'nativeNX')
MAP_COM = os.path.join(NATIVE, 'loc', 'arc', 'stage', 'map_com')
TYPE_IPS, TYPE_IPL = 0x52776AB1, 0x25FD693F


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
