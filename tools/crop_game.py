"""在 Cocos Creator 截图里定位游戏视图并裁剪出来。

游戏视图是唯一不是中性灰色的区域：编辑器面板是灰色（#1c1c1c..#262626），
而预览区要么是纯黑的留边黑条，要么是游戏的深蓝色背景。
检测器会在预览面板上扫过几条水平与垂直扫描线，
保留其中最长的连续一段"非灰色"像素，
那一整段正好就是游戏视口。

用法：crop_game.py <in.png> <out.png> [--verbose]
"""
import sys

from PIL import Image

ASPECT = 720.0 / 1280.0


def is_grey(pixel):
    r, g, b = pixel
    return abs(r - g) <= 4 and abs(g - b) <= 4 and 24 <= r <= 70


def longest_run(flags, offset):
    """最长的一段连续 True，以绝对下标返回。"""
    best_start, best_end = -1, -1
    start = -1
    last = -1
    for index, flag in enumerate(flags):
        if flag:
            if start < 0:
                start = index
            last = index
        else:
            if start >= 0 and last - start > best_end - best_start:
                best_start, best_end = start + offset, last + offset
            start = -1
    if start >= 0 and last - start > best_end - best_start:
        best_start, best_end = start + offset, last + offset
    return best_start, best_end


def median(values):
    ordered = sorted(values)
    return ordered[len(ordered) // 2]


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: crop_game.py <in.png> <out.png> [--verbose]")
        return 2
    verbose = "--verbose" in sys.argv
    image = Image.open(sys.argv[1]).convert("RGB")
    width, height = image.size
    pixels = image.load()

    x_lo, x_hi = int(width * 0.25), int(width * 0.75)
    y_lo, y_hi = int(height * 0.12), int(height * 0.94)

    runs = []
    for fraction in (0.3, 0.4, 0.5, 0.6, 0.7):
        y = int(y_lo + (y_hi - y_lo) * fraction)
        flags = [not is_grey(pixels[x, y]) for x in range(x_lo, x_hi)]
        run = longest_run(flags, x_lo)
        if run[0] >= 0 and run[1] - run[0] > 60:
            runs.append(run)
    if not runs:
        print("no game view found (horizontal)")
        return 1
    left = median([run[0] for run in runs])
    right = median([run[1] for run in runs])

    vruns = []
    for fraction in (0.25, 0.4, 0.5, 0.6, 0.75):
        x = int(left + (right - left) * fraction)
        flags = [not is_grey(pixels[x, y]) for y in range(y_lo, y_hi)]
        run = longest_run(flags, y_lo)
        if run[0] >= 0 and run[1] - run[0] > 100:
            vruns.append(run)
    if not vruns:
        print("no game view found (vertical)")
        return 1
    top = median([run[0] for run in vruns])
    bottom = median([run[1] for run in vruns])

    # 以检测到的中心为基准，归一到 720x1280 的画布宽高比。
    centre_x = (left + right) / 2.0
    centre_y = (top + bottom) / 2.0
    half_h = max((bottom - top) / 2.0, ((right - left) / 2.0) / ASPECT)
    half_w = half_h * ASPECT
    left = int(round(centre_x - half_w))
    right = int(round(centre_x + half_w))
    top = int(round(centre_y - half_h))
    bottom = int(round(centre_y + half_h))
    left = max(0, left)
    top = max(0, top)
    right = min(width - 1, right)
    bottom = min(height - 1, bottom)
    cropped = image.crop((left, top, right + 1, bottom + 1))
    cropped.save(sys.argv[2])
    if verbose:
        print("crop=(%d,%d)-(%d,%d) size=%dx%d" % (left, top, right, bottom,
                                                   cropped.size[0], cropped.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
