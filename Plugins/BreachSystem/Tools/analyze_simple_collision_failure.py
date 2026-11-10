"""Analyze an Editor Simple collision failure snapshot without changing geometry."""
import argparse
import json
import math
from collections import Counter
from fractions import Fraction
from pathlib import Path


def parse_snapshot(path):
    result = {"stages": [], "metadata": {}, "triangulation": [], "missing_edges": []}
    stage = region = loop = None
    expected_count = 0

    def finish_loop():
        if loop is not None and len(loop) != expected_count:
            raise ValueError(f"Incomplete loop: expected {expected_count} vertices, got {len(loop)}")

    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "STAGE":
            finish_loop()
            stage = {"name": fields[1], "regions": []}
            result["stages"].append(stage)
            region = loop = None
        elif fields[0] == "REGION":
            finish_loop()
            region = {"outer": [], "holes": [], "declared_area": float(fields[3])}
            stage["regions"].append(region)
            loop = None
        elif fields[0] == "LOOP":
            finish_loop()
            expected_count = int(fields[3])
            loop = []
            if int(fields[1]) == -1:
                region["outer"] = loop
            else:
                region["holes"].append(loop)
        elif fields[0] == "TRIANGULATION":
            result["triangulation"].append(line)
        elif fields[0] == "MISSING":
            result["missing_edges"].append(line)
        elif stage is None:
            result["metadata"][fields[0]] = " ".join(fields[1:])
        elif loop is not None and len(fields) == 2:
            loop.append(tuple(map(float, fields)))
    finish_loop()
    return result


def orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def on_segment(a, b, p):
    return orient(a, b, p) == 0 and all(min(a[i], b[i]) <= p[i] <= max(a[i], b[i]) for i in (0, 1))


def point_in_loop(point, loop):
    winding = 0
    for a, b in zip(loop, loop[1:] + loop[:1]):
        cross = orient(a, b, point)
        if on_segment(a, b, point):
            return "boundary"
        if a[1] <= point[1] < b[1] and cross > 0:
            winding += 1
        elif b[1] <= point[1] < a[1] and cross < 0:
            winding -= 1
    return "inside" if winding else "outside"


def inspect_stage(regions):
    issues, edges, lengths = [], [], []
    vertex_count = 0
    for region_index, region in enumerate(regions):
        loops = [[tuple(map(Fraction, p)) for p in loop] for loop in [region["outer"], *region["holes"]]]
        areas = []
        for loop_index, loop in enumerate(loops, -1):
            vertex_count += len(loop)
            area = sum(a[0] * b[1] - a[1] * b[0] for a, b in zip(loop, loop[1:] + loop[:1]))
            areas.append(area)
            if area == 0:
                issues.append({"kind": "zero_area_loop", "region": region_index, "loop": loop_index})
            seen = {}
            for index, point in enumerate(loop):
                if point in seen:
                    issues.append({"kind": "duplicate_vertex", "region": region_index, "loop": loop_index, "vertices": [seen[point], index]})
                seen[point] = index
                other = loop[(index + 1) % len(loop)]
                location = {"region": region_index, "loop": loop_index, "start": index, "end": (index + 1) % len(loop)}
                lengths.append(math.hypot(float(other[0] - point[0]), float(other[1] - point[1])))
                if point == other:
                    issues.append({"kind": "zero_length_edge", "edge": location})
                    continue
                edges.append({"a": point, "b": other, "location": location})
        for hole_index, hole in enumerate(loops[1:]):
            if any(point_in_loop(p, loops[0]) == "outside" for p in hole):
                issues.append({"kind": "hole_outside_outer", "region": region_index, "hole": hole_index})
            if areas[0] * areas[hole_index + 1] > 0:
                issues.append({"kind": "hole_winding_matches_outer", "region": region_index, "hole": hole_index})
            for previous in range(hole_index):
                other = loops[previous + 1]
                if hole and other and (point_in_loop(hole[0], other) == "inside" or point_in_loop(other[0], hole) == "inside"):
                    issues.append({"kind": "nested_holes", "region": region_index, "holes": [previous, hole_index]})

    for index, first in enumerate(edges):
        a, b = first["a"], first["b"]
        for second in edges[index + 1:]:
            c, d = second["a"], second["b"]
            if any(max(a[axis], b[axis]) < min(c[axis], d[axis]) or max(c[axis], d[axis]) < min(a[axis], b[axis]) for axis in (0, 1)):
                continue
            ab_c, ab_d, cd_a, cd_b = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
            kind = None
            if ab_c * ab_d < 0 and cd_a * cd_b < 0:
                kind = "proper_crossing"
            elif ab_c == ab_d == cd_a == cd_b == 0:
                axis = 0 if a[0] != b[0] else 1
                if max(min(a[axis], b[axis]), min(c[axis], d[axis])) < min(max(a[axis], b[axis]), max(c[axis], d[axis])):
                    kind = "collinear_overlap"
            if kind is None:
                contacts = {p for p in (c, d) if on_segment(a, b, p)} | {p for p in (a, b) if on_segment(c, d, p)}
                left, right = first["location"], second["location"]
                adjacent = left["region"] == right["region"] and left["loop"] == right["loop"] and (left["end"] == right["start"] or right["end"] == left["start"])
                if contacts and not adjacent:
                    kind = "vertex_on_edge" if any(p not in (a, b) or p not in (c, d) for p in contacts) else "boundary_contact"
            if kind:
                issues.append({"kind": kind, "edge_a": first["location"], "edge_b": second["location"], "points": [[float(x), float(y)] for x, y in (a, b, c, d)]})
    return {"regions": len(regions), "vertices": vertex_count, "minimum_edge_length": min(lengths, default=None), "issue_counts": dict(Counter(item["kind"] for item in issues)), "issues": issues}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("snapshot", nargs="?", type=Path, help="snapshot.txt path; defaults to the newest Editor failure capture")
    args = parser.parse_args()
    path = args.snapshot
    if path is None:
        directory = Path(__file__).resolve().parents[3] / "Saved/Diagnostics/SimpleCollisionFailures"
        candidates = list(directory.glob("*/snapshot.txt"))
        if not candidates:
            parser.error(f"No failure snapshot found under {directory}")
        path = max(candidates, key=lambda candidate: candidate.stat().st_mtime_ns)
    capture = parse_snapshot(path)
    report = {"snapshot": str(path.resolve()), "metadata": capture["metadata"], "triangulation": capture["triangulation"], "missing_edges": capture["missing_edges"], "stages": []}
    for stage in capture["stages"]:
        checked = {"name": stage["name"], **inspect_stage(stage["regions"])}
        report["stages"].append(checked)
        print(f'{stage["name"]}: regions={checked["regions"]}, vertices={checked["vertices"]}, boundary_findings={checked["issue_counts"]}')
    output = path.with_name("analysis.json")
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Missing-edge records: {len(capture['missing_edges'])}; analysis: {output.resolve()}")
    print("Boundary findings are diagnostic evidence; none detected does not prove complete polygon validity.")


if __name__ == "__main__":
    main()
