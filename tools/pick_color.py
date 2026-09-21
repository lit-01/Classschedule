"""从一张图里取主色（不装 Pillow 也能跑，纯 zlib 解 PNG）。

用途：pony 发个色块截图说「我要这个绿」，跑一下就拿到准确的十六进制值。

用法：
    python tools/pick_color.py 图片路径 [输出条数]
"""

import collections
import struct
import sys
import zlib


def decode_png(path):
    """解 8 位非隔行 PNG，返回 (宽, 高, 通道数, 像素字节串)"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("不是 PNG 文件")

    pos, idat = 8, b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if ctype == b"IHDR":
            w, h, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or interlace != 0:
                raise ValueError("只支持 8 位非隔行 PNG")
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break

    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
    stride = w * channels
    raw = zlib.decompress(idat)

    out, prev, i = bytearray(), bytearray(stride), 0
    for _ in range(h):
        filt = raw[i]
        i += 1
        line = bytearray(raw[i:i + stride])
        i += stride

        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 255

        out += line
        prev = line

    return w, h, channels, out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    w, h, channels, pixels = decode_png(path)
    stride = w * channels

    counter = collections.Counter()
    # 隔几个像素采一次就够了，大图也不至于慢
    step_x = max(1, w // 120)
    step_y = max(1, h // 120)
    for y in range(0, h, step_y):
        for x in range(0, w, step_x):
            off = y * stride + x * channels
            counter[tuple(pixels[off:off + 3])] += 1

    print(f"{path}   {w}x{h}")
    for (r, g, b), n in counter.most_common(top):
        print(f"  #{r:02X}{g:02X}{b:02X}   ({r}, {g}, {b})   x{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
