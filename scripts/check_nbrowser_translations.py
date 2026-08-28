#!/usr/bin/env python3
"""Reports missing/stale translations in NBrowser-owned .grd string bundles.

Scope
-----
This checks *our own* .grd files - the ones NBrowser authors entirely
(currently nbrowser_first_run_strings.grd; more get added as new UI ships,
see nbrowser/overlay/**/*_strings.grd). It's a different problem from
upstream Chromium string re-branding (that's fix_branding_translations.py):
here there is no "orphaned but still-correct" translation to relink,
because we wrote both the English text and (hopefully) its translations
ourselves. The only failure mode is "a translation for the *current*
English wording doesn't exist yet" - either because it was never written,
or because the English text changed since it was.

Both cases need the exact same fix (write/update the translation), so this
tool doesn't try to distinguish "missing" from "stale" - it just reports,
per (message, locale), whether a <translation> matching the message's
*current* grit fingerprint exists. Uses the same fingerprint algorithm as
fix_branding_translations.py (see that file for the algorithm's
provenance/verification).

Usage:
    python check_nbrowser_translations.py [grd_file ...]

With no arguments, checks every path in NBROWSER_GRD_FILES below (relative
to the repo root this script lives in). Exits non-zero if anything is
missing, so it's usable as a pre-commit/CI gate.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fix_branding_translations import (  # noqa: E402
    MEANING_RE,
    MESSAGE_RE,
    TRANSLATIONS_BLOCK_RE,
    XTB_FILE_RE,
    generate_message_id,
    presentable_text,
)

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The project's full locale set (source language 'en' needs no entry).
TARGET_LOCALES = ["ru", "es", "fr", "zh-CN", "de"]

# .grd files NBrowser authors and fully owns, relative to REPO_ROOT.
# Add new entries here as new NBrowser-only UI ships its own string bundle.
NBROWSER_GRD_FILES = [
    "nbrowser/overlay/chrome/browser/ui/webui/nbrowser_first_run/nbrowser_first_run_strings.grd",
    "nbrowser/overlay/chrome/browser/ui/webui/nbrowser_ui_strings/nbrowser_ui_strings.grd",
]

OUTPUT_LANG_RE = re.compile(
    r'<output\s+[^>]*type="data_package"[^>]*\slang="([^"]+)"'
)


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8")


def extract_all_messages(content):
    """Returns {name: (fingerprint_id, presentable_text)} for every message
    in `content`, placeholder-bearing ones included (their id is still
    computable and checkable; this script doesn't need to *edit* text the
    way the branding sweep does, so the <ph> limitation there doesn't
    apply here - <ph> presentation names are literal in the source XML for
    a plain "did we translate the current wording" check as long as no
    substitution is being simulated)."""
    messages = {}
    for m in MESSAGE_RE.finditer(content):
        body = m.group("body")
        text = presentable_text(body)
        if not text:
            continue
        meaning_match = MEANING_RE.search(m.group("attrs"))
        meaning = meaning_match.group(1) if meaning_match else ""
        fp = generate_message_id(text, meaning)
        messages[m.group("name")] = (fp, text)
    return messages


def declared_translation_files(grd_dir, content):
    """Returns {lang: absolute_xtb_path} from this .grd's <translations>
    section (only locales that already have a <file> entry - a locale
    missing here entirely is reported separately as NOT DECLARED)."""
    block_match = TRANSLATIONS_BLOCK_RE.search(content)
    if not block_match:
        return {}
    result = {}
    for file_match in XTB_FILE_RE.finditer(block_match.group(1)):
        rel_path, lang = file_match.group(1), file_match.group(2)
        result[lang] = os.path.normpath(os.path.join(grd_dir, rel_path))
    return result


def declared_output_locales(content):
    """Returns the full set of locales this .grd declares as build outputs
    (<output type="data_package" lang="...">), i.e. every locale the
    browser will actually try to load a translation for."""
    return set(OUTPUT_LANG_RE.findall(content))


def check_file(grd_path):
    """Returns a list of (severity, message) problem strings for one .grd."""
    problems = []
    content = read_text(grd_path)
    grd_dir = os.path.dirname(grd_path)
    messages = extract_all_messages(content)
    if not messages:
        return problems

    xtb_files = declared_translation_files(grd_dir, content)
    output_locales = declared_output_locales(content)

    xtb_cache = {}

    def xtb_text(lang):
        if lang not in xtb_cache:
            path = xtb_files.get(lang)
            xtb_cache[lang] = read_text(path) if path and os.path.isfile(path) else None
        return xtb_cache[lang]

    for lang in TARGET_LOCALES:
        if lang not in xtb_files:
            note = "" if lang in output_locales else " (also not declared as an <output>)"
            problems.append(("ERROR", f"{lang}: no <file> entry in <translations>{note} "
                                       f"- every string in this locale falls back to English."))
            continue
        text = xtb_text(lang)
        if text is None:
            problems.append(("ERROR", f"{lang}: declared translation file is missing on disk "
                                       f"({xtb_files[lang]})."))
            continue
        missing_names = [
            name for name, (fp, _msg_text) in messages.items()
            if f'id="{fp}"' not in text
        ]
        if missing_names:
            problems.append((
                "MISSING",
                f"{lang}: {len(missing_names)}/{len(messages)} message(s) have no "
                f"translation matching their current text: {', '.join(sorted(missing_names))}",
            ))
    return problems


def main():
    targets = sys.argv[1:] or NBROWSER_GRD_FILES
    any_problem = False
    for rel_path in targets:
        grd_path = rel_path if os.path.isabs(rel_path) else os.path.join(REPO_ROOT, rel_path)
        if not os.path.isfile(grd_path):
            print(f"[SKIP] {rel_path}: file not found")
            continue
        problems = check_file(grd_path)
        if not problems:
            print(f"[OK]   {rel_path}: all {len(TARGET_LOCALES)} target locales current")
            continue
        any_problem = True
        print(f"[FAIL] {rel_path}")
        for severity, text in problems:
            print(f"       {severity}: {text}")
    return 1 if any_problem else 0


if __name__ == "__main__":
    sys.exit(main())
