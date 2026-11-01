"""Compare matched GPU captures; requires Pillow in analysis Python."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops


def metrics(image, mask=None):
    histogram = image.histogram(mask)
    counts = [sum(histogram[value::256]) for value in range(256)]
    size = sum(counts)
    if not size:
        return {"mean_absolute_error": None, "p99_absolute_error": None, "max_absolute_error": None}
    cumulative = 0
    percentile = None
    for value, count in enumerate(counts):
        cumulative += count
        if percentile is None and cumulative >= size * 0.99:
            percentile = value
    return {"mean_absolute_error": sum(i * count for i, count in enumerate(counts)) / size,
            "p99_absolute_error": percentile,
            "max_absolute_error": max(i for i, count in enumerate(counts) if count)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve(strict=True)
    manifest = json.loads((directory / "complete.json").read_text())
    baseline, override = [Image.open(directory / name).convert("RGB") for name in manifest["captures"]]
    if baseline.size != (1280, 720) or override.size != baseline.size:
        raise RuntimeError("Unexpected capture dimensions")
    difference = ImageChops.difference(override, baseline)
    cells = []
    for row, y in enumerate((125, 360, 595)):
        for column, x in enumerate((405, 640, 875)):
            bounds = (x - 90, y - 90, x + 90, y + 90)
            original = baseline.crop(bounds)
            delta = difference.crop(bounds)
            red, green, blue = original.split()
            occupied = ImageChops.lighter(ImageChops.lighter(red, green), blue).point(lambda x: 255 if x > 8 else 0)
            cells.append({"metallic": manifest["metallic_rows"][row],
                          "roughness": manifest["roughness_columns"][column],
                          "foreground_pixels": occupied.histogram()[255], **metrics(delta, occupied)})
    passed = all(cell["foreground_pixels"] > 500 and cell["mean_absolute_error"] <= 0.5
                 and cell["p99_absolute_error"] <= 2 and cell["max_absolute_error"] <= 10 for cell in cells)
    overall = metrics(difference)
    report = {"passed": passed, "scope": manifest.get("scope", "Matched control/override captures within each rendering path; 8-bit RGB"),
              "whole_image_mean_absolute_error": overall["mean_absolute_error"],
              "whole_image_max_absolute_error": overall["max_absolute_error"], "cells": cells}
    (directory / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
