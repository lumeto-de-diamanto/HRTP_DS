"""Read an ndstool ROM's NitroFS by pathname for packaging regressions."""
import struct


def nitro_files(rom):
    fnt, fnt_size, fat, fat_size = struct.unpack_from('<4I', rom, 0x40)
    if fnt_size < 8 or fat_size % 8 or fnt + fnt_size > len(rom) or fat + fat_size > len(rom):
        raise ValueError('invalid NDS filesystem tables')
    result, visited = {}, set()

    def directory(number, prefix):
        if number in visited:
            raise ValueError('cyclic NitroFS directory')
        visited.add(number)
        entry = (number - 0xf000) * 8
        if entry < 0 or entry + 8 > fnt_size:
            raise ValueError('invalid NitroFS directory ID')
        offset, file_id, _ = struct.unpack_from('<IHH', rom, fnt + entry)
        p = fnt + offset
        while p < fnt + fnt_size:
            size = rom[p]
            p += 1
            if size == 0:
                return
            length = size & 127
            if not length or p + length + (2 if size & 128 else 0) > fnt + fnt_size:
                raise ValueError('invalid NitroFS name')
            name = rom[p:p + length].decode('ascii')
            p += length
            if name in ('.', '..') or '/' in name:
                raise ValueError('invalid NitroFS pathname')
            path = prefix + name
            if size & 128:
                child = struct.unpack_from('<H', rom, p)[0]
                p += 2
                directory(child, path + '/')
            else:
                if (file_id + 1) * 8 > fat_size:
                    raise ValueError('invalid NitroFS file ID')
                start, end = struct.unpack_from('<II', rom, fat + file_id * 8)
                file_id += 1
                if start > end or end > len(rom) or path in result:
                    raise ValueError('invalid/duplicate NitroFS file')
                result[path] = rom[start:end]
        raise ValueError('unterminated NitroFS directory')

    directory(0xf000, '')
    return result
