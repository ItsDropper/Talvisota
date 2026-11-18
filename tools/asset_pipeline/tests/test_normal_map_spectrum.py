import hashlib
import json
import math
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import normal_map_spectrum as spectrum
import generate_environment_normal_map_reports as report_generator


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


def make_ttex_mips(base_width, base_height=None, red_frequency=4, green_frequency=7, amplitude=48):
    width = base_width
    height = base_width if base_height is None else base_height
    mips = []
    while True:
        pixels = bytearray()
        if width == height:
            red, green = make_signal(width, red_frequency, green_frequency, amplitude)
            for y in range(height):
                for x in range(width):
                    normal_x = red[y][x] / 127.5 - 1.0
                    normal_y = green[y][x] / 127.5 - 1.0
                    normal_z = math.sqrt(max(0.0, 1.0 - normal_x ** 2 - normal_y ** 2))
                    blue = round((normal_z * 0.5 + 0.5) * 255.0)
                    pixels.extend((red[y][x], green[y][x], blue, 255))
        else:
            pixels.extend(bytes((128, 128, 255, 255)) * (width * height))
        mips.append((width, height, bytes(pixels)))
        if width == 1 and height == 1:
            break
        width = max(1, (width + 1) // 2)
        height = max(1, (height + 1) // 2)
    return mips


def write_ttex(path, mips, color_space=0, version=1, pixel_format=1, trailing=b""):
    base_width, base_height, _ = mips[0]
    data = bytearray(b"TVTTEX01")
    data.extend(struct.pack(
        "<6I", version, base_width, base_height, pixel_format, color_space, len(mips)
    ))
    for width, height, rgba in mips:
        data.extend(struct.pack("<3I", width, height, len(rgba)))
        data.extend(rgba)
    data.extend(trailing)
    path.write_bytes(data)
    return path


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
        red = report["channels"]["red_x"]
        green = report["channels"]["green_y"]
        tangent = report["tangent_xy"]
        self.assertEqual(red["peak_frequency_cycles_per_uv"], 7)
        self.assertEqual(green["peak_frequency_cycles_per_uv"], 11)
        self.assertEqual(red["u_axis"]["peak_frequency_cycles_per_uv"], 7)
        self.assertEqual(green["v_axis"]["peak_frequency_cycles_per_uv"], 11)
        self.assertAlmostEqual(red["v_axis"]["zero_frequency_fraction"], 1.0, places=12)
        self.assertAlmostEqual(green["u_axis"]["zero_frequency_fraction"], 1.0, places=12)
        self.assertEqual(tangent["u_axis"]["peak_frequency_cycles_per_uv"], 7)
        self.assertEqual(tangent["v_axis"]["peak_frequency_cycles_per_uv"], 11)
        self.assertAlmostEqual(tangent["u_axis"]["zero_frequency_fraction"], 0.5, places=3)
        self.assertAlmostEqual(tangent["v_axis"]["zero_frequency_fraction"], 0.5, places=3)
        for channel in (red, green):
            self.assertAlmostEqual(channel["total_ac_power"],
                channel["rms_ac_byte"] ** 2, places=8)
            self.assertAlmostEqual(channel["u_axis"]["total_ac_power"],
                channel["total_ac_power"], places=8)
            self.assertAlmostEqual(channel["v_axis"]["total_ac_power"],
                channel["total_ac_power"], places=8)

    def test_constant_normal_components_have_no_ac_power(self):
        red, green = make_signal(64)
        path = write_bmp(self.root / "constant.bmp", red, green)
        report = spectrum.analyze_bmp(path)
        self.assertEqual(report["tangent_xy"]["total_ac_power"], 0.0)
        self.assertEqual(report["tangent_xy"]["near_nyquist_power_fraction"], 0.0)
        self.assertEqual(report["tangent_xy"]["peak_frequency_cycles_per_uv"], 0)
        self.assertEqual(report["tangent_xy"]["u_axis"]["total_ac_power"], 0.0)
        self.assertEqual(report["tangent_xy"]["v_axis"]["total_ac_power"], 0.0)
        self.assertEqual(report["tangent_xy"]["u_axis"]["zero_frequency_fraction"], 0.0)
        self.assertEqual(report["tangent_xy"]["v_axis"]["zero_frequency_fraction"], 0.0)

    def test_energy_bands_are_finite_and_normalized(self):
        path = self.write_signal("bands.bmp", red_frequency=5, green_frequency=27)
        report = spectrum.analyze_bmp(path)
        fractions = report["tangent_xy"]["energy_fraction_by_band"]
        self.assertAlmostEqual(sum(fractions.values()), 1.0, places=12)
        self.assertTrue(all(math.isfinite(value) and 0.0 <= value <= 1.0
            for value in fractions.values()))
        # Byte quantization introduces tiny harmonics outside the source bin.
        self.assertGreater(
            report["channels"]["green_y"]["near_nyquist_power_fraction"], 0.999
        )
        self.assertAlmostEqual(
            report["tangent_xy"]["near_nyquist_power_fraction"], 0.5, places=3
        )
        u_axis = report["tangent_xy"]["u_axis"]
        v_axis = report["tangent_xy"]["v_axis"]
        self.assertAlmostEqual(sum(u_axis["energy_fraction_by_band"].values()), 1.0, places=12)
        self.assertAlmostEqual(sum(v_axis["energy_fraction_by_band"].values()), 1.0, places=12)
        self.assertGreater(v_axis["near_nyquist_power_fraction"],
            u_axis["near_nyquist_power_fraction"])
        self.assertAlmostEqual(u_axis["total_ac_power"],
            report["tangent_xy"]["total_ac_power"], places=8)
        self.assertAlmostEqual(v_axis["total_ac_power"],
            report["tangent_xy"]["total_ac_power"], places=8)

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
        self.assertGreater(delta["u_axis"]["near_nyquist_power_fraction"], 0.99)
        self.assertAlmostEqual(delta["v_axis"]["near_nyquist_power_fraction"], 0.0, places=12)

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

    def test_cli_writes_utf8_json_report_and_keeps_human_summary(self):
        candidate = self.write_signal("candidate.bmp", red_frequency=8)
        report_path = self.root / "reports" / "candidate.json"
        report_path.parent.mkdir()
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        result = subprocess.run(
            [sys.executable, str(script), str(candidate),
             "--json-output", str(report_path),
             "--source-frequency", "bark=840"],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Normal-map spectrum: candidate.bmp", result.stdout)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(report["image"], "candidate.bmp")
        self.assertEqual(
            report["source_frequency_alias_estimates"][0]["folded_cycles_per_uv"],
            8.0,
        )

    def test_cli_refuses_to_overwrite_an_input_bmp(self):
        image = self.write_signal("protected.bmp", red_frequency=8)
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        result = subprocess.run(
            [sys.executable, str(script), str(image), "--json-output", str(image)],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("must not overwrite", result.stderr)
        self.assertEqual(spectrum.analyze_bmp(image)["image"], "protected.bmp")

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


    def test_processed_texture_reports_frequency_spectra_for_each_resolvable_mip(self):
        path = write_ttex(self.root / "normal.ttex", make_ttex_mips(64))
        report = spectrum.analyze_processed_texture(
            path, [("bark", 840.0), ("granite", 1536.0)]
        )
        self.assertEqual(report["container"], "TVTTEX01")
        self.assertEqual(report["color_space"], "linear")
        self.assertEqual(report["mip_count"], 7)
        self.assertEqual(
            [(mip["width"], mip["height"]) for mip in report["mips"]],
            [(64, 64), (32, 32), (16, 16), (8, 8), (4, 4), (2, 2), (1, 1)],
        )
        self.assertEqual(
            [mip["analysis_status"] for mip in report["mips"]],
            ["spectrum", "spectrum", "spectrum"] +
            ["insufficient_frequency_resolution"] * 4,
        )
        base = report["mips"][0]
        self.assertEqual(base["channels"]["red_x"]["peak_frequency_cycles_per_uv"], 4)
        self.assertEqual(base["channels"]["green_y"]["peak_frequency_cycles_per_uv"], 7)
        self.assertEqual(base["tangent_xy"]["u_axis"]["peak_frequency_cycles_per_uv"], 4)
        self.assertEqual(base["tangent_xy"]["v_axis"]["peak_frequency_cycles_per_uv"], 7)
        # Normal-vector renormalization couples X/Y components slightly after mip filtering.
        self.assertAlmostEqual(
            base["tangent_xy"]["u_axis"]["zero_frequency_fraction"], 0.5, delta=0.01
        )
        self.assertAlmostEqual(
            base["tangent_xy"]["v_axis"]["zero_frequency_fraction"], 0.5, delta=0.01
        )
        aliases = {item["label"]: item for item in base["source_frequency_alias_estimates"]}
        self.assertEqual(aliases["bark"]["folded_cycles_per_uv"], 8.0)
        self.assertEqual(aliases["granite"]["folded_cycles_per_uv"], 0.0)
        for mip in report["mips"]:
            vector_stats = mip["normal_vector_length"]
            self.assertLess(vector_stats["rms_deviation_from_unit"], 0.01)
            self.assertEqual(vector_stats["samples_outside_2_percent"], 0)
        for mip in report["mips"][:3]:
            fractions = mip["tangent_xy"]["energy_fraction_by_band"]
            self.assertAlmostEqual(sum(fractions.values()), 1.0, places=10)
            self.assertTrue(all(math.isfinite(value) for value in fractions.values()))
            for axis_name in ("u_axis", "v_axis"):
                axis = mip["tangent_xy"][axis_name]
                self.assertAlmostEqual(
                    sum(axis["energy_fraction_by_band"].values()), 1.0, places=10
                )
                self.assertAlmostEqual(
                    axis["total_ac_power"], mip["tangent_xy"]["total_ac_power"], places=8
                )
        for mip in report["mips"][3:]:
            self.assertEqual(mip["minimum_spectrum_dimension"], 16)
            self.assertTrue(math.isfinite(mip["tangent_xy"]["rms_ac_byte"]))
            self.assertNotIn("energy_fraction_by_band", mip["tangent_xy"])
            self.assertNotIn("u_axis", mip["tangent_xy"])
            self.assertNotIn("v_axis", mip["tangent_xy"])

    def test_processed_texture_comparison_reports_deltas_per_mip(self):
        baseline = write_ttex(
            self.root / "baseline.ttex",
            make_ttex_mips(64, red_frequency=3, green_frequency=7),
        )
        candidate = write_ttex(
            self.root / "candidate.ttex",
            make_ttex_mips(64, red_frequency=10, green_frequency=7),
        )
        baseline_report = spectrum.analyze_processed_texture(baseline)
        candidate_report = spectrum.analyze_processed_texture(candidate)
        comparison = spectrum.compare_processed_reports(candidate_report, baseline_report)
        self.assertEqual(comparison["baseline_image"], "baseline.ttex")
        deltas = comparison["candidate_minus_baseline_by_mip"]
        self.assertEqual(len(deltas), 7)
        self.assertGreater(deltas[0]["spectral_centroid_cycles_per_uv"], 0.0)
        self.assertGreater(
            deltas[0]["u_axis"]["spectral_centroid_cycles_per_uv"], 0.0
        )
        self.assertIn("near_nyquist_power_fraction", deltas[0])
        self.assertNotIn("near_nyquist_power_fraction", deltas[-1])
        self.assertTrue(math.isfinite(deltas[-1]["rms_ac_byte"]))
        self.assertIn("normal_vector_length", deltas[0])
        self.assertEqual(deltas[0]["normal_vector_length"]["samples_outside_2_percent"], 0)

    def test_processed_texture_cli_json_is_deterministic_and_reports_each_mip(self):
        path = write_ttex(self.root / "cli.ttex", make_ttex_mips(64))
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        command = [
            sys.executable, str(script), str(path),
            "--source-frequency", "bark=840",
            "--json",
        ]
        first = subprocess.run(command, capture_output=True, text=True, check=False, timeout=20)
        second = subprocess.run(command, capture_output=True, text=True, check=False, timeout=20)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, second.stdout)
        report = json.loads(first.stdout)
        self.assertEqual(report["container"], "TVTTEX01")
        self.assertEqual(report["mips"][0]["source_frequency_alias_estimates"][0]["label"], "bark")
        self.assertEqual(len(report["mips"]), 7)

    def test_processed_texture_human_summary_and_json_output(self):
        path = write_ttex(self.root / "human.ttex", make_ttex_mips(32))
        report_path = self.root / "reports" / "human.json"
        report_path.parent.mkdir()
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        result = subprocess.run(
            [sys.executable, str(script), str(path), "--json-output", str(report_path)],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Normal-map mip spectrum: human.ttex", result.stdout)
        self.assertIn("U axis: near-Nyquist", result.stdout)
        self.assertIn("V axis: near-Nyquist", result.stdout)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(report["mip_count"], 6)
        self.assertEqual(report["mips"][-1]["analysis_status"], "insufficient_frequency_resolution")

    def test_processed_texture_requires_linear_colour_space(self):
        path = write_ttex(
            self.root / "srgb.ttex", make_ttex_mips(16), color_space=1
        )
        with self.assertRaisesRegex(spectrum.SpectrumError, "linear-colour-space"):
            spectrum.analyze_processed_texture(path)

    def test_processed_texture_rejects_non_square_and_non_power_of_two_bases(self):
        rectangular = write_ttex(
            self.root / "rectangular.ttex", make_ttex_mips(32, base_height=16)
        )
        with self.assertRaisesRegex(spectrum.SpectrumError, "square base texture"):
            spectrum.analyze_processed_texture(rectangular)

        odd = write_ttex(self.root / "odd.ttex", make_ttex_mips(17))
        with self.assertRaisesRegex(spectrum.SpectrumError, "power of two"):
            spectrum.analyze_processed_texture(odd)

    def test_processed_texture_parser_matches_ceil_halved_odd_mip_dimensions(self):
        path = write_ttex(self.root / "odd-chain.ttex", make_ttex_mips(17))
        container = spectrum._read_processed_texture(path)
        self.assertEqual(
            [(mip[0], mip[1]) for mip in container["mips"]],
            [(17, 17), (9, 9), (5, 5), (3, 3), (2, 2), (1, 1)],
        )
        with self.assertRaisesRegex(spectrum.SpectrumError, "power of two"):
            spectrum.analyze_processed_texture(path)

    def test_processed_texture_rejects_invalid_headers_and_mip_boundaries(self):
        valid_path = write_ttex(self.root / "valid.ttex", make_ttex_mips(16))
        valid = valid_path.read_bytes()
        invalid = []

        bad_magic = bytearray(valid)
        bad_magic[0] = ord("X")
        invalid.append(("magic", bytes(bad_magic)))

        invalid.append(("truncated header", valid[:31]))

        bad_version = bytearray(valid)
        struct.pack_into("<I", bad_version, 8, 2)
        invalid.append(("version", bytes(bad_version)))

        zero_width = bytearray(valid)
        struct.pack_into("<I", zero_width, 12, 0)
        invalid.append(("zero dimensions", bytes(zero_width)))

        bad_format = bytearray(valid)
        struct.pack_into("<I", bad_format, 20, 2)
        invalid.append(("pixel format", bytes(bad_format)))

        bad_colour_space = bytearray(valid)
        struct.pack_into("<I", bad_colour_space, 24, 2)
        invalid.append(("colour space", bytes(bad_colour_space)))

        zero_mips = bytearray(valid)
        struct.pack_into("<I", zero_mips, 28, 0)
        invalid.append(("mip count", bytes(zero_mips)))

        invalid.append(("truncated mip header", valid[:37]))

        bad_mip_width = bytearray(valid)
        struct.pack_into("<I", bad_mip_width, 32, 8)
        invalid.append(("mip dimensions", bytes(bad_mip_width)))

        bad_byte_count = bytearray(valid)
        struct.pack_into("<I", bad_byte_count, 40, 1)
        invalid.append(("byte count", bytes(bad_byte_count)))

        invalid.append(("truncated pixels", valid[:-1]))
        invalid.append(("trailing bytes", valid + b"x"))
        invalid.append((
            "incomplete chain",
            write_ttex(self.root / "incomplete.ttex", make_ttex_mips(16)[:-1]).read_bytes(),
        ))

        malformed_path = self.root / "malformed.ttex"
        for label, payload in invalid:
            with self.subTest(case=label):
                malformed_path.write_bytes(payload)
                with self.assertRaises(spectrum.SpectrumError):
                    spectrum._read_processed_texture(malformed_path)

    def test_processed_texture_cli_refuses_to_overwrite_its_container(self):
        path = write_ttex(self.root / "protected.ttex", make_ttex_mips(16))
        original = path.read_bytes()
        script = Path(__file__).resolve().parents[1] / "normal_map_spectrum.py"
        result = subprocess.run(
            [sys.executable, str(script), str(path), "--json-output", str(path)],
            capture_output=True, text=True, check=False, timeout=20,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("must not overwrite", result.stderr)
        self.assertEqual(path.read_bytes(), original)



class EnvironmentNormalMapReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.source_root = self.root / "sources"
        self.source_root.mkdir()
        red, green = make_signal(16, red_frequency=2, green_frequency=3)
        write_bmp(self.source_root / "bark-normal.bmp", red, green)
        write_bmp(self.source_root / "granite-normal.bmp", red, green)
        self.processor = self.root / "talvisota_texture_process.exe"
        self.processor.write_bytes(b"deterministic fake processor")
        self.output_directory = self.root / "reports"

    def tearDown(self):
        self.temporary.cleanup()

    def _successful_processor(self, command, **kwargs):
        self.assertEqual(command[0], str(self.processor.resolve()))
        self.assertEqual(command[3], "normal")
        source = Path(command[1])
        output = Path(command[2])
        self.assertEqual(source.parent, self.source_root.resolve())
        label = source.name.split("-", 1)[0]
        self.assertIn(label, ("bark", "granite"))
        write_ttex(
            output,
            make_ttex_mips(32, red_frequency=4, green_frequency=7),
        )
        self.assertTrue(kwargs["capture_output"])
        self.assertTrue(kwargs["text"])
        self.assertEqual(kwargs["timeout"], 90)
        return subprocess.CompletedProcess(command, 0, "texture processed\n", "")

    def test_generates_deterministic_reports_and_provenance_from_real_processor_outputs(self):
        with mock.patch.object(
            report_generator.subprocess, "run", side_effect=self._successful_processor
        ) as run:
            outputs = report_generator.generate_reports(
                self.processor, self.source_root, self.output_directory, timeout_seconds=90
            )

        self.assertEqual(
            [path.name for path in outputs],
            [
                "bark-normal-spectrum.json",
                "granite-normal-spectrum.json",
                "environment-normal-map-spectrum-manifest.json",
            ],
        )
        self.assertEqual(run.call_count, 2)
        for call in run.call_args_list:
            command = call.args[0]
            self.assertEqual(command[3], "normal")

        first_bytes = {path.name: path.read_bytes() for path in outputs}
        manifest = json.loads(first_bytes["environment-normal-map-spectrum-manifest.json"])
        self.assertEqual(manifest["schema_version"], 1)
        self.assertEqual(
            manifest["processor_sha256"],
            hashlib.sha256(self.processor.read_bytes()).hexdigest(),
        )
        self.assertEqual([item["label"] for item in manifest["reports"]], ["bark", "granite"])

        for record in manifest["reports"]:
            source = self.source_root / record["source_file"]
            report_path = self.output_directory / record["report_file"]
            report_bytes = report_path.read_bytes()
            report = json.loads(report_bytes)
            self.assertEqual(record["source_sha256"], hashlib.sha256(source.read_bytes()).hexdigest())
            self.assertEqual(
                record["report_sha256"], hashlib.sha256(report_bytes).hexdigest()
            )
            self.assertEqual(record["processed_texture_sha256"], hashlib.sha256(
                write_ttex(
                    self.root / "hash-reference.ttex",
                    make_ttex_mips(32, red_frequency=4, green_frequency=7),
                ).read_bytes()
            ).hexdigest())
            self.assertEqual(report["container"], "TVTTEX01")
            self.assertEqual(report["color_space"], "linear")
            self.assertEqual(report["width"], 32)
            self.assertEqual(report["mip_count"], 6)
            expected_frequency = 840.0 if record["label"] == "bark" else 1536.0
            for mip in report["mips"]:
                self.assertEqual(
                    mip["source_frequency_alias_estimates"][0]["label"], record["label"]
                )
                self.assertEqual(
                    mip["source_frequency_alias_estimates"][0]["source_cycles_per_uv"],
                    expected_frequency,
                )

        with mock.patch.object(
            report_generator.subprocess, "run", side_effect=self._successful_processor
        ):
            report_generator.generate_reports(
                self.processor, self.source_root, self.output_directory, timeout_seconds=90
            )
        self.assertEqual(
            first_bytes,
            {path.name: path.read_bytes() for path in outputs},
        )

    def test_processor_failure_does_not_publish_partial_reports(self):
        def fail_second_map(command, **kwargs):
            if Path(command[1]).name == "granite-normal.bmp":
                return subprocess.CompletedProcess(command, 7, "", "deliberate processor failure")
            return self._successful_processor(command, **kwargs)

        with mock.patch.object(
            report_generator.subprocess, "run", side_effect=fail_second_map
        ):
            with self.assertRaisesRegex(
                report_generator.ReportGenerationError, "exit code 7.*deliberate processor failure"
            ):
                report_generator.generate_reports(
                    self.processor, self.source_root, self.output_directory, timeout_seconds=90
                )

        self.assertFalse(
            (self.output_directory / "environment-normal-map-spectrum-manifest.json").exists()
        )
        self.assertFalse((self.output_directory / "bark-normal-spectrum.json").exists())
        self.assertFalse((self.output_directory / "granite-normal-spectrum.json").exists())

    def test_processor_success_without_output_is_rejected(self):
        with mock.patch.object(
            report_generator.subprocess, "run",
            return_value=subprocess.CompletedProcess([], 0, "", ""),
        ):
            with self.assertRaisesRegex(
                report_generator.ReportGenerationError, "did not create"
            ):
                report_generator.generate_reports(
                    self.processor, self.source_root, self.output_directory
                )

    def test_malformed_processed_texture_is_rejected(self):
        def malformed(command, **kwargs):
            Path(command[2]).write_bytes(b"not a TVTTEX01 container" + b"x" * 16)
            return subprocess.CompletedProcess(command, 0, "", "")

        with mock.patch.object(report_generator.subprocess, "run", side_effect=malformed):
            with self.assertRaisesRegex(
                report_generator.ReportGenerationError, "invalid TVTTEX01 signature"
            ):
                report_generator.generate_reports(
                    self.processor, self.source_root, self.output_directory
                )

        self.assertFalse(
            (self.output_directory / "environment-normal-map-spectrum-manifest.json").exists()
        )

    def test_missing_source_is_rejected_before_starting_processor(self):
        (self.source_root / "granite-normal.bmp").unlink()
        with mock.patch.object(report_generator.subprocess, "run") as run:
            with self.assertRaisesRegex(
                report_generator.ReportGenerationError, "granite source normal map"
            ):
                report_generator.generate_reports(
                    self.processor, self.source_root, self.output_directory
                )
        run.assert_not_called()

    def test_non_positive_processor_timeout_is_rejected(self):
        with self.assertRaisesRegex(
            report_generator.ReportGenerationError, "positive"
        ):
            report_generator.generate_reports(
                self.processor, self.source_root, self.output_directory, timeout_seconds=0
            )


if __name__ == "__main__":
    unittest.main()
