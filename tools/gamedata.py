"""Reading the game files the plugin's tables are built from: MT archives (ARC) and the item table."""
import os
import struct
import zlib

NATIVE = os.path.join('romfs', 'nativeNX')


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


def source(root, rel):
    """The file the game loads for rel: the last mod in folder order that has it, else the game's own."""
    mods = os.path.join(root, 'mods')
    for name in sorted(os.listdir(mods), key=str.lower, reverse=True):
        path = os.path.join(mods, name, rel)
        if os.path.isfile(path):
            return path
    return os.path.join(root, 'data', rel)


def item_table(root):
    """(icon, color) per item: its cell of the common icon texture and its color index."""
    data = arc_file(source(root, os.path.join(NATIVE, 'loc', 'arc', 'resident.arc')), 'table\\itemData', 0x7C4883A8)
    count = struct.unpack_from('<I', data, 4)[0]
    # Each 44-byte entry has the item's icon at +16 and its color at +17.
    return [(data[8 + 44 * i + 16], data[8 + 44 * i + 17]) for i in range(count)]
