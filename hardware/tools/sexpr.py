"""Minimal S-expression reader/writer for KiCad files.

Atoms are kept as Python strings; quoted strings are wrapped in `Q` so they
round-trip with their quotes. Lists are Python lists.

SPDX-License-Identifier: CERN-OHL-S-2.0
"""


class Q(str):
    """A quoted string atom."""


def parse(text):
    pos = 0
    n = len(text)

    def skip_ws():
        nonlocal pos
        while pos < n and text[pos] in " \t\r\n":
            pos += 1

    def read():
        nonlocal pos
        skip_ws()
        c = text[pos]
        if c == "(":
            pos += 1
            out = []
            while True:
                skip_ws()
                if text[pos] == ")":
                    pos += 1
                    return out
                out.append(read())
        if c == '"':
            pos += 1
            buf = []
            while text[pos] != '"':
                if text[pos] == "\\":
                    pos += 1
                    esc = text[pos]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(esc, esc))
                else:
                    buf.append(text[pos])
                pos += 1
            pos += 1
            return Q("".join(buf))
        start = pos
        while pos < n and text[pos] not in ' \t\r\n()"':
            pos += 1
        return text[start:pos]

    return read()


def quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def dump(node, indent=0):
    """Serialises with KiCad-like formatting (one list per line when nested)."""
    if isinstance(node, Q):
        return quote(node)
    if not isinstance(node, list):
        return str(node)
    if not any(isinstance(x, list) for x in node):
        return "(" + " ".join(dump(x) for x in node) + ")"
    pad = "\t" * (indent + 1)
    parts = ["(" + " ".join(dump(x) for x in node if not isinstance(x, list))]
    first_list = True
    for x in node:
        if isinstance(x, list):
            parts.append("\n" + pad + dump(x, indent + 1))
            first_list = False
    return "".join(parts) + "\n" + "\t" * indent + ")"


def find(node, key):
    """First child list whose head is `key`."""
    for x in node:
        if isinstance(x, list) and x and x[0] == key:
            return x
    return None


def find_all(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]
