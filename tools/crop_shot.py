"""把编辑器截图裁剪到正在运行的游戏视图。

游戏背景色 (18, 22, 30) 是 FruitsCar 画布独有的，因此它的包围盒
正好就是包含 HUD 在内的实际游戏渲染区域。
"""
import sys

from PIL import Image

BACKGROUND = (18, 22, 30)


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: crop_shot.py <in.png> <out.png>")
        return 2
    image = Image.open(sys.argv[1]).convert("RGB")
    width, height = image.size
    pixels = image.load()
    min_x, min_y = width, height
    max_x, max_y = -1, -1
    for y in range(0, height, 2):
        for x in range(0, width, 2):
            if pixels[x, y] == BACKGROUND:
                if x < min_x:
                    min_x = x
                if y < min_y:
                    min_y = y
                if x > max_x:
                    max_x = x
                if y > max_y:
                    max_y = y
    if max_x < 0:
        print("no game view found")
        return 1
    margin = 4
    left = max(0, min_x - margin)
    top = max(0, min_y - margin)
    right = min(width, max_x + margin)
    bottom = min(height, max_y + margin)
    cropped = image.crop((left, top, right, bottom))
    cropped.save(sys.argv[2])
    print("cropped %dx%d -> %dx%d" % (width, height, cropped.size[0], cropped.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
