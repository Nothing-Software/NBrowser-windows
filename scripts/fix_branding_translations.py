#!/usr/bin/env python3
"""Applies NBrowser's brand-name text substitution to .grd/.grdp/.xtb string
resource files WITHOUT orphaning existing .xtb translations.

Background
----------
apply_branding_assets.ps1 renames the product across every .grd/.grdp/.xtb
file with a blind, case-sensitive text substitution ("Chromium" ->
"NBrowser", "The Chromium Authors" -> the company name, "ungoogled-chromium"
-> "NBrowser by <company>"). That touches the English source text in .grd
files AND the already-translated text in .xtb files - both correctly end up
saying "NBrowser".

The problem: grit looks up each locale's <translation id="..."> by an MD5
fingerprint of the CURRENT English message text (see
tools/grit/grit/extern/{FP,tclib}.py), not by the message's symbolic
IDS_* name, and the substitution never touches that numeric id. The moment
a message's English text changes (e.g. "Reset to Default Chromium" ->
"Reset to Default NBrowser"), grit computes a brand-new fingerprint that no
longer matches the translation's old id - so the (also correctly
re-branded) existing translation becomes invisible, and grit silently
falls back to displaying the English text for every locale.

What this script does instead: for every <message> in every swept .grd/
.grdp file, it computes the message's real grit translation fingerprint for
the PRISTINE upstream text (read via `git show HEAD:<path>` - build/src is
a real Chromium git checkout that this project never commits its own
changes into, so HEAD always has the genuine un-rebranded source,
regardless of how many times branding has already been applied to the
on-disk working tree) and for that same text after the substitution.
Whenever those two fingerprints differ, every locale's
<translation id="OLD"> declared in that .grd's own <translations> section
is renamed to id="NEW"> - so the existing, already-correctly-substituted
translation stays linked to its message instead of being silently
orphaned. Anchoring to git HEAD rather than "whatever the file said before
this specific run" makes this retroactive: it fixes files branding already
ran on in the past just as well as files seeing it for the first time.
Fully automatic and re-derived from the current source tree on every run,
so it keeps working across upstream rebases without any hand-maintained
list of string ids.

Limitation: messages containing <ph> placeholders are substituted the same
as before (their *text* still gets branded) but are NOT fingerprint-remapped
by this script, because grit folds each placeholder into its presentation
name (not its literal text) before fingerprinting, and correctly
replicating that per-placeholder derivation is nontrivial. In practice the
strings that actually carry the brand name in a *placeholder-visible* way
("Reset to Default $BRAND", "Customize $BRAND", ...) are plain text, so
this covers what matters; placeholder-bearing messages just keep falling
back to English exactly as they did before this script existed (not a
regression - simply not (yet) fixed). Likewise, a .grd file with no git
history at all (a brand-new file a patch adds, e.g.
nbrowser_first_run_strings.grd) has no pristine baseline to anchor to, so
it just gets the plain text substitution with no remap attempt - correct,
since a file that never shipped upstream never had orphanable translations
to begin with.

Usage:
    python fix_branding_translations.py <build/src root>

Called from apply_branding_assets.ps1 in place of a blind PowerShell sweep.
"""

import hashlib
import os
import re
import subprocess
import sys
import xml.sax.saxutils as saxutils

BRAND_NAME = "NBrowser"
COMPANY_NAME = "Nothing Software"

STRING_ROOTS = ["chrome", "components", "extensions", "ui", "content"]

# Applied in this exact order, case-sensitively - mirrors
# apply_branding_assets.ps1's -creplace chain 1:1.
SUBSTITUTIONS = [
    ("The Chromium Authors", COMPANY_NAME),
    ("Chromium", BRAND_NAME),
    ("ungoogled-chromium", f"{BRAND_NAME} by {COMPANY_NAME}"),
]

MESSAGE_RE = re.compile(
    r'<message\s+name="(?P<name>IDS_[A-Za-z0-9_]+)"(?P<attrs>[^>]*)>'
    r'(?P<body>.*?)</message>',
    re.DOTALL,
)
MEANING_RE = re.compile(r'\bmeaning="([^"]*)"')
TRANSLATIONS_BLOCK_RE = re.compile(
    r'<translations>(.*?)</translations>', re.DOTALL
)
XTB_FILE_RE = re.compile(
    r'<file\s+path="([^"]+\.xtb)"\s+lang="([^"]+)"\s*/>'
)


def substitute(text):
    for old, new in SUBSTITUTIONS:
        text = text.replace(old, new)
    return text


def needs_substitution(text):
    return "chromium" in text.lower()


def unsigned_fingerprint(text):
    digest = hashlib.md5(text.encode("utf-8")).hexdigest()
    return int(digest[:16], 16)


def fingerprint(text):
    fp = unsigned_fingerprint(text)
    if fp & 0x8000000000000000:
        fp = -((~fp & 0xFFFFFFFFFFFFFFFF) + 1)
    return fp


def generate_message_id(message, meaning=""):
    fp = fingerprint(message)
    if meaning:
        fp2 = fingerprint(meaning)
        if fp < 0:
            fp = fp2 + (fp << 1) + 1
        else:
            fp = fp2 + (fp << 1)
    return str(fp & 0x7FFFFFFFFFFFFFFF)


def presentable_text(raw_body):
    """Decodes XML entities and trims leading/trailing whitespace, matching
    grit's own message.py _WHITESPACE regex (re.DOTALL means the trim can
    span newlines; internal whitespace/indentation is left untouched)."""
    return saxutils.unescape(raw_body).strip()


def extract_messages(content):
    """Returns {name: (old_text, meaning)} for every plain-text (no <ph>)
    message in `content` whose body actually needs the brand substitution."""
    messages = {}
    for m in MESSAGE_RE.finditer(content):
        body = m.group("body")
        if "<ph" in body:
            continue
        text = presentable_text(body)
        if not text or not needs_substitution(text):
            continue
        meaning_match = MEANING_RE.search(m.group("attrs"))
        meaning = meaning_match.group(1) if meaning_match else ""
        if meaning and needs_substitution(meaning):
            meaning = substitute(meaning)
        messages[m.group("name")] = (text, meaning)
    return messages


def git_show_head(repo_root, rel_path):
    """Returns the git-HEAD (pristine, pre-branding) content of a tracked
    file, or None if it isn't tracked (a new file a patch adds) or git
    fails for any other reason."""
    try:
        result = subprocess.run(
            ["git", "show", f"HEAD:{rel_path.replace(os.sep, '/')}"],
            cwd=repo_root,
            capture_output=True,
            check=False,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    try:
        return result.stdout.decode("utf-8")
    except UnicodeDecodeError:
        return None


def compute_remaps_for_grd(repo_root, grd_path, current_content):
    """Returns a list of (old_id, new_id) for every plain-text <message>
    whose fingerprint differs between the pristine git-HEAD text and that
    same text after the brand substitution. Anchoring to HEAD (rather than
    diffing this run's before/after) makes this correct even when branding
    already ran on this file in an earlier pass."""
    rel_path = os.path.relpath(grd_path, repo_root)
    pristine_content = git_show_head(repo_root, rel_path)
    if pristine_content is None:
        return []
    pristine_messages = extract_messages(pristine_content)
    if not pristine_messages:
        return []
    remaps = []
    for name, (old_text, meaning) in pristine_messages.items():
        new_text = substitute(old_text)
        if new_text == old_text:
            continue
        old_id = generate_message_id(old_text, meaning)
        new_id = generate_message_id(new_text, meaning)
        if old_id != new_id:
            remaps.append((old_id, new_id))
    return remaps


def collect_locale_files(repo_root, grd_path, current_content):
    """Returns {absolute_xtb_path: [(old_id, new_id), ...]} declared by this
    .grd's own <translations> section, paired with the remaps computed
    against its pristine git-HEAD messages."""
    block_match = TRANSLATIONS_BLOCK_RE.search(current_content)
    if not block_match:
        return {}
    remaps = compute_remaps_for_grd(repo_root, grd_path, current_content)
    if not remaps:
        return {}
    grd_dir = os.path.dirname(grd_path)
    result = {}
    for file_match in XTB_FILE_RE.finditer(block_match.group(1)):
        rel_path = file_match.group(1)
        xtb_path = os.path.normpath(os.path.join(grd_dir, rel_path))
        result.setdefault(xtb_path, []).extend(remaps)
    return result


def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    # These files are UTF-8 with mixed LF/CRLF; decode losslessly and leave
    # line endings untouched everywhere in this script.
    return raw.decode("utf-8")


def write_text(path, text):
    with open(path, "wb") as f:
        f.write(text.encode("utf-8"))


def apply_id_remaps(content, remaps):
    for old_id, new_id in remaps:
        if old_id == new_id:
            continue
        old_attr = f'id="{old_id}"'
        new_attr = f'id="{new_id}"'
        if old_attr in content and new_attr not in content:
            content = content.replace(old_attr, new_attr, 1)
    return content


def main():
    if len(sys.argv) != 2:
        print("usage: fix_branding_translations.py <build/src root>", file=sys.stderr)
        return 1
    root = os.path.abspath(sys.argv[1])

    xtb_remaps = {}  # absolute xtb path -> [(old_id, new_id), ...]
    swept_files = 0

    # Pass 1: .grd/.grdp files. Substitute + collect id remaps (anchored to
    # git HEAD) for their declared locale .xtb files.
    for root_name in STRING_ROOTS:
        root_path = os.path.join(root, root_name)
        if not os.path.isdir(root_path):
            continue
        for dirpath, _dirnames, filenames in os.walk(root_path):
            for filename in filenames:
                if not filename.endswith((".grd", ".grdp")):
                    continue
                path = os.path.join(dirpath, filename)
                content = read_text(path)
                # Consider files that either still need branding, or already
                # carry it (a prior run may have already substituted this
                # exact file, and we still want to compute remaps for it).
                if not (needs_substitution(content) or BRAND_NAME in content):
                    continue
                for xtb_path, remaps in collect_locale_files(root, path, content).items():
                    xtb_remaps.setdefault(xtb_path, []).extend(remaps)
                new_content = substitute(content)
                if new_content != content:
                    write_text(path, new_content)
                    swept_files += 1

    # Pass 2: every .xtb file (same scope apply_branding_assets.ps1 always
    # swept). Substitute their text, then apply any id remaps collected
    # above for files reachable from a .grd we just processed.
    for root_name in STRING_ROOTS:
        root_path = os.path.join(root, root_name)
        if not os.path.isdir(root_path):
            continue
        for dirpath, _dirnames, filenames in os.walk(root_path):
            for filename in filenames:
                if not filename.endswith(".xtb"):
                    continue
                path = os.path.join(dirpath, filename)
                content = read_text(path)
                changed = False
                if needs_substitution(content):
                    new_content = substitute(content)
                    if new_content != content:
                        content = new_content
                        changed = True
                remaps = xtb_remaps.get(os.path.normpath(path))
                if remaps:
                    new_content = apply_id_remaps(content, remaps)
                    if new_content != content:
                        content = new_content
                        changed = True
                if changed:
                    write_text(path, content)
                    swept_files += 1

    print(f"Renamed product in {swept_files} string files (.grd/.grdp/.xtb), "
          f"re-linked translations for {len(xtb_remaps)} locale files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
