"""从整张编辑器截图推导出 Game View 的画布矩形。

主界面用精确颜色 #1C2332 = (28,35,50) 绘制关卡卡片，第一张卡片位于设计坐标
(44, 240)、尺寸 632x296（assets/scripts/home/LevelCard.ts）。
由该颜色的包围盒可以反推出设计坐标到屏幕坐标的缩放比例与画布原点，
再用 720x1280 的画布校验结果。
因此编辑器布局不对时会打印 PANEL_INVALID，
而不是裁出一块误导性的区域。
"""
import sys

from PIL import Image

CARD_COLOR = (28, 35, 50)
CARD_TOLERANCE = 3
CARD_X = 44
CARD_Y = 240
CARD_W = 632
CANVAS_W = 720
CANVAS_H = 1280


def bbox_of_first_card(image, target, tolerance, region):
    """最上方那张连续卡片（目录里的第一张卡）的包围盒。

    所有卡片共用同一种填充色，直接取包围盒会横跨整个列表；
    第一段连续行就是第一张卡片，能给出精确的原点。
    """
    pixels = image.load()
    left, top, right, bottom = region
    rows = []
    for y in range(top, bottom):
        first_x = -1
        last_x = -1
        for x in range(left, right):
            r, g, b = pixels[x, y]
            if abs(r - target[0]) <= tolerance and abs(g - target[1]) <= tolerance and \
                    abs(b - target[2]) <= tolerance:
                if first_x < 0:
                    first_x = x
                last_x = x
        rows.append((y, first_x, last_x))
    # 第一段包含该颜色的连续行。
    start = -1
    end = -1
    for index, (y, first_x, last_x) in enumerate(rows):
        if first_x >= 0:
            if start < 0:
                start = index
            end = index
        elif start >= 0:
            break
    if start < 0:
        return None
    band = rows[start:end + 1]
    min_x = min(row[1] for row in band)
    max_x = max(row[2] for row in band)
    return (min_x, band[0][0], max_x - min_x + 1, band[-1][0] - band[0][0] + 1)


def main():
    if len(sys.argv) < 2:
        print("usage: find_panel.py <editor_screenshot.png> [out.json]")
        return 2
    image = Image.open(sys.argv[1]).convert("RGB")
    width, height = image.size
    # 预览面板位于编辑器窗口的中右部。
    region = (int(width * 0.30), int(height * 0.06), int(width * 0.80), int(height * 0.97))
    box = bbox_of_first_card(image, CARD_COLOR, CARD_TOLERANCE, region)
    if box is None:
        print("PANEL_NOT_FOUND")
        return 1
    card_x, card_y, card_w, card_h = box
    scale = card_w / float(CARD_W)
    canvas_x = int(round(card_x - CARD_X * scale))
    canvas_y = int(round(card_y - CARD_Y * scale))
    canvas_w = int(round(CANVAS_W * scale))
    canvas_h = int(round(CANVAS_H * scale))
    aspect_ok = abs(canvas_w / float(canvas_h) - CANVAS_W / float(CANVAS_H)) <= 0.02
    height_ok = abs(card_h / float(296) - scale) <= 0.03
    inside = canvas_x >= 0 and canvas_y >= 0 and canvas_x + canvas_w <= width and \
        canvas_y + canvas_h <= height
    ok = aspect_ok and height_ok and inside
    payload = {
        "ok": ok,
        "x": canvas_x, "y": canvas_y, "w": canvas_w, "h": canvas_h,
        "scale": round(scale, 4),
        "cardBox": {"x": card_x, "y": card_y, "w": card_w, "h": card_h},
        "checks": {"aspectOk": aspect_ok, "heightOk": height_ok, "inside": inside},
    }
    text = ("CROP x=%d y=%d w=%d h=%d scale=%.4f ok=%s"
            % (canvas_x, canvas_y, canvas_w, canvas_h, scale, "true" if ok else "false"))
    print(text)
    if len(sys.argv) >= 3:
        import json
        with open(sys.argv[2], "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
