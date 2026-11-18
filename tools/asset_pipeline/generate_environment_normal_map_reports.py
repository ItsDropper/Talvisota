#!/usr/bin/env python3
"""Generate reproducible spectral reports from the shipped processed normal maps.

The engine texture processor is deliberately invoked as a child process: this
checks the same executable and command-line contract used by the Windows build.
The resulting TVTTEX01 files are analyzed before temporary files are discarded.
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
    if not output.is_file():
        raise ReportGenerationError(
            "Texture processor returned success but did not create '{}'.".format(output)
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


def _validate_report(
    report: Dict[str, object], label: str, source_frequency: float
) -> None:
    if report.get("container") != "TVTTEX01":
        raise ReportGenerationError("{} report is not a TVTTEX01 analysis.".format(label))
    if report.get("pixel_format") != "RGBA8" or report.get("color_space") != "linear":
        raise ReportGenerationError(
            "{} report does not describe a linear RGBA8 normal texture.".format(label)
        )

    width = report.get("width")
    height = report.get("height")
    mips = report.get("mips")
    if (
        not isinstance(width, int)
        or not isinstance(height, int)
        or width != height
        or width < 16
        or width & (width - 1)
        or not isinstance(mips, list)
        or report.get("mip_count") != len(mips)
        or not mips
    ):
        raise ReportGenerationError("{} report has invalid base dimensions or mip inventory.".format(label))

    expected_width = width
    expected_height = height
    for level, mip in enumerate(mips):
        if (
            mip.get("level") != level
            or mip.get("width") != expected_width
            or mip.get("height") != expected_height
        ):
            raise ReportGenerationError(
                "{} report has an invalid dimension at mip {}.".format(label, level)
            )

        normal = mip.get("normal_vector_length")
        if not isinstance(normal, dict):
            raise ReportGenerationError(
                "{} report is missing normal-vector metrics at mip {}.".format(label, level)
            )
        for key in ("mean", "minimum", "maximum", "rms_deviation_from_unit"):
            value = normal.get(key)
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ReportGenerationError(
                    "{} report has an invalid {} metric at mip {}.".format(label, key, level)
                )
        outside = normal.get("samples_outside_2_percent")
        if not isinstance(outside, int) or not 0 <= outside <= expected_width * expected_height:
            raise ReportGenerationError(
                "{} report has an invalid normal-vector tolerance count at mip {}.".format(
                    label, level
                )
            )

        estimates = mip.get("source_frequency_alias_estimates")
        if (
            not isinstance(estimates, list)
            or len(estimates) != 1
            or estimates[0].get("label") != label
            or estimates[0].get("source_cycles_per_uv") != source_frequency
        ):
            raise ReportGenerationError(
                "{} report is missing its expected source-frequency estimate at mip {}.".format(
                    label, level
                )
            )

        expected_width = max(1, (expected_width + 1) // 2)
        expected_height = max(1, (expected_height + 1) // 2)

    if (mips[-1].get("width"), mips[-1].get("height")) != (1, 1):
        raise ReportGenerationError("{} report does not contain a complete mip chain.".format(label))


def _json_bytes(value: object) -> bytes:
    try:
        return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ReportGenerationError("Could not serialize deterministic report JSON: {}".format(exc)) from exc


def generate_reports(
    texture_processor: Path,
    source_root: Path,
    output_directory: Path,
    timeout_seconds: int = 300,
) -> List[Path]:
    if timeout_seconds <= 0:
        raise ReportGenerationError("Processor timeout must be a positive number of seconds.")

    try:
        processor = Path(texture_processor).resolve(strict=True)
        sources = [
            (label, source_root_path, frequency)
            for label, filename, frequency in _ENVIRONMENT_MAPS
            for source_root_path in (Path(source_root) / filename,)
        ]
        source_paths = [(label, source.resolve(strict=True), frequency)
                        for label, source, frequency in sources]
        output_dir = Path(output_directory).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
    except (OSError, RuntimeError) as exc:
        raise ReportGenerationError("Could not resolve report inputs or output directory: {}".format(exc)) from exc

    if not processor.is_file():
        raise ReportGenerationError("Texture processor is not a file: {}".format(processor))
    for label, source, _ in source_paths:
        if not source.is_file():
            raise ReportGenerationError(
                "Required {} source normal map is not a file: {}".format(label, source)
            )

    manifest_path = output_dir / "environment-normal-map-spectrum-manifest.json"
    report_names = {
        label: "{}-normal-spectrum.json".format(label)
        for label, _, _ in _ENVIRONMENT_MAPS
    }
    protected_paths = {processor, *(source for _, source, _ in source_paths)}
    final_paths = {manifest_path, *(output_dir / name for name in report_names.values())}
    if protected_paths & {path.resolve() for path in final_paths}:
        raise ReportGenerationError("A report output path would overwrite a source or processor input.")

    # A missing manifest means the two reports do not represent one complete run.
    # Remove it before work starts; publish it only after both reports are ready.
    try:
        manifest_path.unlink(missing_ok=True)
        processor_hash = _sha256(processor)
        with tempfile.TemporaryDirectory(
            prefix=".environment-normal-map-spectrum-", dir=str(output_dir)
        ) as temporary:
            staging = Path(temporary)
            report_records: List[Dict[str, object]] = []
            staged_reports: List[Tuple[Path, Path]] = []

            for label, source, frequency in source_paths:
                processed = staging / "{}-normal.ttex".format(label)
                _run_texture_processor(processor, source, processed, timeout_seconds)

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
                final_report = output_dir / report_names[label]
                staged_reports.append((staged_report, final_report))
                report_records.append({
                    "label": label,
                    "source_file": source.name,
                    "source_sha256": _sha256(source),
                    "processed_texture_sha256": _sha256(processed),
                    "report_file": final_report.name,
                    "report_sha256": hashlib.sha256(report_data).hexdigest(),
                    "width": report["width"],
                    "height": report["height"],
                    "mip_count": report["mip_count"],
                    "source_cycles_per_uv": frequency,
                })

            manifest = {
                "schema_version": 1,
                "processor_file": processor.name,
                "processor_sha256": processor_hash,
                "reports": report_records,
            }
            staged_manifest = staging / manifest_path.name
            staged_manifest.write_bytes(_json_bytes(manifest))

            for staged_report, final_report in staged_reports:
                os.replace(str(staged_report), str(final_report))
            os.replace(str(staged_manifest), str(manifest_path))
    except ReportGenerationError:
        raise
    except (OSError, RuntimeError) as exc:
        raise ReportGenerationError("Could not publish normal-map reports: {}".format(exc)) from exc

    return [output_dir / report_names[label] for label, _, _ in source_paths] + [manifest_path]


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
        help="directory for the two JSON reports and their provenance manifest",
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
