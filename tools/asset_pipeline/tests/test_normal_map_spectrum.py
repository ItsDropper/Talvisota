import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import normal_map_spectrum as spectrum


def write_bmp(path, red, green, top_down=False):
    size = len(red)
    row_bytes = size * 3
    stride = (row_bytes + 3) & ~3
    pixels = bytearray()
    for file_y in range(size):
        y = file_y if top_down else size - 1 - file_y
        for x in range(size):
            pixels.extend((255, green[y][x], red[y][x]))
        pixels.extend(b"\0" * (stride - row_bytes))
    signed_height = -size if top_down else size
    dib = struct.pack(
        "<IiiHHIIiiII", 40, size, signed_height, 1, 24, 0,
        len(pixels), 2835, 2835, 0, 0,
    )
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    path.write_bytes(header + dib + pixels)
    return path


def make_signal(size, red_frequency=0, green_frequency=0, amplitude=48):
    red = []
    green = []
    for y in range(size):
        red.append([
            max(0, min(255, round(128 + amplitude * math.cos(
                2.0 * math.pi * red_frequency * x / size
            ))))
            for x in range(size)
        ])
        green.append([
            max(0, min(255, round(128 + amplitude * math.sin(
                2.0 * math.pi * green_frequency * y / size
            ))))
            for x in range(size)
        ])
    return red, green


class NormalMapSpectrumTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def write_signal(self, name, red_frequency=0, green_frequency=0, size=64):
        red, green = make_signal(size, red_frequency, green_frequency)
        return write_bmp(self.root / name, red, green)

    def test_known_u_and_v_frequencies_land_in_exact_fft_bins(self):
        path = self.write_signal("known.bmp", red_frequency=7, green_frequency=11)
        report = spectrum.analyze_bmp(path)
        self.assertEqual(report["channels"]["red_x"]["peak_frequency_cycles_per_uv"], 7)
        self.assertEqual(report["channels"]["green_y"]["peak_frequency_cycles_per_uv"], 11)
        self.assertAlmostEqual(report["channels"]["red_x"]["total_ac_power"],
            report["channels"]["red_x"]["rms_ac_byte"] ** 2, places=8)
        self.assertAlmostEqual(report["channels"]["green_y"]["total_ac_power"],
            report["channels"]["green_y"]["rms_ac_byte"] ** 2, places=8)

    def test_constant_normal_components_have_no_ac_power(self):
        red, green = make_signal(64)
        path = write_bmp(self.root / "constant.bmp", red, green)
        report = spectrum.analyze_bmp(path)
        self.assertEqual(report["tangent_xy"]["total_ac_power"], 0.0)
        self.assertEqual(report["tangent_xy"]["near_nyquist_power_fraction"], 0.0)
        self.assertEqual(report["tangent_xy"]["peak_frequency_cycles_per_uv"], 0)

    def test_energy_bands_are_finite_and_normalized(self):
        path = self.write_signal("bands.bmp", red_frequency=5, green_frequency=27)
        report = spectrum.analyze_bmp(path)
        fractions = report["tangent_xy"]["energy_fraction_by_band"]
        self.assertAlmostEqual(sum(fractions.values()), 1.0, places=12)
        self.assertTrue(all(math.isfinite(value) and 0.0 <= value <= 1.0
            for value in fractions.values()))
        self.assertAlmostEqual(
            report["channels"]["green_y"]["near_nyquist_power_fraction"], 1.0
        )
        self.assertAlmostEqual(
            report["tangent_xy"]["near_nyquist_power_fraction"], 0.5, places=3
        )

    def test_bmp_top_down_and_bottom_up_rows_are_equivalent(self):
        red, green = make_signal(64, red_frequency=9, green_frequency=13)
        bottom_up = write_bmp(self.root / "bottom.bmp", red, green)
        top_down = write_bmp(self.root / "top.bmp", red, green, top_down=True)
        self.assertEqual(
            spectrum.analyze_bmp(bottom_up)["tangent_xy"],
            spectrum.analyze_bmp(top_down)["tangent_xy"],
        )

    def test_truncated_and_compressed_bmps_are_rejected(self):
        valid = self.write_signal("valid.bmp", red_frequency=4)
        truncated = self.root / "truncated.bmp"
        truncated.write_bytes(valid.read_bytes()[:-12])
        with self.assertRaisesRegex(spectrum.SpectrumError, "truncated"):
            spectrum.analyze_bmp(truncated)

        compressed = self.root / "compressed.bmp"
        data = bytearray(valid.read_bytes())
        struct.pack_into("<I", data, 30, 1)
        compressed.write_bytes(data)
        with self.assertRaisesRegex(spectrum.SpectrumError, "uncompressed"):
            spectrum.analyze_bmp(compressed)

    def test_non_power_of_two_and_non_square_images_are_rejected(self):
        valid = self.write_signal("valid.bmp", red_frequency=4)
        bad_size = self.root / "bad-size.bmp"
        data = bytearray(valid.read_bytes())
        struct.pack_into("<i", data, 18, 48)
        bad_size.write_bytes(data)
        with self.assertRaisesRegex(spectrum.SpectrumError, "square|power of two"):
            spectrum.analyze_bmp(bad_size)

    def test_source_octaves_fold_into_the_sampled_nyquist_interval(self):
        self.assertEqual(spectrum.fold_source_frequency(840.0, 1024), 184.0)
        self.assertEqual(spectrum.fold_source_frequency(1536.0, 1024), 512.0)
        self.assertEqual(spectrum.fold_source_frequency(40.0, 64), 24.0)

    def test_baseline_comparison_reports_candidate_minus_baseline(self):
        baseline = spectrum.analyze_bmp(self.write_signal("baseline.bmp", red_frequency=4))
        candidate = spectrum.analyze_bmp(self.write_signal("candidate.bmp", red_frequency=28))
        comparison = spectrum.compare_reports(candidate, baseline)
        delta = comparison["candidate_minus_baseline"]
        self.assertEqual(comparison["baseline_image"], "baseline.bmp")
        self.assertGreater(delta["near_nyquist_power_fraction"], 0.99)
        self.assertGreater(delta["spectral_centroid_cycles_per_uv"], 0.0)

    def test_baseline_comparison_rejects_different_dimensions(self):
        baseline = spectrum.analyze_bmp(self.write_signal("baseline.bmp", size=64))
        red, green = make_signal(128, red_frequency=8)
        candidate = spectrum.analyze_bmp(write_bmp(self.root / "candidate.bmp", red, green))
        with self.assertRaisesRegex(spectrum.SpectrumError, "identical image dimensions"):
            spectrum.compare_reports(candidate, baseline)

    def test_cli_json_is_deterministic_and_includes_alias_estimates(self):
        path = self.write_signal("cli.bmp", red_frequency=8)
        baseline = self.write_signal("baseline.bmp", red_frequency=4)
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        command = [
            sys.executable, str(script), str(path),
            "--compare", str(baseline),
            "--source-frequency", "bark=840",
            "--source-frequency", "granite=1536",
            "--json",
        ]
        first = subprocess.run(command, capture_output=True, text=True, check=False, timeout=20)
        second = subprocess.run(command, capture_output=True, text=True, check=False, timeout=20)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        report = json.loads(first.stdout)
        estimates = {item["label"]: item for item in report["source_frequency_alias_estimates"]}
        self.assertEqual(estimates["bark"]["folded_cycles_per_uv"], 8.0)
        self.assertEqual(estimates["granite"]["folded_cycles_per_uv"], 0.0)
        self.assertEqual(report["comparison"]["baseline_image"], "baseline.bmp")
        self.assertGreater(
            report["comparison"]["candidate_minus_baseline"]["spectral_centroid_cycles_per_uv"],
            0.0,
        )

    def test_cli_rejects_duplicate_source_frequency_labels(self):
        path = self.write_signal("duplicate.bmp", red_frequency=4)
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        result = subprocess.run(
            [sys.executable, str(script), str(path),
             "--source-frequency", "bark=840",
             "--source-frequency", "bark=900"],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("unique", result.stderr)

    def test_generated_bark_and_granite_maps_are_analyzed_when_available(self):
        repository_root = Path(__file__).resolve().parents[3]
        texture_root = repository_root / "assets" / "textures" / "environment"
        maps = ("bark-normal.bmp", "granite-normal.bmp")
        if not all((texture_root / name).is_file() for name in maps):
            self.skipTest("generated environment normal maps are not present in this build")
        for name in maps:
            with self.subTest(image=name):
                report = spectrum.analyze_bmp(texture_root / name)
                self.assertEqual((report["width"], report["height"]), (1024, 1024))
                self.assertEqual(report["nyquist_cycles_per_uv"], 512)
                self.assertGreater(report["tangent_xy"]["total_ac_power"], 0.0)
                self.assertAlmostEqual(
                    sum(report["tangent_xy"]["energy_fraction_by_band"].values()),
                    1.0, places=10,
                )


if __name__ == "__main__":
    unittest.main()
