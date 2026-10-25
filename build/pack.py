"""Pack the release zip: a zzz-gather-map folder to drop into the recomp's mods folder, with its setup."""
import os
import sys
import zipfile

MOD = 'zzz-gather-map'


def main(nro, dst):
    here = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
        z.write(nro, MOD + '/romfs/nativeNX/plugins/gather_map.nro')
        z.write(os.path.join(here, '..', 'mod', 'README.txt'), MOD + '/README.txt')
        z.write(os.path.join(here, '..', 'mod', 'setup.bat'), MOD + '/setup.bat')
        for name in ('make_data.py', 'gamedata.py'):
            z.write(os.path.join(here, '..', 'tools', name), MOD + '/setup/' + name)
    print('wrote %s' % dst)


if __name__ == '__main__':
    main(*sys.argv[1:3])
