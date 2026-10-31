#!/usr/bin/env python3
"""Consistency check of the translations (run from the repository root: python3 tools/check-lang.py).

Server (server/app/Lang/<code>.php): every language has the same keys and the same {placeholders} per key.
Firmware (sniffer/lang_<code>.h): every language defines the same macros, with the same printf conversions in the same
order (the compiler does not check them), and the texts that are pasted into the HTML or JavaScript of the setup page
(P_*) hold no quote, backslash or angle bracket. Exit code 1 when anything is wrong."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
problems = []


def server():
    langs = {}
    for path in glob.glob(os.path.join(ROOT, "server/app/Lang/*.php")):
        code = os.path.basename(path)[:-4]
        text = open(path, encoding="utf-8").read()
        langs[code] = {k: v for k, v in re.findall(r"^\s*'([^']+)' => '((?:[^'\\]|\\.)*)',\s*$", text, re.M)}
    base = langs.get("en")
    if not base:
        problems.append("server: no English file")
        return
    for code, keys in sorted(langs.items()):
        for k in sorted(set(base) - set(keys)):
            problems.append(f"server/{code}: missing key {k}")
        for k in sorted(set(keys) - set(base)):
            problems.append(f"server/{code}: key {k} does not exist in en")
        for k in set(base) & set(keys):
            if sorted(re.findall(r"\{\w+\}", base[k])) != sorted(re.findall(r"\{\w+\}", keys[k])):
                problems.append(f"server/{code}: placeholders of {k} differ from en")
    print(f"server: {len(langs)} languages, {len(base)} keys")


SPEC = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:ll|l|h)?[diouxXfeEgGcsp%]")
HTML_OK = {"P_STEP_WIFI_A", "P_STEP_WIFI_B"}  # go only into JSON (the progress line), never into the page


def firmware():
    langs = {}
    for path in glob.glob(os.path.join(ROOT, "sniffer/lang_*.h")):
        code = os.path.basename(path)[5:-2]
        if code == "select":
            continue
        defs = {}
        for line in open(path, encoding="utf-8"):
            m = re.match(r'#define (\w+) "((?:[^"\\]|\\.)*)"\s*$', line)
            if m:
                defs[m.group(1)] = m.group(2)
        langs[code] = defs
    base = langs.get("pl") or {}
    for code, defs in sorted(langs.items()):
        for k in sorted(set(base) - set(defs)):
            problems.append(f"firmware/{code}: missing macro {k}")
        for k in sorted(set(defs) - set(base)):
            problems.append(f"firmware/{code}: macro {k} does not exist in pl")
        for k in set(base) & set(defs):
            if SPEC.findall(base[k]) != SPEC.findall(defs[k]):
                problems.append(f"firmware/{code}: printf conversions of {k} differ from pl: {SPEC.findall(base[k])} / {SPEC.findall(defs[k])}")
            if k.startswith("P_") and k not in HTML_OK and re.search(r"['\"\\<>]", defs[k]):
                problems.append(f"firmware/{code}: {k} has a quote, backslash or angle bracket (it is pasted into the setup page)")
    print(f"firmware: {len(langs)} languages, {len(base)} macros")


server()
firmware()
for p in problems:
    print("PROBLEM:", p)
sys.exit(1 if problems else 0)
