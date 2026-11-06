#!/usr/bin/env python
"""生成 schemaVersion 2 的曲线关卡（任务书 5.3 / A-10）。

脚本复刻了 C++ 几何内核（向心 Catmull-Rom + 等弧长重采样，core/src/geom.cpp），
因此水果层可以写成"把道路中心线偏移 d 格"的形式。
输出稠密控制点可让该层自身的样条落在这条偏移曲线上；
真正关键的采样仍由 core 负责：
水果间距、投影与接触带。
"""
import json
import math
import os
import sys

STEP = 0.05  # 仅用于推导控制点的稠密采样


def knot(a, b):
    d = math.hypot(b[0] - a[0], b[1] - a[1])
    v = math.sqrt(d)
    return v if v >= 1e-4 else 1e-4


def catmull_rom(p0, p1, p2, p3, u):
    t0 = 0.0
    t1 = t0 + knot(p0, p1)
    t2 = t1 + knot(p1, p2)
    t3 = t2 + knot(p2, p3)
    t = t1 + (t2 - t1) * u
    a1 = (1.0 / (t1 - t0)) * ((t1 - t) * p0[0] + (t - t0) * p1[0]), (1.0 / (t1 - t0)) * ((t1 - t) * p0[1] + (t - t0) * p1[1])
    a2 = (1.0 / (t2 - t1)) * ((t2 - t) * p1[0] + (t - t1) * p2[0]), (1.0 / (t2 - t1)) * ((t2 - t) * p1[1] + (t - t1) * p2[1])
    a3 = (1.0 / (t3 - t2)) * ((t3 - t) * p2[0] + (t - t2) * p3[0]), (1.0 / (t3 - t2)) * ((t3 - t) * p2[1] + (t - t2) * p3[1])
    b1 = (1.0 / (t2 - t0)) * ((t2 - t) * a1[0] + (t - t0) * a2[0]), (1.0 / (t2 - t0)) * ((t2 - t) * a1[1] + (t - t0) * a2[1])
    b2 = (1.0 / (t3 - t1)) * ((t3 - t) * a2[0] + (t - t1) * a3[0]), (1.0 / (t3 - t1)) * ((t3 - t) * a2[1] + (t - t1) * a3[1])
    c = (1.0 / (t2 - t1)) * ((t2 - t) * b1[0] + (t - t1) * b2[0]), (1.0 / (t2 - t1)) * ((t2 - t) * b1[1] + (t - t1) * b2[1])
    return c


def dense(control, closed):
    n = len(control)
    segments = n if closed else n - 1
    out = []
    for i in range(segments):
        p1 = control[i]
        p2 = control[(i + 1) % n]
        p0 = control[(i - 1) % n] if closed else (2 * p1[0] - p2[0], 2 * p1[1] - p2[1])
        p3 = control[(i + 2) % n] if closed else (2 * p2[0] - p1[0], 2 * p2[1] - p1[1])
        chord = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        sub = max(4, int(math.ceil(chord / STEP)))
        for k in range(sub):
            out.append(catmull_rom(p0, p1, p2, p3, k / float(sub)))
    out.append(control[0] if closed else control[-1])
    return out


def arc_table(points):
    arc = [0.0]
    for i in range(1, len(points)):
        arc.append(arc[-1] + math.hypot(points[i][0] - points[i - 1][0], points[i][1] - points[i - 1][1]))
    return arc


def tangent(points, arc, s):
    total = arc[-1]
    s = max(0.0, min(total, s))
    lo, hi = 0, len(arc) - 1
    while lo < hi - 1:
        mid = (lo + hi) // 2
        if arc[mid] <= s:
            lo = mid
        else:
            hi = mid
    seg = max(1e-9, arc[lo + 1] - arc[lo])
    t = (s - arc[lo]) / seg
    dx = (points[lo + 1][0] - points[lo][0]) * (1 - t) + (points[lo + 1][0] - points[lo][0])
    dy = (points[lo + 1][1] - points[lo][1]) * (1 - t) + (points[lo + 1][1] - points[lo][1])
    length = math.hypot(dx, dy)
    return (dx / length, dy / length) if length > 1e-12 else (1.0, 0.0)


def offset_control(control, closed, distance, keep_every=0.5):
    """在驾驶员左侧偏移 `distance` 后所得曲线的控制点。"""
    pts = dense(control, closed)
    arc = arc_table(pts)
    total = arc[-1]
    out = []
    s = 0.0
    guard = 0
    while s < total - 1e-9 and guard < 100000:
        guard += 1
        # 弧长 s 处的位置
        lo, hi = 0, len(arc) - 1
        while lo < hi - 1:
            mid = (lo + hi) // 2
            if arc[mid] <= s:
                lo = mid
            else:
                hi = mid
        seg = max(1e-9, arc[lo + 1] - arc[lo])
        t = (s - arc[lo]) / seg
        px = pts[lo][0] + (pts[lo + 1][0] - pts[lo][0]) * t
        py = pts[lo][1] + (pts[lo + 1][1] - pts[lo][1]) * t
        tx, ty = tangent(pts, arc, s)
        # (x 向右, y 向下) 坐标系下的左法线：(ty, -tx)
        out.append((px + distance * ty, py - distance * tx))
        s += keep_every
    return out


def rounded(value):
    return round(value, 3)


def control_json(points):
    return [[rounded(x), rounded(y)] for x, y in points]


def offset_with_length(control, closed, distance, keep_every=0.5):
    """偏移后的控制点，以及该偏移折线的弧长。"""
    pts = offset_control(control, closed, distance, keep_every)
    length = 0.0
    for i in range(1, len(pts)):
        length += math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1])
    return pts, length


def layer(layer_id, points, closed, spacing, size, color, feeds_from, sample_step=0.25):
    data = {
        "id": layer_id,
        "controlPoints": control_json(points),
        "closed": closed,
        "spacing": spacing,
        "size": size,
        "colors": [color],
        "sampleStep": sample_step,
    }
    if feeds_from:
        data["refill"] = {"mode": "layerIn", "feedsFrom": feeds_from}
    else:
        data["refill"] = {"mode": "none"}
    return data


def write(path, document):
    text = json.dumps(document, ensure_ascii=False, indent=2)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text + "\n")
    print("WROTE %s (%d bytes)" % (path, len(text)))



def make_layer(layer_id, road, distance, count, size, color, feeds_from, keep_every=0.4, sample_step=0.25):
    """相对道路偏移出来的一层水果，槽位数量正好是 `count`。

    间距由偏移曲线的长度反推，因此可以直接控制槽位数量：
    为了让补位链保持单射（E2011），外层槽位数绝不能多于
    它所供给的那些层。
    """
    points, length = offset_with_length(road, False, distance, keep_every)
    spacing = max(0.2, length / float(count))
    return layer(layer_id, points, False, round(spacing, 4), size, color, feeds_from, sample_step)


def arc_points(cx, cy, radius, start_deg, end_deg, step=0.4):
    """圆弧上的点，相邻点间距不超过 `step` 格。

    关卡空间是 x=列、y=行，且 y 向下增长，所以圆的"上半"
    对应 180deg..360deg 的扫掠，驾驶员的左侧就是圆弧的外侧
    （见 core/src/geom.cpp 的 project()）。
    """
    span_deg = end_deg - start_deg
    length = abs(math.radians(span_deg)) * radius
    count = max(2, int(math.ceil(length / step)) + 1)
    points = []
    for i in range(count):
        angle = math.radians(start_deg + span_deg * i / float(count - 1))
        points.append((cx + radius * math.cos(angle), cy + radius * math.sin(angle)))
    return points


def ring_level(root):
    """L_CURVE_RING：一条开放的环形道路，配三层同心水果层。"""
    road = [(3.0, 10.0), (4.5, 4.5), (10.0, 2.0), (16.0, 4.0), (17.5, 10.5),
            (13.0, 15.5), (6.5, 16.0), (3.0, 13.0)]
    document = {
        "schemaVersion": 2,
        "id": "L_CURVE_RING",
        "name": "曲线环形果园",
        "seed": 20261107,
        "grid": {"rows": 20, "cols": 21},
        "mechanism": "curves",
        "palette": ["red", "yellow", "blue"],
        "road": {
            "controlPoints": control_json(road),
            "closed": False,
            "width": 1,
            "collectSide": "left",
            "carLength": 1.0,
            "carGap": 0.5,
            "speed": 2.0,
            "sampleStep": 0.25,
            "contactSlack": 0.5,
            "contactReach": 0.5,
        },
        "fruitLayers": [
            make_layer("ring0", road, 0.85, 22, 1.0, "blue", "ring1"),
            make_layer("ring1", road, 1.70, 16, 1.12, "yellow", "ring2"),
            make_layer("ring2", road, 2.55, 11, 1.24, "red", None),
        ],
        "parking": {
            "slotCount": 5,
            "lanes": [
                {"color": "red", "queue": ["v_r24", "v_r12"]},
                {"color": "yellow", "queue": ["v_y24", "v_y12"]},
                {"color": "blue", "queue": ["v_b24", "v_b12"]},
            ],
        },
        "vehicles": {
            "v_r24": {"color": "red", "capacity": 24},
            "v_r12": {"color": "red", "capacity": 12},
            "v_y24": {"color": "yellow", "capacity": 24},
            "v_y12": {"color": "yellow", "capacity": 12},
            "v_b24": {"color": "blue", "capacity": 24},
            "v_b12": {"color": "blue", "capacity": 12},
        },
        "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
    }
    write(os.path.join(root, "levels", "L_CURVE_RING.json"), document)


def sector_ring_level(root):
    """L_RING_OMEGA 改版：半圆形道路外围的半扇环水果区。

    * 道路是圆的上半部分（180deg..360deg），半径 6 格；
    * 水果区是其外侧的半圆环，划分成五个 36deg 的扇区；
    * 每个扇区有五层径向层，每层放三个水果槽位；
    * 整个水果区是五条同心半圆，每条 15 个槽位，
      因此每个扇区总是恰好拥有每一层中相邻的 3 个槽位；
    * `presentation.ring` 指示表现层在水果下方绘制五块扇形地面
      （每个扇区边界处留 3deg 间隔）。
    """
    cx, cy, radius = 12.0, 12.0, 6.0
    palette = ["red", "yellow", "blue"]
    road = arc_points(cx, cy, radius, 180.0, 360.0, 0.4)
    layer_radii = [radius + 0.95 + 1.10 * j for j in range(5)]
    layer_sizes = [1.0, 1.06, 1.12, 1.18, 1.24]
    layers = []
    for j, layer_radius in enumerate(layer_radii):
        points = arc_points(cx, cy, layer_radius, 180.0, 360.0, 0.4)
        # 半圆上 15 个槽位 = 每 12deg 一个：每个 36deg 扇区三个。
        spacing = math.pi * layer_radius / 15.0
        colors = [palette[(i // 3 + i % 3) % 3] for i in range(15)]
        layer = {
            "id": "ring%d" % j,
            "controlPoints": control_json(points),
            "closed": False,
            "spacing": round(spacing, 4),
            "size": layer_sizes[j],
            "colors": colors,
            "sampleStep": 0.2,
        }
        if j < 4:
            layer["refill"] = {"mode": "layerIn", "feedsFrom": "ring%d" % (j + 1)}
        else:
            layer["refill"] = {"mode": "none"}
        layers.append(layer)

    document = {
        "schemaVersion": 2,
        "id": "L_RING_OMEGA",
        "name": "环形果园",
        "seed": 20261105,
        "grid": {"rows": 14, "cols": 26},
        "mechanism": "curves",
        "palette": palette,
        "road": {
            "controlPoints": control_json(road),
            "closed": False,
            "width": 1,
            "collectSide": "left",
            "carLength": 1.0,
            "carGap": 0.5,
            "speed": 2.0,
            "sampleStep": 0.15,
            "contactSlack": 0.5,
            "contactReach": 0.5,
        },
        "fruitLayers": layers,
        "parking": {
            "slotCount": 5,
            "lanes": [
                {"color": "red", "queue": ["v_red"]},
                {"color": "yellow", "queue": ["v_yellow"]},
                {"color": "blue", "queue": ["v_blue"]},
            ],
        },
        "vehicles": {
            # 每种颜色 25 个水果，因此一直装不满的车会不断从停车位被重新派出，
            # 直到该颜色的水果耗尽。
            "v_red": {"color": "red", "capacity": 25},
            "v_yellow": {"color": "yellow", "capacity": 25},
            "v_blue": {"color": "blue", "capacity": 25},
        },
        "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
        "presentation": {
            "island": [],
            "ring": {
                "centre": [cx, cy],
                "sectors": 5,
                "startDeg": 180.0,
                "sweepDeg": 180.0,
                "innerRadius": 6.55,
                "outerRadius": 12.15,
                "gapDeg": 3.0,
            },
        },
    }
    write(os.path.join(root, "levels", "L_RING_OMEGA.json"), document)


def s_level(root):
    """L_CURVE_S：一条 S 形道路，水果层顺着两处弯道铺开。"""
    road = [(2.0, 5.0), (7.0, 8.0), (12.0, 5.0), (17.0, 8.0), (21.0, 5.5)]
    document = {
        "schemaVersion": 2,
        "id": "L_CURVE_S",
        "name": "S 形弯道",
        "seed": 20261108,
        "grid": {"rows": 14, "cols": 23},
        "mechanism": "curves",
        "palette": ["yellow", "red", "green"],
        "road": {
            "controlPoints": control_json(road),
            "closed": False,
            "width": 1,
            "collectSide": "left",
            "carLength": 1.0,
            "carGap": 0.5,
            "speed": 2.0,
            "sampleStep": 0.25,
        },
        "fruitLayers": [
            make_layer("bend0", road, 0.85, 14, 1.0, "green", "bend1"),
            make_layer("bend1", road, 1.65, 10, 1.15, "yellow", "bend2"),
            make_layer("bend2", road, 2.45, 7, 1.3, "red", None),
        ],
        "parking": {
            "slotCount": 5,
            "lanes": [
                {"color": "yellow", "queue": ["v_y24", "v_y12"]},
                {"color": "red", "queue": ["v_r24", "v_r12"]},
                {"color": "green", "queue": ["v_g24", "v_g12"]},
            ],
        },
        "vehicles": {
            "v_y24": {"color": "yellow", "capacity": 24},
            "v_y12": {"color": "yellow", "capacity": 12},
            "v_r24": {"color": "red", "capacity": 24},
            "v_r12": {"color": "red", "capacity": 12},
            "v_g24": {"color": "green", "capacity": 24},
            "v_g12": {"color": "green", "capacity": 12},
        },
        "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
    }
    write(os.path.join(root, "levels", "L_CURVE_S.json"), document)


def poly_level(root):
    """L_CURVE_POLY：多边形根层、开放外层，外加一座岛。

    道路是一段开放的六边形弧，所以中心是没有任何层占据的"空洞"；
    最外层是一条开放曲线（任务书 A-05：
    多边形层、空洞层与开放曲线层）。
    """
    road = [(11.0, 3.0), (17.5, 6.5), (18.0, 13.0), (12.0, 17.0), (5.0, 14.5), (3.5, 8.5)]
    document = {
        "schemaVersion": 2,
        "id": "L_CURVE_POLY",
        "name": "多边形与开放层",
        "seed": 20261109,
        "grid": {"rows": 20, "cols": 21},
        "mechanism": "curves",
        "palette": ["blue", "purple", "yellow"],
        "road": {
            "controlPoints": control_json(road),
            "closed": False,
            "width": 1,
            "collectSide": "left",
            "carLength": 1.0,
            "carGap": 0.5,
            "speed": 2.0,
            "sampleStep": 0.25,
        },
        "fruitLayers": [
            make_layer("poly0", road, 0.85, 16, 1.0, "blue", "poly1"),
            make_layer("poly1", road, 1.75, 12, 1.15, "purple", "open0"),
            make_layer("open0", road, 2.6, 9, 1.3, "yellow", None, keep_every=0.3),
        ],
        "parking": {
            "slotCount": 5,
            "lanes": [
                {"color": "blue", "queue": ["v_b24", "v_b12"]},
                {"color": "purple", "queue": ["v_p24", "v_p12"]},
                {"color": "yellow", "queue": ["v_y24", "v_y12"]},
            ],
        },
        "vehicles": {
            "v_b24": {"color": "blue", "capacity": 24},
            "v_b12": {"color": "blue", "capacity": 12},
            "v_p24": {"color": "purple", "capacity": 24},
            "v_p12": {"color": "purple", "capacity": 12},
            "v_y24": {"color": "yellow", "capacity": 24},
            "v_y12": {"color": "yellow", "capacity": 12},
        },
        "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
        "presentation": {"island": []},
    }
    write(os.path.join(root, "levels", "L_CURVE_POLY.json"), document)


def items_level(root):
    """道具关样本：带保留字段 `items[]` 的曲线关卡。"""
    road = [(2.0, 9.0), (6.0, 4.0), (11.0, 3.5), (14.0, 8.0), (10.0, 12.0), (4.0, 12.5)]
    document = {
        "schemaVersion": 2,
        "id": "L_curve_items",
        "name": "曲线道具关卡",
        "seed": 20261110,
        "grid": {"rows": 14, "cols": 16},
        "mechanism": "curves",
        "palette": ["red", "blue"],
        "road": {
            "controlPoints": control_json(road),
            "closed": False,
            "width": 1,
            "collectSide": "left",
            "carLength": 1.0,
            "carGap": 0.5,
            "speed": 2.0,
            "sampleStep": 0.25,
        },
        "fruitLayers": [
            make_layer("inner", road, 0.85, 12, 1.0, "blue", "outer"),
            make_layer("outer", road, 1.70, 8, 1.2, "red", None),
        ],
        "parking": {
            "slotCount": 4,
            "lanes": [
                {"color": "red", "queue": ["vr1", "vr2"]},
                {"color": "blue", "queue": ["vb1", "vb2"]},
            ],
        },
        "vehicles": {
            "vr1": {"color": "red", "capacity": 24},
            "vr2": {"color": "red", "capacity": 12},
            "vb1": {"color": "blue", "capacity": 24},
            "vb2": {"color": "blue", "capacity": 12},
        },
        "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
        "items": [
            {"id": "paint", "kind": "recolor_car", "charges": 1,
             "params": {"palette": ["red", "blue"]}},
            {"id": "sweep", "kind": "collect_area", "charges": 2,
             "params": {"radius": 2.0}},
            {"id": "empty", "kind": "collect_area", "charges": 0},
        ],
    }
    write(os.path.join(root, "tests", "levels", "L_curve_items.json"), document)


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    ring_level(root)
    sector_ring_level(root)
    s_level(root)
    poly_level(root)
    items_level(root)
    return 0


if __name__ == "__main__":
    sys.exit(main())
