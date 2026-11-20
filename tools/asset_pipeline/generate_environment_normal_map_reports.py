#!/usr/bin/env python3
"""Generate reproducible spectral reports from the shipped processed normal maps.

The engine texture processor is deliberately invoked as a child process: this
checks the same executable and command-line contract used by the Windows build.
The resulting TVTTEX01 files are analyzed and retained beside the reports
for independent verification.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import normal_map_spectrum


class ReportGenerationError(RuntimeError):
    """A texture could not be processed or its diagnostic report was invalid."""


_ENVIRONMENT_MAPS: Tuple[Tuple[str, str, float], ...] = (
    ("bark", "bark-normal.bmp", 840.0),
    ("granite", "granite-normal.bmp", 1536.0),
)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as exc:
        raise ReportGenerationError("Could not hash '{}': {}".format(path, exc)) from exc
    return digest.hexdigest()


def _run_texture_processor(
    processor: Path, source: Path, output: Path, timeout_seconds: int
) -> None:
    if output.exists():
        raise ReportGenerationError(
            "Refusing to reuse an existing processed texture: {}".format(output)
        )

    command = [str(processor), str(source), str(output), "normal"]
    try:
        result = subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        raise ReportGenerationError(
            "Texture processing timed out after {} seconds for '{}'.{}".format(
                timeout_seconds, source.name,
                "\n" + str(exc.stderr) if exc.stderr else "",
            )
        ) from exc
    except OSError as exc:
        raise ReportGenerationError(
            "Could not start texture processor '{}': {}".format(processor, exc)
        ) from exc

    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
        raise ReportGenerationError(
            "Texture processor failed for '{}' with exit code {}: {}".format(
                source.name, result.returncode, details
            )
        )
    if output.is_symlink():
        raise ReportGenerationError(
            "Texture processor output must not be a symbolic link: {}".format(output)
        )
    if not output.is_file():
        raise ReportGenerationError(
            "Texture processor returned success but did not create a regular file: {}".format(
                output
            )
        )
    try:
        size = output.stat().st_size
    except OSError as exc:
        raise ReportGenerationError(
            "Could not inspect processed texture '{}': {}".format(output, exc)
        ) from exc
    if size < 32:
        raise ReportGenerationError(
            "Texture processor created an empty or truncated output '{}': {} bytes.".format(
                output, size
            )
        )


def _require_report_object(value: object, keys: Sequence[str], description: str) -> Dict[str, object]:
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ReportGenerationError(
            "{} must be an object with exactly these fields: {}.".format(
                description, ", ".join(sorted(keys))
            )
        )
    return value


def _require_report_number(
    value: object,
    description: str,
    minimum: Optional[float] = None,
    maximum: Optional[float] = None,
) -> float:
    if type(value) not in (int, float) or not math.isfinite(float(value)):
        raise ReportGenerationError("{} must be a finite number.".format(description))
    number = float(value)
    if minimum is not None and number < minimum:
        raise ReportGenerationError("{} must be at least {}.".format(description, minimum))
    if maximum is not None and number > maximum:
        raise ReportGenerationError("{} must be at most {}.".format(description, maximum))
    return number


def _require_report_close(actual: float, expected: float, description: str) -> None:
    if not math.isclose(actual, expected, rel_tol=1.0e-6, abs_tol=1.0e-7):
        raise ReportGenerationError(
            "{} is inconsistent: expected {:.12g}, found {:.12g}.".format(
                description, expected, actual
            )
        )


def _validate_frequency_distribution(
    summary: Dict[str, object],
    dimension: int,
    description: str,
    axis_projection: bool = False,
) -> float:
    base_keys = {
        "peak_frequency_cycles_per_uv",
        "spectral_centroid_cycles_per_uv",
        "near_nyquist_power_fraction",
        "energy_fraction_by_band",
    }
    if axis_projection:
        keys = base_keys | {
            "total_ac_power",
            "zero_frequency_power",
            "zero_frequency_fraction",
            "nonzero_frequency_power",
            "nonzero_frequency_fraction",
        }
        _require_report_object(summary, keys, description)
    else:
        keys = base_keys | {"total_ac_power"}
        if not isinstance(summary, dict) or not keys.issubset(summary):
            raise ReportGenerationError(
                "{} is missing required frequency-summary fields.".format(description)
            )

    nyquist = dimension // 2
    total_power = _require_report_number(
        summary["total_ac_power"], description + ".total_ac_power", 0.0
    )
    if axis_projection:
        zero_power = _require_report_number(
            summary["zero_frequency_power"], description + ".zero_frequency_power", 0.0
        )
        nonzero_power = _require_report_number(
            summary["nonzero_frequency_power"], description + ".nonzero_frequency_power", 0.0
        )
        _require_report_close(
            zero_power + nonzero_power, total_power,
            description + " zero/non-zero power conservation",
        )
        zero_fraction = _require_report_number(
            summary["zero_frequency_fraction"], description + ".zero_frequency_fraction", 0.0, 1.0
        )
        nonzero_fraction = _require_report_number(
            summary["nonzero_frequency_fraction"], description + ".nonzero_frequency_fraction", 0.0, 1.0
        )
        if total_power > 0.0:
            _require_report_close(
                zero_fraction, zero_power / total_power,
                description + ".zero_frequency_fraction",
            )
            _require_report_close(
                nonzero_fraction, nonzero_power / total_power,
                description + ".nonzero_frequency_fraction",
            )
            _require_report_close(
                zero_fraction + nonzero_fraction, 1.0,
                description + " zero/non-zero frequency fractions",
            )
        elif zero_fraction != 0.0 or nonzero_fraction != 0.0:
            raise ReportGenerationError(
                "{} must report zero fractions when it has no AC power.".format(description)
            )
        normalization_power = nonzero_power
    else:
        normalization_power = total_power

    peak = summary["peak_frequency_cycles_per_uv"]
    if type(peak) is not int or not 0 <= peak <= nyquist:
        raise ReportGenerationError(
            "{}.peak_frequency_cycles_per_uv must be an integer in [0, {}].".format(
                description, nyquist
            )
        )
    centroid = _require_report_number(
        summary["spectral_centroid_cycles_per_uv"],
        description + ".spectral_centroid_cycles_per_uv",
        0.0,
        float(nyquist),
    )
    near_nyquist = _require_report_number(
        summary["near_nyquist_power_fraction"],
        description + ".near_nyquist_power_fraction",
        0.0,
        1.0,
    )
    bands = _require_report_object(
        summary["energy_fraction_by_band"],
        normal_map_spectrum._BAND_NAMES,
        description + ".energy_fraction_by_band",
    )
    fractions = [
        _require_report_number(bands[name], description + "." + name, 0.0, 1.0)
        for name in normal_map_spectrum._BAND_NAMES
    ]
    expected_fraction_sum = 1.0 if normalization_power > 0.0 else 0.0
    _require_report_close(
        sum(fractions), expected_fraction_sum,
        description + " frequency-band normalization",
    )
    _require_report_close(
        near_nyquist, fractions[-1], description + " near-Nyquist band",
    )
    if normalization_power > 0.0 and peak == 0:
        raise ReportGenerationError(
            "{} must have a non-zero peak when non-zero frequency power exists.".format(description)
        )
    if normalization_power == 0.0 and (peak != 0 or centroid != 0.0):
        raise ReportGenerationError(
            "{} must report a zero peak and centroid without frequency power.".format(description)
        )
    return total_power


def _validate_spectrum_summary(
    summary: Dict[str, object],
    dimension: int,
    description: str,
    combined: bool,
) -> None:
    keys = {
        "total_ac_power",
        "peak_frequency_cycles_per_uv",
        "spectral_centroid_cycles_per_uv",
        "near_nyquist_power_fraction",
        "energy_fraction_by_band",
        "rms_ac_byte",
        "u_axis",
        "v_axis",
    }
    if not combined:
        keys.add("mean_byte")
    _require_report_object(summary, keys, description)

    rms = _require_report_number(
        summary["rms_ac_byte"],
        description + ".rms_ac_byte",
        0.0,
        math.sqrt(2.0) * 127.5 if combined else 127.5,
    )
    expected_power = rms * rms
    radial_power = _validate_frequency_distribution(summary, dimension, description)
    _require_report_close(
        radial_power, expected_power, description + " RMS/power consistency",
    )
    if not combined:
        _require_report_number(
            summary["mean_byte"], description + ".mean_byte", 0.0, 255.0
        )

    u_power = _validate_frequency_distribution(
        summary["u_axis"], dimension, description + ".u_axis", axis_projection=True
    )
    v_power = _validate_frequency_distribution(
        summary["v_axis"], dimension, description + ".v_axis", axis_projection=True
    )
    _require_report_close(
        u_power, expected_power, description + " U-axis Parseval conservation",
    )
    _require_report_close(
        v_power, expected_power, description + " V-axis Parseval conservation",
    )
    _require_report_close(
        u_power, v_power, description + " U/V power conservation",
    )


def _validate_scalar_summary(
    summary: Dict[str, object], description: str, combined: bool
) -> None:
    if combined:
        _require_report_object(
            summary, {"total_ac_power", "rms_ac_byte"}, description
        )
        rms = _require_report_number(
            summary["rms_ac_byte"], description + ".rms_ac_byte", 0.0,
            math.sqrt(2.0) * 127.5,
        )
        power = _require_report_number(
            summary["total_ac_power"], description + ".total_ac_power", 0.0
        )
        _require_report_close(power, rms * rms, description + " RMS/power consistency")
    else:
        _require_report_object(summary, {"mean_byte", "rms_ac_byte"}, description)
        _require_report_number(
            summary["mean_byte"], description + ".mean_byte", 0.0, 255.0
        )
        _require_report_number(
            summary["rms_ac_byte"], description + ".rms_ac_byte", 0.0, 127.5
        )


def _validate_report(
    report: Dict[str, object], label: str, source_frequency: float
) -> None:
    _require_report_object(
        report,
        {
            "image", "container", "version", "width", "height",
            "pixel_format", "color_space", "mip_count", "mips",
        },
        label + " report",
    )
    if (
        report["image"] != "{}-normal.ttex".format(label)
        or report["container"] != "TVTTEX01"
        or type(report["version"]) is not int
        or report["version"] != 1
        or report["pixel_format"] != "RGBA8"
        or report["color_space"] != "linear"
    ):
        raise ReportGenerationError(
            "{} report has unsupported image, container, version, or format metadata.".format(label)
        )

    width = report["width"]
    height = report["height"]
    mips = report["mips"]
    if (
        type(width) is not int
        or type(height) is not int
        or width != height
        or width < 16
        or width > 2048
        or width & (width - 1)
        or not isinstance(mips, list)
        or type(report["mip_count"]) is not int
        or report["mip_count"] != width.bit_length()
        or report["mip_count"] != len(mips)
    ):
        raise ReportGenerationError(
            "{} report has invalid base dimensions or mip inventory.".format(label)
        )

    expected_width = width
    expected_height = height
    for level, mip in enumerate(mips):
        if not isinstance(mip, dict):
            raise ReportGenerationError(
                "{} report has a non-object mip entry at level {}.".format(label, level)
            )
        resolvable = expected_width >= normal_map_spectrum._MIN_SPECTRUM_DIMENSION
        mip_keys = {
            "level", "width", "height", "nyquist_cycles_per_uv",
            "normal_vector_length", "analysis_status", "channels",
            "tangent_xy", "source_frequency_alias_estimates",
        }
        if not resolvable:
            mip_keys.add("minimum_spectrum_dimension")
        _require_report_object(mip, mip_keys, "{} mip {}".format(label, level))

        if (
            type(mip["level"]) is not int
            or mip["level"] != level
            or type(mip["width"]) is not int
            or mip["width"] != expected_width
            or type(mip["height"]) is not int
            or mip["height"] != expected_height
        ):
            raise ReportGenerationError(
                "{} report has an invalid dimension at mip {}.".format(label, level)
            )
        nyquist = _require_report_number(
            mip["nyquist_cycles_per_uv"],
            "{} mip {} Nyquist frequency".format(label, level),
            0.0,
        )
        _require_report_close(
            nyquist, expected_width / 2.0,
            "{} mip {} Nyquist frequency".format(label, level),
        )

        normal = _require_report_object(
            mip["normal_vector_length"],
            {
                "mean", "minimum", "maximum", "rms_deviation_from_unit",
                "samples_outside_2_percent",
            },
            "{} mip {} normal-vector metrics".format(label, level),
        )
        normal_mean = _require_report_number(
            normal["mean"], "{} mip {} normal mean".format(label, level), 0.0, math.sqrt(3.0)
        )
        normal_minimum = _require_report_number(
            normal["minimum"], "{} mip {} normal minimum".format(label, level), 0.0, math.sqrt(3.0)
        )
        normal_maximum = _require_report_number(
            normal["maximum"], "{} mip {} normal maximum".format(label, level), 0.0, math.sqrt(3.0)
        )
        _require_report_number(
            normal["rms_deviation_from_unit"],
            "{} mip {} normal-length RMS deviation".format(label, level),
            0.0,
            1.0,
        )
        if not normal_minimum <= normal_mean <= normal_maximum:
            raise ReportGenerationError(
                "{} mip {} normal-length mean is outside its reported range.".format(label, level)
            )
        outside = normal["samples_outside_2_percent"]
        if type(outside) is not int or not 0 <= outside <= expected_width * expected_height:
            raise ReportGenerationError(
                "{} report has an invalid normal-vector tolerance count at mip {}.".format(
                    label, level
                )
            )

        estimates = mip["source_frequency_alias_estimates"]
        if not isinstance(estimates, list) or len(estimates) != 1 or not isinstance(estimates[0], dict):
            raise ReportGenerationError(
                "{} report has an invalid source-frequency estimate at mip {}.".format(label, level)
            )
        estimate = _require_report_object(
            estimates[0],
            {"label", "source_cycles_per_uv", "above_nyquist", "folded_cycles_per_uv"},
            "{} mip {} source-frequency estimate".format(label, level),
        )
        expected_above_nyquist = source_frequency > expected_width / 2.0
        expected_folded = normal_map_spectrum.fold_source_frequency(
            source_frequency, expected_width
        )
        if (
            estimate["label"] != label
            or type(estimate["above_nyquist"]) is not bool
            or estimate["above_nyquist"] != expected_above_nyquist
        ):
            raise ReportGenerationError(
                "{} report has inconsistent source-frequency metadata at mip {}.".format(
                    label, level
                )
            )
        _require_report_close(
            _require_report_number(
                estimate["source_cycles_per_uv"],
                "{} mip {} source frequency".format(label, level),
                0.0,
            ),
            source_frequency,
            "{} mip {} source frequency".format(label, level),
        )
        _require_report_close(
            _require_report_number(
                estimate["folded_cycles_per_uv"],
                "{} mip {} folded source frequency".format(label, level),
                0.0,
                expected_width / 2.0,
            ),
            expected_folded,
            "{} mip {} folded source frequency".format(label, level),
        )

        channels = _require_report_object(
            mip["channels"], {"red_x", "green_y"},
            "{} mip {} channels".format(label, level),
        )
        tangent = mip["tangent_xy"]
        if resolvable:
            if mip["analysis_status"] != "spectrum":
                raise ReportGenerationError(
                    "{} mip {} must contain a frequency spectrum.".format(label, level)
                )
            _validate_spectrum_summary(
                channels["red_x"], expected_width,
                "{} mip {} red_x".format(label, level), combined=False,
            )
            _validate_spectrum_summary(
                channels["green_y"], expected_width,
                "{} mip {} green_y".format(label, level), combined=False,
            )
            _validate_spectrum_summary(
                tangent, expected_width,
                "{} mip {} tangent_xy".format(label, level), combined=True,
            )
        else:
            if (
                mip["analysis_status"] != "insufficient_frequency_resolution"
                or type(mip["minimum_spectrum_dimension"]) is not int
                or mip["minimum_spectrum_dimension"] != normal_map_spectrum._MIN_SPECTRUM_DIMENSION
            ):
                raise ReportGenerationError(
                    "{} mip {} has invalid low-resolution spectrum metadata.".format(label, level)
                )
            _validate_scalar_summary(
                channels["red_x"], "{} mip {} red_x".format(label, level), combined=False
            )
            _validate_scalar_summary(
                channels["green_y"], "{} mip {} green_y".format(label, level), combined=False
            )
            _validate_scalar_summary(
                tangent, "{} mip {} tangent_xy".format(label, level), combined=True
            )
        expected_width = max(1, (expected_width + 1) // 2)
        expected_height = max(1, (expected_height + 1) // 2)

    if (mips[-1]["width"], mips[-1]["height"]) != (1, 1):
        raise ReportGenerationError(
            "{} report does not contain a complete mip chain.".format(label)
        )

def _json_bytes(value: object) -> bytes:
    try:
        return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ReportGenerationError("Could not serialize deterministic report JSON: {}".format(exc)) from exc


def _resolve_source_file(path: Path, label: str) -> Path:
    """Resolve one shipped source map without following a file-level symlink."""
    try:
        if path.is_symlink():
            raise ReportGenerationError(
                "Required {} source normal map must not be a symbolic link: {}".format(
                    label, path
                )
            )
        source = path.resolve(strict=True)
    except ReportGenerationError:
        raise
    except (OSError, RuntimeError) as exc:
        raise ReportGenerationError(
            "Required {} source normal map is not a regular file: {}".format(label, path)
        ) from exc
    if not source.is_file():
        raise ReportGenerationError(
            "Required {} source normal map is not a regular file: {}".format(label, path)
        )
    return source


def generate_reports(
    texture_processor: Path,
    source_root: Path,
    output_directory: Path,
    timeout_seconds: int = 300,
) -> List[Path]:
    if timeout_seconds <= 0:
        raise ReportGenerationError("Processor timeout must be a positive number of seconds.")

    try:
        processor = Path(texture_processor).resolve()
        generator = Path(__file__).resolve(strict=True)
        analyzer = Path(normal_map_spectrum.__file__).resolve(strict=True)
        output_dir = Path(output_directory).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        source_candidates = [
            (label, Path(source_root) / filename, frequency)
            for label, filename, frequency in _ENVIRONMENT_MAPS
        ]
    except (OSError, RuntimeError) as exc:
        raise ReportGenerationError(
            "Could not resolve report inputs or output directory: {}".format(exc)
        ) from exc

    manifest_path = output_dir / "environment-normal-map-spectrum-manifest.json"
    processed_names = {
        label: "{}-normal.ttex".format(label)
        for label, _, _ in _ENVIRONMENT_MAPS
    }
    report_names = {
        label: "{}-normal-spectrum.json".format(label)
        for label, _, _ in _ENVIRONMENT_MAPS
    }
    # A missing manifest means the payload files do not represent a complete run.
    # Check aliases before cleanup: the manifest path itself may be the processor.
    previous_outputs = [
        manifest_path,
        *(output_dir / name for name in processed_names.values()),
        *(output_dir / name for name in report_names.values()),
    ]
    try:
        protected_paths = {
            processor,
            generator,
            analyzer,
            *(source.resolve() for _, source, _ in source_candidates),
        }
        resolved_outputs = {path.resolve() for path in previous_outputs}
        if protected_paths & resolved_outputs:
            raise ReportGenerationError("A report output path would overwrite an input file.")

        # Invalidate the previous artifact before validating inputs or running the
        # processor. Unlink removes a stale symlink itself, never its target.
        for path in previous_outputs:
            try:
                path.unlink(missing_ok=True)
            except OSError as exc:
                raise ReportGenerationError(
                    "Could not invalidate previous report output '{}': {}".format(path, exc)
                ) from exc

        if not processor.is_file():
            raise ReportGenerationError(
                "Texture processor is not a file: {}".format(processor)
            )
        source_paths = [
            (label, _resolve_source_file(source, label), frequency)
            for label, source, frequency in source_candidates
        ]

        source_hashes = {
            label: _sha256(source) for label, source, _ in source_paths
        }
        processor_hash = _sha256(processor)
        generator_hash = _sha256(generator)
        analyzer_hash = _sha256(analyzer)
        with tempfile.TemporaryDirectory(
            prefix=".environment-normal-map-spectrum-", dir=str(output_dir)
        ) as temporary:
            staging = Path(temporary)
            report_records: List[Dict[str, object]] = []
            staged_outputs: List[Tuple[Path, Path]] = []

            for label, source, frequency in source_paths:
                processed = staging / processed_names[label]
                _run_texture_processor(processor, source, processed, timeout_seconds)
                if _sha256(source) != source_hashes[label]:
                    raise ReportGenerationError(
                        "{} source normal map changed during processing.".format(label)
                    )
                if _sha256(processor) != processor_hash:
                    raise ReportGenerationError("Texture processor changed during report generation.")

                try:
                    report = normal_map_spectrum.analyze_processed_texture(
                        processed, [(label, frequency)]
                    )
                except normal_map_spectrum.SpectrumError as exc:
                    raise ReportGenerationError(
                        "Could not analyze processed {} normal map: {}".format(label, exc)
                    ) from exc
                _validate_report(report, label, frequency)

                report_data = _json_bytes(report)
                staged_report = staging / report_names[label]
                staged_report.write_bytes(report_data)
                staged_outputs.append((processed, output_dir / processed_names[label]))
                staged_outputs.append((staged_report, output_dir / report_names[label]))
                report_records.append({
                    "label": label,
                    "source_file": source.name,
                    "source_sha256": source_hashes[label],
                    "processed_file": processed_names[label],
                    "processed_texture_sha256": _sha256(processed),
                    "report_file": report_names[label],
                    "report_sha256": hashlib.sha256(report_data).hexdigest(),
                    "width": report["width"],
                    "height": report["height"],
                    "mip_count": report["mip_count"],
                    "source_cycles_per_uv": frequency,
                })

            for label, source, _ in source_paths:
                if _sha256(source) != source_hashes[label]:
                    raise ReportGenerationError(
                        "{} source normal map changed during report generation.".format(label)
                    )
            if _sha256(processor) != processor_hash:
                raise ReportGenerationError("Texture processor changed during report generation.")
            if _sha256(generator) != generator_hash or _sha256(analyzer) != analyzer_hash:
                raise ReportGenerationError("Report generation code changed during report generation.")

            manifest = {
                "schema_version": 2,
                "generator_file": generator.name,
                "generator_sha256": generator_hash,
                "processor_file": processor.name,
                "processor_sha256": processor_hash,
                "analyzer_file": analyzer.name,
                "analyzer_sha256": analyzer_hash,
                "reports": report_records,
            }
            staged_manifest = staging / manifest_path.name
            staged_manifest.write_bytes(_json_bytes(manifest))

            published_outputs: List[Path] = []
            try:
                for staged_output, final_output in staged_outputs:
                    os.replace(str(staged_output), str(final_output))
                    published_outputs.append(final_output)
                os.replace(str(staged_manifest), str(manifest_path))
            except OSError as exc:
                rollback_errors = []
                for published_output in reversed(published_outputs):
                    try:
                        published_output.unlink(missing_ok=True)
                    except OSError as rollback_exc:
                        rollback_errors.append(
                            "{}: {}".format(published_output, rollback_exc)
                        )
                if rollback_errors:
                    raise ReportGenerationError(
                        "Could not publish normal-map reports: {}; rollback also failed for {}."
                        .format(exc, "; ".join(rollback_errors))
                    ) from exc
                raise
    except ReportGenerationError:
        raise
    except (OSError, RuntimeError) as exc:
        raise ReportGenerationError("Could not publish normal-map reports: {}".format(exc)) from exc

    return (
        [output_dir / processed_names[label] for label, _, _ in source_paths]
        + [output_dir / report_names[label] for label, _, _ in source_paths]
        + [manifest_path]
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Process and analyze the shipped bark and granite normal-map mip chains."
    )
    parser.add_argument(
        "--texture-processor", type=Path, required=True,
        help="path to talvisota_texture_process.exe from the MSVC engine build",
    )
    parser.add_argument(
        "--source-root", type=Path, default=Path("assets/textures/environment"),
        help="directory containing bark-normal.bmp and granite-normal.bmp",
    )
    parser.add_argument(
        "--output-directory", type=Path, default=Path("artifacts/normal-map-spectrum"),
        help="directory for processed .ttex containers, two JSON reports and the provenance manifest",
    )
    parser.add_argument(
        "--timeout-seconds", type=int, default=300,
        help="maximum processing time per source map (default: 300)",
    )
    args = parser.parse_args(argv)

    try:
        outputs = generate_reports(
            args.texture_processor, args.source_root, args.output_directory,
            args.timeout_seconds,
        )
    except ReportGenerationError as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 2

    for path in outputs:
        print("Wrote {}".format(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
