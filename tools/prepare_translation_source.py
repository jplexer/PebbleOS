# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Merge firmware POT artifacts into a reproducible translation-service input."""

import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

import polib


def prepare(inputs, output):
    inputs = sorted(Path(item).resolve() for item in inputs)
    if not inputs:
        raise ValueError("No firmware POT catalogs supplied")
    plurals = {}
    for path in inputs:
        catalog = polib.pofile(str(path), check_for_duplicates=True)
        if not catalog or any(
            entry.msgstr or any(entry.msgstr_plural.values()) for entry in catalog
        ):
            raise ValueError(
                f"Expected non-empty source strings without translations: {path.name}"
            )
        for entry in catalog:
            key = (entry.msgctxt, entry.msgid)
            if key in plurals and plurals[key] != entry.msgid_plural:
                raise ValueError(
                    f"Conflicting plural forms for source string: {entry.msgid}"
                )
            plurals[key] = entry.msgid_plural
    with tempfile.TemporaryDirectory() as temporary:
        merged = Path(temporary) / "merged.pot"
        subprocess.run(
            [
                "msgcat",
                "--sort-output",
                "--use-first",
                "--output-file",
                str(merged),
                *map(str, inputs),
            ],
            check=True,
        )
        catalog = polib.pofile(str(merged), check_for_duplicates=True)
    # Build timestamps, package versions and input ordering must not cause uploads.
    catalog.metadata = {
        "Project-Id-Version": "PebbleOS",
        "MIME-Version": "1.0",
        "Content-Type": "text/plain; charset=UTF-8",
        "Content-Transfer-Encoding": "8bit",
    }
    catalog.header = "PebbleOS source strings. Generated; do not commit this file."
    for entry in catalog:
        entry.occurrences = sorted(set(entry.occurrences))
        entry.flags = sorted(set(entry.flags))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    catalog.save(str(output))
    return {
        "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "strings": len(catalog),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.inputs, args.output), sort_keys=True))


if __name__ == "__main__":
    main()
