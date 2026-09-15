# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Catalog integrity and reproducibility checks using GNU gettext."""

import tempfile
import unittest
from pathlib import Path

import polib
from prepare_translation_source import prepare


class SourceTests(unittest.TestCase):
    def test_union_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            first = polib.POFile()
            first.metadata = {"POT-Creation-Date": "2026-01-01 00:00+0000"}
            first.append(
                polib.POEntry(
                    msgid="%d item",
                    msgid_plural="%d items",
                    msgstr_plural={0: "", 1: ""},
                    flags=["c-format"],
                    occurrences=[("src/a.c", "1")],
                    comment="Keep the count",
                )
            )
            first.append(polib.POEntry(msgid="Open", msgctxt="verb"))
            second = polib.POFile()
            second.append(polib.POEntry(msgid="Only on another board"))
            second.append(polib.POEntry(msgid="Open", msgctxt="adjective"))
            a, b, out = root / "a.pot", root / "b.pot", root / "out.pot"
            first.save(str(a))
            second.save(str(b))
            result = prepare([a, b], out)
            before = out.read_bytes()
            first.metadata["POT-Creation-Date"] = "2026-09-15 00:00+0000"
            first.save(str(a))
            self.assertEqual(result, prepare([b, a], out))
            self.assertEqual(before, out.read_bytes())
            merged = polib.pofile(str(out))
            self.assertEqual(len(merged), 4)
            count = merged.find("%d item")
            self.assertEqual(count.msgid_plural, "%d items")
            self.assertIn("c-format", count.flags)
            self.assertIn(("src/a.c", "1"), count.occurrences)
            self.assertEqual(count.comment, "Keep the count")

    def test_reject_translations_empty_and_conflicting_plurals(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            a, b, out = root / "a.pot", root / "b.pot", root / "out.pot"
            source = polib.POFile()
            source.append(polib.POEntry(msgid="Item", msgstr="Translated"))
            source.save(str(a))
            with self.assertRaises(ValueError):
                prepare([a], out)
            with self.assertRaises(ValueError):
                prepare([], out)
            source[0].msgstr = ""
            source.save(str(a))
            source[0].msgid_plural = "Items"
            source[0].msgstr_plural = {0: "", 1: ""}
            source.save(str(b))
            with self.assertRaises(ValueError):
                prepare([a, b], out)


if __name__ == "__main__":
    unittest.main()
