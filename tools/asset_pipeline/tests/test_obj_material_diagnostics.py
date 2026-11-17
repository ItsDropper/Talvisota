import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from obj_material_diagnostics import (
    DiagnosticError,
    analyze_obj,
    image_dimensions,
    parse_mtl,
    triangle_mapping,
)


class ObjMaterialDiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        for folder in ("models", "materials", "textures"):
            (self.root / folder).mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def write_scene(self, obj_text=None, mtl_text=None):
        obj = self.root / "models" / "scene.obj"
        mtl = self.root / "materials" / "scene.mtl"
        if mtl_text is None:
            mtl_text = "newmtl wall\nmap_Bump -bm 0.5 ../textures/normal.bmp\n"
        if obj_text is None:
            obj_text = (
                "mtllib ../materials/scene.mtl\n"
                "v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\n"
                "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                "usemtl wall\nf 1/1 2/2 3/3 4/4\n"
            )
        obj.write_text(obj_text, encoding="utf-8")
        mtl.write_text(mtl_text, encoding="utf-8")
        header = bytearray(54)
        header[:2] = b"BM"
        struct.pack_into("<I", header, 14, 40)
        struct.pack_into("<ii", header, 18, 1024, 1024)
        (self.root / "textures" / "normal.bmp").write_bytes(header)
        return obj

    def test_positive_indices_quad_fan_and_density(self):
        wall = analyze_obj(self.write_scene(), self.root)["materials"]["wall"]
        self.assertEqual(wall["triangles"], 2)
        self.assertEqual(wall["valid_mapped_triangles"], 2)
        self.assertEqual(wall["degenerate_uv"], 0)
        self.assertAlmostEqual(wall["texels_per_meter_min_singular"]["median"], 512.0)
        self.assertEqual(wall["normal_map"], "textures/normal.bmp")
        self.assertTrue(wall["normal_map_header_found"])

    def test_negative_indices_resolve_at_face_line(self):
        obj = self.write_scene(
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 2 0 0\nv 2 2 0\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nusemtl wall\nf -3/-3 -2/-2 -1/-1\n"
        )
        wall = analyze_obj(obj, self.root)["materials"]["wall"]
        self.assertEqual(wall["triangles"], 1)
        self.assertEqual(wall["valid_mapped_triangles"], 1)

    def test_zero_and_out_of_range_indices_fail(self):
        for face in ("f 0/1 2/2 3/3", "f 1/1 2/2 9/3"):
            obj = self.write_scene(
                "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 0 1\nusemtl wall\n" + face + "\n"
            )
            with self.subTest(face=face), self.assertRaises(DiagnosticError):
                analyze_obj(obj, self.root)

    def test_valid_face_corner_forms_include_missing_uv_with_normal(self):
        obj = self.write_scene(
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vn 0 0 1\nusemtl wall\nf 1//1 2//1 3//1\n"
        )
        wall = analyze_obj(obj, self.root)["materials"]["wall"]
        self.assertEqual(wall["triangles"], 1)
        self.assertEqual(wall["missing_uv"], 1)
        self.assertEqual(wall["valid_mapped_triangles"], 0)

    def test_malformed_face_corner_forms_are_rejected(self):
        prefix = (
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 0 1\nusemtl wall\n"
        )
        for face in (
            "f 1/1/1/1 2/2 3/3",
            "f 1/1/ 2/2/ 3/3/",
            "f 1/ 2/ 3/",
        ):
            with self.subTest(face=face):
                obj = self.write_scene(prefix + face + "\n")
                with self.assertRaisesRegex(
                    DiagnosticError,
                    "malformed face corner|missing texture-coordinate index|missing normal index",
                ):
                    analyze_obj(obj, self.root)

    def test_degenerate_geometry_and_uv_are_separate(self):
        obj = self.write_scene(
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 2 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 2 0\nvt 0 1\nusemtl wall\n"
            "f 1/1 2/2 3/3\nf 1/1 2/2 4/2\n"
        )
        wall = analyze_obj(obj, self.root)["materials"]["wall"]
        self.assertEqual(wall["degenerate_geometry"], 1)
        self.assertEqual(wall["degenerate_uv"], 1)
        self.assertEqual(wall["valid_mapped_triangles"], 0)

    def test_degeneracy_threshold_is_scale_aware(self):
        for scale in (1.0e-6, 1.0, 1.0e6):
            with self.subTest(scale=scale):
                mapping = triangle_mapping(
                    ((0, 0, 0), (2*scale, 0, 0), (0, 2*scale, 0)),
                    ((0, 0), (1, 0), (0, 1)), (1024, 1024),
                )
                self.assertIsNotNone(mapping)
                self.assertAlmostEqual(mapping[2], 1.0)

    def test_anisotropy_uses_jacobian_singular_values(self):
        low, high, anisotropy = triangle_mapping(
            ((0, 0, 0), (4, 0, 0), (0, 1, 0)),
            ((0, 0), (1, 0), (0, 1)), (1024, 1024),
        )
        self.assertAlmostEqual(low, 256.0)
        self.assertAlmostEqual(high, 1024.0)
        self.assertAlmostEqual(anisotropy, 4.0)

    def test_source_frequency_uses_conservative_texture_nyquist(self):
        obj = self.write_scene()
        wall = analyze_obj(obj, self.root, source_frequencies={"normal.bmp": 600.0})["materials"]["wall"]
        self.assertEqual(wall["sampling_assessment"], "above_nyquist_warning")
        self.assertEqual(wall["texture_nyquist_cycles_per_uv"], 512.0)
        wall = analyze_obj(obj, self.root, source_frequencies={"normal": 400.0})["materials"]["wall"]
        self.assertEqual(wall["sampling_assessment"], "below_nyquist")

    def test_mtl_map_options_and_quoted_path(self):
        mtl = self.root / "materials" / "options.mtl"
        mtl.write_text(
            'newmtl plaster\nmap_Bump -bm 0.35 -s 1 1 1 -clamp on "../textures/normal map.bmp"\n',
            encoding="utf-8",
        )
        self.assertEqual(parse_mtl(mtl)["plaster"]["normal_map"], "../textures/normal map.bmp")

    def test_path_escape_is_rejected(self):
        obj = self.write_scene(mtl_text="newmtl wall\nmap_Bump ../../../outside.bmp\n")
        with self.assertRaisesRegex(DiagnosticError, "escapes content root"):
            analyze_obj(obj, self.root)

    def test_escaped_map_is_rejected_even_without_valid_uv_faces(self):
        obj = self.write_scene(
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "usemtl wall\nf 1 2 3\n",
            mtl_text="newmtl wall\nmap_Bump ../../../outside.bmp\n",
        )
        with self.assertRaisesRegex(DiagnosticError, "escapes content root"):
            analyze_obj(
                obj,
                self.root,
                fallback_size=(1024, 1024),
            )

    def test_windows_style_asset_paths_normalize_to_posix(self):
        obj = self.write_scene(
            "mtllib \"..\\materials\\scene.mtl\"\nv 0 0 0\nv 2 0 0\nv 2 2 0\n"
            "vt 0 0\nvt 1 0\nvt 1 1\nusemtl wall\nf 1/1 2/2 3/3\n",
            mtl_text="newmtl wall\nmap_Bump \"..\\textures\\normal.bmp\"\n",
        )
        report = analyze_obj(obj, self.root)
        self.assertEqual(report["obj"], "models/scene.obj")
        self.assertEqual(report["material_libraries"], ["materials/scene.mtl"])
        self.assertEqual(report["materials"]["wall"]["normal_map"], "textures/normal.bmp")

    def test_missing_generated_map_uses_explicit_dimensions(self):
        obj = self.write_scene()
        (self.root / "textures" / "normal.bmp").unlink()
        wall = analyze_obj(obj, self.root, fallback_size=(1024, 1024))["materials"]["wall"]
        self.assertFalse(wall["normal_map_header_found"])
        self.assertEqual(wall["texture_size"], [1024, 1024])
        self.assertEqual(wall["valid_mapped_triangles"], 2)

    def test_normal_indices_are_validated(self):
        obj = self.write_scene(
            "mtllib ../materials/scene.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
            "vt 0 0\nvt 1 0\nvt 0 1\nusemtl wall\nf 1/1/1 2/2/1 3/3/1\n"
        )
        with self.assertRaisesRegex(DiagnosticError, "outside 0-entry array"):
            analyze_obj(obj, self.root)

    def test_mtllib_can_follow_faces(self):
        obj = self.write_scene(
            "v 0 0 0\nv 2 0 0\nv 2 2 0\nvt 0 0\nvt 1 0\nvt 1 1\n"
            "usemtl wall\nf 1/1 2/2 3/3\nmtllib ../materials/scene.mtl\n"
        )
        self.assertEqual(
            analyze_obj(obj, self.root)["materials"]["wall"]["normal_map"],
            "textures/normal.bmp",
        )

    def test_png_and_bmp_headers(self):
        self.write_scene()
        png = self.root / "textures" / "normal.png"
        png.write_bytes(
            b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR"
            + struct.pack(">II", 256, 128) + b"\x08\x06\x00\x00\x00"
        )
        self.assertEqual(image_dimensions(png), (256, 128))
        self.assertEqual(image_dimensions(self.root / "textures" / "normal.bmp"), (1024, 1024))

    def test_cli_json_and_frequency_argument(self):
        obj = self.write_scene()
        script = Path(__file__).resolve().parents[1] / "obj_material_diagnostics.py"
        result = subprocess.run(
            [sys.executable, str(script), str(obj), "--content-root", str(self.root),
             "--normal-map-size", "1024", "--source-frequency", "normal.bmp=600", "--json"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout)["materials"]["wall"]["sampling_assessment"],
            "above_nyquist_warning",
        )

    def test_json_report_is_deterministic_and_finite(self):
        obj = self.write_scene()
        first = json.dumps(analyze_obj(obj, self.root), sort_keys=True, allow_nan=False)
        second = json.dumps(analyze_obj(obj, self.root), sort_keys=True, allow_nan=False)
        self.assertEqual(first, second)


    def test_shipped_interior_obj_cli_reports(self):
        repository_root = Path(__file__).resolve().parents[3]
        script = Path(__file__).resolve().parents[1] / "obj_material_diagnostics.py"
        fixtures = {
            "church-interior.obj": {
                "triangles": 732,
                "materials": {
                    "church_boards": 312,
                    "door": 24,
                    "foundation_stone": 12,
                    "roof_timber": 336,
                    "roof_wood": 48,
                },
            },
            "farmhouse-interior.obj": {
                "triangles": 276,
                "materials": {
                    "chimney_brick": 12,
                    "foundation_stone": 24,
                    "house_logs": 84,
                    "roof_wood": 48,
                    "timber": 108,
                },
            },
        }

        for filename, expected in fixtures.items():
            with self.subTest(obj=filename):
                obj = repository_root / "assets" / "models" / "interiors" / filename
                command = [
                    sys.executable,
                    str(script),
                    str(obj),
                    "--content-root",
                    str(repository_root),
                    "--normal-map-size",
                    "1024",
                    "--source-frequency",
                    "bark-normal.bmp=840",
                    "--source-frequency",
                    "granite-normal.bmp=1536",
                    "--json",
                ]
                result = subprocess.run(
                    command,
                    capture_output=True,
                    text=True,
                    check=False,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                report = json.loads(result.stdout)
                self.assertEqual(report["obj"], obj.relative_to(repository_root).as_posix())
                self.assertEqual(
                    report["material_libraries"],
                    ["assets/materials/environment/village.mtl"],
                )
                materials = report["materials"]
                self.assertEqual(
                    {name: item["triangles"] for name, item in materials.items()},
                    expected["materials"],
                )
                self.assertEqual(
                    sum(item["triangles"] for item in materials.values()),
                    expected["triangles"],
                )

                for name, item in materials.items():
                    with self.subTest(obj=filename, material=name):
                        triangles = expected["materials"][name]
                        self.assertEqual(item["valid_mapped_triangles"], triangles)
                        self.assertEqual(item["missing_uv"], 0)
                        self.assertEqual(item["degenerate_geometry"], 0)
                        self.assertEqual(item["degenerate_uv"], 0)
                        self.assertEqual(item["texture_size"], [1024, 1024])

                        density = item["texels_per_meter_min_singular"]
                        for statistic in ("min", "p05", "median", "p95", "max"):
                            self.assertAlmostEqual(density[statistic], 256.0, places=5)
                        self.assertAlmostEqual(item["anisotropy"]["p95"], 1.0, places=6)

                        normal_map = item["normal_map"]
                        self.assertIsNotNone(normal_map)
                        map_name = Path(normal_map).name
                        if map_name == "bark-normal.bmp":
                            source_frequency = 840.0
                        elif map_name == "granite-normal.bmp":
                            source_frequency = 1536.0
                        elif map_name == "snow-normal.bmp":
                            self.assertEqual(
                                item["sampling_assessment"],
                                "source_frequency_not_supplied_for_map",
                            )
                            continue
                        else:
                            self.fail(f"Unexpected interior normal map: {normal_map}")

                        self.assertEqual(item["source_frequency_cycles_per_uv"], source_frequency)
                        self.assertEqual(item["texture_nyquist_cycles_per_uv"], 512.0)
                        self.assertAlmostEqual(
                            item["frequency_margin"],
                            512.0 / source_frequency,
                            places=7,
                        )
                        self.assertEqual(item["sampling_assessment"], "above_nyquist_warning")


if __name__ == "__main__":
    unittest.main()
