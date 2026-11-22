#!/usr/bin/env python3
"""The interface texts (backend/i18n/messages*.properties): the two languages aligned, every text valid for
MessageFormat, no apostrophe lost, every key used by the code present and every key used somewhere. The code is the
backend (I18n::t("key", ...)) and the app (T("key", ...)).

    tests/texts_test.py
"""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXTS = os.path.join(ROOT, "backend", "i18n")


def read(name):
    """A .properties file: key=value lines, # comments, \\ escapes and \\uXXXX."""
    out = {}
    with open(os.path.join(TEXTS, name), encoding="utf-8") as f:
        raw = f.read().split("\n")
    i = 0
    while i < len(raw):
        line = raw[i].lstrip()
        i += 1
        if not line or line[0] in "#!":
            continue
        # A line ending with an odd number of backslashes goes on.
        while (len(line) - len(line.rstrip("\\"))) % 2 == 1 and i < len(raw):
            line = line[:-1] + raw[i].lstrip()
            i += 1
        m = re.match(r"((?:\\.|[^=:\s])*)\s*[=:\s]\s*(.*)$", line)
        key, value = (m.group(1), m.group(2)) if m else (line, "")
        out[unescape(key)] = unescape(value)
    return out


def unescape(s):
    def one(m):
        c = m.group(1)
        if c.startswith("u"):
            return chr(int(c[1:], 16))
        return {"n": "\n", "t": "\t", "r": "\r", "f": "\f"}.get(c, c)
    return re.sub(r"\\(u[0-9a-fA-F]{4}|.)", one, s)


def sources():
    files = []
    for pattern in ("backend/**/*.cpp", "backend/**/*.h", "src/**/*.cpp", "src/**/*.h"):
        files += glob.glob(os.path.join(ROOT, pattern), recursive=True)
    return {f: open(f, encoding="utf-8").read() for f in files}


failures = []


def check(cond, what):
    if not cond:
        failures.append(what)


def main():
    en, it = read("messages.properties"), read("messages_it.properties")
    check(set(en) == set(it), "keys only in one language: %s" % sorted(set(en) ^ set(it)))
    holes = lambda t: set(re.findall(r"\{(\d+)", t))
    for k in en:
        if k in it:
            check(holes(en[k]) == holes(it[k]), "placeholders of " + k)

    for name, texts in (("messages.properties", en), ("messages_it.properties", it)):
        lost = re.compile(r"\b(nell|dell|all|dall|sull|quest)(?![aeio]\b)[aeiouàèéìòù]\w*" if "_it" in name else
                          r"\b(cant|dont|doesnt|isnt|wont|hasnt|arent|didnt|couldnt|youre|theyre)\b", re.IGNORECASE)
        for k, v in texts.items():
            # A single apostrophe opens a quotation for MessageFormat and makes the rest of the text disappear.
            check(not re.search(r"(?<!')'(?!')", v), "%s %s: single apostrophe in %r" % (name, k, v))
            check(v.count("{") == v.count("}"), "%s %s: unbalanced braces" % (name, k))
            check(not lost.search(v.replace("''", "'")), "%s %s: missing apostrophe? %r" % (name, k, v))

    code = sources()
    used = set()
    usage = re.compile(r"(?:I18n::t|I18n::format|\bT)\(\s*\"([^\"]+)\"\s*[,)]")
    literal = re.compile(r"\"([a-z][a-zA-Z]*(?:\.[a-zA-Z]+)+)\"")
    joined = re.compile(r"\"([a-z][a-zA-Z]*(?:\.[a-zA-Z]+)*\.)\"\s*\+")
    strings, prefixes = set(), set()
    for text in code.values():
        used |= set(usage.findall(text))
        strings |= set(literal.findall(text))
        prefixes |= {p for p in joined.findall(text) if p != "security."}
    missing = sorted(used - set(en))
    check(not missing, "keys used in the code but missing: %s" % missing)
    useless = set(en) - used - strings
    # The texts of the security checks: "security." + id and "security." + id + ".advice", the ids in the code.
    useless = {k for k in useless if not (k.startswith("security.") and re.sub(r"\.advice$", "", k[9:]) in strings)}
    # Keys put together in the code ("ghost.dock." + side): the whole family counts as used.
    useless = {k for k in useless if not any(k.startswith(p) for p in prefixes)}
    check(not useless, "keys never used: %s" % sorted(useless))

    if failures:
        for f in failures:
            print("FAILED: " + f)
        sys.exit(1)
    print("texts test passed: %d keys, %d used in the code" % (len(en), len(used)))


if __name__ == "__main__":
    main()
