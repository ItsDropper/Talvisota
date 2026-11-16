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


if __name__ == "__main__":
    unittest.main()
