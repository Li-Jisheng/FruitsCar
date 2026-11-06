#!/usr/bin/env python
"""生成 M22（附录 C）使用的非法曲线关卡样本。

每个样本都是一份最小的合法曲线关卡，且只做恰好一处改动，
这样测试就能断言"这个文件 -> 这个错误码"，而不存在其他差异。
"""
import copy
import json
import os
import sys

BASE = {
    "schemaVersion": 2,
    "id": "L_curve_invalid",
    "name": "invalid curve",
    "seed": 7,
    "grid": {"rows": 8, "cols": 12},
    "mechanism": "curves",
    "palette": ["red"],
    "road": {
        "controlPoints": [[1.0, 6.0], [6.0, 6.0], [11.0, 6.0]],
        "closed": False,
        "width": 1,
        "collectSide": "left",
        "carLength": 1.0,
        "carGap": 0.5,
        "speed": 2.0,
        "sampleStep": 0.25,
    },
    "fruitLayers": [
        {
            "id": "root",
            "controlPoints": [[1.0, 5.0], [6.0, 5.0], [11.0, 5.0]],
            "closed": False,
            "spacing": 1.0,
            "size": 1.0,
            "colors": ["red"],
            "refill": {"mode": "none"},
        }
    ],
    "parking": {"slotCount": 3, "lanes": [{"color": "red", "queue": ["v1", "v2"]}]},
    "vehicles": {"v1": {"color": "red", "capacity": 32}, "v2": {"color": "red", "capacity": 32}},
    "timing": {"spawnIntervalMs": 500, "settleTicks": 30},
}


def fixture(name, mutate):
    document = copy.deepcopy(BASE)
    document["id"] = "L_curve_" + name.lower()
    mutate(document)
    return name, document


def set_road_points(points):
    return lambda d: d["road"].__setitem__("controlPoints", points)


def two_layer_chain():
    def mutate(d):
        d["fruitLayers"] = [
            {
                "id": "a",
                "controlPoints": [[1.0, 5.0], [6.0, 5.0], [11.0, 5.0]],
                "closed": False,
                "spacing": 2.0,
                "size": 1.0,
                "colors": ["red"],
                "refill": {"mode": "layerIn", "feedsFrom": "b"},
            },
            {
                "id": "b",
                "controlPoints": [[1.0, 4.0], [6.0, 4.0], [11.0, 4.0]],
                "closed": False,
                "spacing": 1.0,
                "size": 1.1,
                "colors": ["red"],
                "refill": {"mode": "layerIn", "feedsFrom": "a"},
            },
        ]
    return mutate


FIXTURES = [
    fixture("E2001_onepoint", set_road_points([[1.0, 6.0]])),
    fixture("E2002_step", lambda d: d["road"].__setitem__("sampleStep", 0.0)),
    fixture("E2003_spacing", lambda d: d["fruitLayers"][0].__setitem__("spacing", 0.0)),
    fixture("E2004_feedsfrom", lambda d: d["fruitLayers"][0].__setitem__(
        "refill", {"mode": "layerIn", "feedsFrom": "missing"})),
    fixture("E2005_cycle", two_layer_chain()),
    fixture("E2011_toomany", lambda d: d["fruitLayers"].__setitem__(0, {
        "id": "root",
        "controlPoints": [[1.0, 5.0], [6.0, 5.0], [11.0, 5.0]],
        "closed": False,
        "spacing": 2.0,
        "size": 1.0,
        "colors": ["red"],
        "refill": {"mode": "layerIn", "feedsFrom": "outer"},
    })),
    fixture("E2013_selfcross", set_road_points(
        [[1.0, 1.0], [10.0, 10.0], [10.0, 1.0], [1.0, 10.0]])),
    fixture("E2020_outofband", lambda d: d["fruitLayers"][0].__setitem__(
        "controlPoints", [[1.0, 3.0], [6.0, 3.0], [11.0, 3.0]])),
    fixture("E2021_oversize", set_road_points([[0.0, 0.0], [3000.0, 0.0]])),
    fixture("E2030_kind", lambda d: d.__setitem__(
        "items", [{"id": "x", "kind": "teleport", "charges": 1}])),
    fixture("E2031_charges", lambda d: d.__setitem__(
        "items", [{"id": "x", "kind": "recolor_car", "charges": -2}])),
    fixture("E1013_capacity", lambda d: d.__setitem__(
        "vehicles", {"v1": {"color": "red", "capacity": 2}, "v2": {"color": "red", "capacity": 2}})),
]

# E2011 样本需要一个槽位比内层更多的外层。
for name, document in FIXTURES:
    if name == "E2011_toomany":
        document["fruitLayers"].append({
            "id": "outer",
            "controlPoints": [[1.0, 4.0], [6.0, 4.0], [11.0, 4.0]],
            "closed": False,
            "spacing": 1.0,
            "size": 1.1,
            "colors": ["red"],
            "refill": {"mode": "none"},
        })
        # 容量放大到 2 倍，避免 E1013 掩盖链式补位错误
        document["vehicles"] = {"v1": {"color": "red", "capacity": 64},
                                "v2": {"color": "red", "capacity": 64}}


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    target = os.path.join(root, "tests", "levels", "invalid")
    for name, document in FIXTURES:
        path = os.path.join(target, name + ".json")
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(document, ensure_ascii=False, indent=2) + "\n")
        print("WROTE %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
