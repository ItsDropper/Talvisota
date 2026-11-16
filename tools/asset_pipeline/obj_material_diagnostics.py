#!/usr/bin/env python3
"""Read-only OBJ/MTL UV-density and normal-map Nyquist diagnostics."""
from __future__ import annotations

import argparse
import json
import math
import shlex
import struct
import sys
from collections import defaultdict
from pathlib import Path

NORMAL_MAP_KEYS = {"map_bump", "bump", "map_kn", "norm"}


class DiagnosticError(Exception):
    """Malformed or unsafe OBJ/MTL input."""


def resolve_index(raw: str, count: int, context: str) -> int:
    try:
        index = int(raw)
    except ValueError as exc:
        raise DiagnosticError(f"{context}: invalid OBJ index {raw!r}") from exc
    if index == 0:
        raise DiagnosticError(f"{context}: OBJ index zero is invalid")
    resolved = index - 1 if index > 0 else count + index
    if not 0 <= resolved < count:
        raise DiagnosticError(f"{context}: index {index} outside {count}-entry array")
    return resolved


def parse_map_path(tokens: list[str], context: str) -> str:
    """Remove standard MTL map options while preserving quoted filenames."""
    arity = {"-blendu": 1, "-blendv": 1, "-boost": 1, "-texres": 1,
             "-clamp": 1, "-bm": 1, "-imfchan": 1, "-type": 1,
             "-cc": 1, "-colorspace": 1, "-mm": 2}
    i = 0
    while i < len(tokens) and tokens[i].startswith("-"):
        option = tokens[i].lower()
        if option in {"-o", "-s", "-t"}:
            i += 1
            consumed = 0
            while i < len(tokens) and consumed < 3:
                try:
                    float(tokens[i])
                except ValueError:
                    break
                i += 1
                consumed += 1
            if not consumed:
                raise DiagnosticError(f"{context}: {option} requires numeric values")
        elif option in arity:
            count = arity[option]
            if i + count >= len(tokens):
                raise DiagnosticError(f"{context}: {option} is missing a value")
            i += count + 1
        else:
            raise DiagnosticError(f"{context}: unsupported MTL map option {tokens[i]!r}")
    if i == len(tokens):
        raise DiagnosticError(f"{context}: normal-map filename is missing")
    return " ".join(tokens[i:])


def parse_mtl(path: Path) -> dict[str, dict[str, str]]:
    result: dict[str, dict[str, str]] = {}
    current = None
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise DiagnosticError(f"cannot read MTL {path}: {exc}") from exc
    for number, line in enumerate(lines, 1):
        try:
            fields = shlex.split(line, comments=True)
        except ValueError as exc:
            raise DiagnosticError(f"{path}:{number}: malformed quoting: {exc}") from exc
        if not fields:
            continue
        key = fields[0].lower()
        context = f"{path}:{number}"
        if key == "newmtl":
            if len(fields) < 2:
                raise DiagnosticError(f"{context}: newmtl requires a name")
            current = " ".join(fields[1:])
            if current in result:
                raise DiagnosticError(f"{context}: duplicate material {current!r}")
            result[current] = {}
        elif key in NORMAL_MAP_KEYS and current is not None:
            result[current]["normal_map"] = parse_map_path(fields[1:], context)
    return result


def image_dimensions(path: Path) -> tuple[int, int] | None:
    """Read BMP/PNG dimensions without decoding pixels."""
    try:
        with path.open("rb") as stream:
            data = stream.read(32)
    except OSError:
        return None
    if len(data) >= 26 and data[:2] == b"BM":
        dib = struct.unpack_from("<I", data, 14)[0]
        if dib == 12 and len(data) >= 22:
            width, height = struct.unpack_from("<HH", data, 18)
        elif dib >= 40:
            width, signed_height = struct.unpack_from("<ii", data, 18)
            height = abs(signed_height)
        else:
            return None
        return (width, height) if width > 0 and height > 0 else None
    if len(data) >= 24 and data[:8] == b"\x89PNG\r\n\x1a\n":
        width, height = struct.unpack_from(">II", data, 16)
        return (width, height) if width and height else None
    return None


def dot(a: tuple[float, ...], b: tuple[float, ...]) -> float:
    return sum(x * y for x, y in zip(a, b))


def sub(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    return tuple(x - y for x, y in zip(a, b))


def triangle_mapping(positions, uvs, texture_size):
    """Return low/high singular texel density (texels/m) and anisotropy."""
    e1, e2 = sub(positions[1], positions[0]), sub(positions[2], positions[0])
    e3 = sub(positions[2], positions[1])
    edge_sq = max(dot(e1, e1), dot(e2, e2), dot(e3, e3))
    g00, g01, g11 = dot(e1, e1), dot(e1, e2), dot(e2, e2)
    gram_det = g00 * g11 - g01 * g01
    if edge_sq <= 0.0 or gram_det <= 1.0e-12 * edge_sq * edge_sq:
        return None

    d1, d2 = sub(uvs[1], uvs[0]), sub(uvs[2], uvs[0])
    uv_scale = max(dot(d1, d1), dot(d2, d2), dot(sub(uvs[2], uvs[1]), sub(uvs[2], uvs[1])))
    uv_det = d1[0] * d2[1] - d2[0] * d1[1]
    if uv_scale <= 0.0 or abs(uv_det) <= 1.0e-12 * uv_scale:
        return None

    inv = 1.0 / gram_det
    ua, ub = (d1[0] * g11 - d2[0] * g01) * inv, (d2[0] * g00 - d1[0] * g01) * inv
    va, vb = (d1[1] * g11 - d2[1] * g01) * inv, (d2[1] * g00 - d1[1] * g01) * inv
    gu = tuple((ua * x + ub * y) * texture_size[0] for x, y in zip(e1, e2))
    gv = tuple((va * x + vb * y) * texture_size[1] for x, y in zip(e1, e2))
    a, b, c = dot(gu, gu), dot(gu, gv), dot(gv, gv)
    disc = math.sqrt(max(0.0, (a - c) ** 2 + 4.0 * b * b))
    high = math.sqrt(max(0.0, 0.5 * (a + c + disc)))
    low = math.sqrt(max(0.0, 0.5 * (a + c - disc)))
    if not math.isfinite(high) or not math.isfinite(low) or low <= 0.0:
        return None
    return low, high, high / low


def percentile(values: list[float], fraction: float):
    if not values:
        return None
    ordered = sorted(values)
    at = (len(ordered) - 1) * fraction
    lo, hi = math.floor(at), math.ceil(at)
    return ordered[lo] if lo == hi else ordered[lo] * (hi - at) + ordered[hi] * (at - lo)


def analyze_obj(obj_path: Path, content_root: Path, fallback_size=None, source_frequencies=None):
    root = content_root.resolve()
    obj_path = obj_path.resolve()
    try:
        obj_path.relative_to(root)
    except ValueError as exc:
        raise DiagnosticError(f"OBJ path escapes content root: {obj_path}") from exc

    try:
        lines = obj_path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise DiagnosticError(f"cannot read OBJ {obj_path}: {exc}") from exc

    # mtllib is an OBJ-wide declaration and may legally follow face records.
    libraries, material_maps = [], {}
    for number, line in enumerate(lines, 1):
        try:
            fields = shlex.split(line, comments=True)
        except ValueError as exc:
            raise DiagnosticError(f"{obj_path}:{number}: malformed quoting: {exc}") from exc
        if fields and fields[0] == "mtllib":
            if len(fields) < 2:
                raise DiagnosticError(f"{obj_path}:{number}: empty mtllib")
            for value in fields[1:]:
                library = (obj_path.parent / value).resolve()
                try:
                    library.relative_to(root)
                except ValueError as exc:
                    raise DiagnosticError(f"material library escapes content root: {library}") from exc
                if library not in libraries:
                    libraries.append(library)
                    for name, properties in parse_mtl(library).items():
                        material_maps[name] = (properties, library)

    positions, uvs, normals = [], [], []
    stats = defaultdict(lambda: {
        "triangles": 0, "missing_uv": 0, "degenerate_geometry": 0,
        "degenerate_uv": 0, "low": [], "high": [], "anisotropy": [],
    })
    material = "(unassigned)"
    for number, line in enumerate(lines, 1):
        try:
            fields = shlex.split(line, comments=True)
        except ValueError as exc:
            raise DiagnosticError(f"{obj_path}:{number}: malformed quoting: {exc}") from exc
        if not fields:
            continue
        key, values = fields[0], fields[1:]
        context = f"{obj_path}:{number}"
        if key == "v" or key == "vn":
            if len(values) < 3:
                raise DiagnosticError(f"{context}: expected three coordinates")
            try:
                point = tuple(float(x) for x in values[:3])
            except ValueError as exc:
                raise DiagnosticError(f"{context}: invalid coordinate") from exc
            if not all(math.isfinite(x) for x in point):
                raise DiagnosticError(f"{context}: non-finite coordinate")
            (positions if key == "v" else normals).append(point)
        elif key == "vt":
            if len(values) < 2:
                raise DiagnosticError(f"{context}: expected two UV coordinates")
            try:
                uv = tuple(float(x) for x in values[:2])
            except ValueError as exc:
                raise DiagnosticError(f"{context}: invalid UV") from exc
            if not all(math.isfinite(x) for x in uv):
                raise DiagnosticError(f"{context}: non-finite UV")
            uvs.append(uv)
        elif key == "usemtl":
            if not values:
                raise DiagnosticError(f"{context}: usemtl requires a name")
            material = " ".join(values)
        elif key == "f":
            if len(values) < 3:
                raise DiagnosticError(f"{context}: face requires at least three corners")
            corners = []
            for value in values:
                parts = value.split("/")
                if not parts[0]:
                    raise DiagnosticError(f"{context}: missing position index")
                vi = resolve_index(parts[0], len(positions), context)
                ti = resolve_index(parts[1], len(uvs), context) if len(parts) > 1 and parts[1] else None
                if len(parts) > 2 and parts[2]:
                    resolve_index(parts[2], len(normals), context)
                corners.append((vi, ti))
            for i in range(1, len(corners) - 1):
                tri = (corners[0], corners[i], corners[i + 1])
                item = stats[material]
                item["triangles"] += 1
                if any(c[1] is None for c in tri):
                    item["missing_uv"] += 1
                    continue
                points = tuple(positions[c[0]] for c in tri)
                coords = tuple(uvs[c[1]] for c in tri)
                e1, e2 = sub(points[1], points[0]), sub(points[2], points[0])
                cross = (e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0])
                area = math.sqrt(dot(cross, cross))
                e3 = sub(points[2], points[1])
                max_edge_sq = max(dot(e1, e1), dot(e2, e2), dot(e3, e3))
                if max_edge_sq <= 0.0 or area <= 1.0e-12 * max_edge_sq:
                    item["degenerate_geometry"] += 1
                    continue
                properties, library = material_maps.get(material, ({}, None))
                normal_ref = properties.get("normal_map")
                dimensions = None
                if normal_ref and library:
                    texture = (library.parent / normal_ref).resolve()
                    try:
                        texture.relative_to(root)
                    except ValueError as exc:
                        raise DiagnosticError(f"normal-map path escapes content root: {texture}") from exc
                    dimensions = image_dimensions(texture)
                    item["normal_map"] = str(texture.relative_to(root))
                    item["normal_map_header_found"] = dimensions is not None
                dimensions = dimensions or fallback_size
                if dimensions is None:
                    continue
                item["texture_size"] = list(dimensions)
                mapping = triangle_mapping(points, coords, dimensions)
                if mapping is None:
                    item["degenerate_uv"] += 1
                    continue
                low, high, anisotropy = mapping
                item["low"].append(low)
                item["high"].append(high)
                item["anisotropy"].append(anisotropy)

    output = {}
    for name in sorted(stats):
        item = stats[name]
        low, high, anisotropy = item.pop("low"), item.pop("high"), item.pop("anisotropy")
        properties, library = material_maps.get(name, ({}, None))
        normal_ref = properties.get("normal_map")
        if normal_ref and library:
            texture = (library.parent / normal_ref).resolve()
            dimensions = image_dimensions(texture)
            item["normal_map"] = str(texture.relative_to(root))
            item["normal_map_header_found"] = dimensions is not None
            item["texture_size"] = list(dimensions or fallback_size) if (dimensions or fallback_size) else None
        else:
            item["normal_map"] = None
            item["normal_map_header_found"] = None
            item["texture_size"] = list(fallback_size) if fallback_size else None
        item["valid_mapped_triangles"] = len(low)
        item["texels_per_meter_min_singular"] = {
            "min": min(low) if low else None, "p05": percentile(low, .05),
            "median": percentile(low, .5), "p95": percentile(low, .95),
            "max": max(low) if low else None,
        }
        item["texels_per_meter_max_singular"] = {
            "p50": percentile(high, .5), "p95": percentile(high, .95),
            "max": max(high) if high else None,
        }
        item["anisotropy"] = {
            "p50": percentile(anisotropy, .5), "p95": percentile(anisotropy, .95),
            "max": max(anisotropy) if anisotropy else None,
        }
        frequency = None
        if normal_ref and source_frequencies:
            frequency = source_frequencies.get(Path(normal_ref).name, source_frequencies.get(Path(normal_ref).stem))
        if frequency is not None and normal_ref and item["texture_size"]:
            nyquist = min(item["texture_size"]) / 2.0
            item["source_frequency_cycles_per_uv"] = frequency
            item["texture_nyquist_cycles_per_uv"] = nyquist
            item["frequency_margin"] = nyquist / frequency
            item["sampling_assessment"] = "below_nyquist" if frequency <= nyquist else "above_nyquist_warning"
        elif not normal_ref:
            item["sampling_assessment"] = "no_normal_map"
        elif source_frequencies and frequency is not None:
            item["sampling_assessment"] = "texture_dimensions_unavailable"
        elif source_frequencies:
            item["sampling_assessment"] = "source_frequency_not_supplied_for_map"
        else:
            item["sampling_assessment"] = "not_evaluated"
        output[name] = item

    return {
        "schema": "talvisota-obj-material-diagnostics-v1",
        "obj": str(obj_path.relative_to(root)),
        "position_count": len(positions), "uv_count": len(uvs),
        "material_libraries": [str(path.relative_to(root)) for path in libraries],
        "materials": output,
        "notes": [
            "Texel density uses the singular values of the world-to-texture Jacobian.",
            "Above-Nyquist is a sampling-risk estimate, not proof of a visible defect.",
            "Missing generated-map headers use only explicitly supplied fallback dimensions.",
            "Source frequencies are caller-supplied highest explicit octaves, not measured image spectra.",
        ],
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("obj", type=Path)
    parser.add_argument("--content-root", type=Path, default=Path.cwd())
    parser.add_argument("--normal-map-size", type=int, metavar="PIXELS",
                        help="square fallback for generated maps absent from source checkout")
    parser.add_argument("--source-frequency", action="append", default=[], metavar="TEXTURE=CPUV",
                        help="highest explicit source octave in cycles per UV tile; repeat per map")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    if args.normal_map_size is not None and args.normal_map_size <= 0:
        parser.error("--normal-map-size must be positive")
    frequencies = {}
    for entry in args.source_frequency:
        name, separator, raw = entry.partition("=")
        if not separator or not name.strip():
            parser.error("--source-frequency must use TEXTURE=CPUV")
        try:
            value = float(raw)
        except ValueError:
            parser.error(f"invalid source frequency: {entry!r}")
        if not math.isfinite(value) or value <= 0.0:
            parser.error(f"source frequency must be finite and positive: {entry!r}")
        frequencies[Path(name.strip()).name] = value
        frequencies[Path(name.strip()).stem] = value
    try:
        report = analyze_obj(
            args.obj, args.content_root,
            (args.normal_map_size, args.normal_map_size) if args.normal_map_size else None,
            frequencies,
        )
    except DiagnosticError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True, allow_nan=False))
        return 0
    print(f"OBJ {report['obj']}: {report['position_count']} positions, {report['uv_count']} UVs")
    for material, item in report["materials"].items():
        density = item["texels_per_meter_min_singular"]
        print(
            f"{material}: {item['triangles']} triangles, {item['valid_mapped_triangles']} mapped, "
            f"{item['missing_uv']} missing UV, {item['degenerate_geometry']} degenerate geometry, "
            f"{item['degenerate_uv']} degenerate UV; min-singular texels/m "
            f"min/p05/median/p95/max={density['min']}/{density['p05']}/{density['median']}/{density['p95']}/{density['max']}; "
            f"anisotropy p95={item['anisotropy']['p95']}; {item['sampling_assessment']}"
        )
        if "source_frequency_cycles_per_uv" in item:
            print(f"  source={item['source_frequency_cycles_per_uv']} cycles/UV, "
                  f"Nyquist={item['texture_nyquist_cycles_per_uv']}, "
                  f"margin={item['frequency_margin']:.3f}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
