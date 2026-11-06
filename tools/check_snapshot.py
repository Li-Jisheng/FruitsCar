"""按 A-12 的采样上限校验导出的快照。

用法：check_snapshot.py <final_state.json>
输出 "SNAPSHOT level=... samples=N maxGap=0.xxxx fruits=M world=curves ok=true"
"""
import json
import math
import sys

MAX_GAP = 0.2505  # 0.25 格，外加 4 位小数序列化的舍入余量


def main():
    if len(sys.argv) < 2:
        print("usage: check_snapshot.py <snapshot.json>")
        return 2
    with open(sys.argv[1], "r", encoding="utf-8") as handle:
        state = json.load(handle)
    road = state.get("road", {})
    polyline = road.get("polyline") or []
    max_gap = 0.0
    for i in range(1, len(polyline)):
        dx = polyline[i][0] - polyline[i - 1][0]
        dy = polyline[i][1] - polyline[i - 1][1]
        max_gap = max(max_gap, math.hypot(dx, dy))
    fruits = state.get("fruits") or []
    items = state.get("items") or []
    effects = state.get("effects")
    ok = (len(polyline) > 2 and max_gap <= MAX_GAP and len(fruits) >= 0 and
          state.get("world") == "curves" and effects == [])
    print("SNAPSHOT level=%s samples=%d maxGap=%.4f fruits=%d items=%d world=%s status=%s ok=%s" % (
        state.get("levelId"), len(polyline), max_gap, len(fruits), len(items),
        state.get("world"), state.get("status"), "true" if ok else "false"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
