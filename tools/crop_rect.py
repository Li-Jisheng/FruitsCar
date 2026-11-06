"""把编辑器截图裁剪到指定的矩形区域。

用法：crop_rect.py <in.png> <out.png> <x> <y> <width> <height>
"""
import sys

from PIL import Image


def main() -> int:
    if len(sys.argv) != 7:
        print("usage: crop_rect.py <in.png> <out.png> <x> <y> <w> <h>")
        return 2
    source, target = sys.argv[1], sys.argv[2]
    x, y, w, h = (int(value) for value in sys.argv[3:7])
    image = Image.open(source).convert("RGB")
    width, height = image.size
    if x < 0 or y < 0 or x + w > width or y + h > height:
        print("crop outside the screenshot (%dx%d)" % (width, height))
        return 1
    image.crop((x, y, x + w, y + h)).save(target)
    return 0


if __name__ == "__main__":
    sys.exit(main())
