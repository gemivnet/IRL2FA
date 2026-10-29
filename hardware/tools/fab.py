#!/usr/bin/env python3
"""Generates everything needed to order IRL2FA boards into hardware/fab/:

  irl2fa-gerbers.zip          Gerbers + drill files (upload to JLCPCB/PCBWay/...)
  gerbers/                    the same files unzipped (for diffs and review)
  irl2fa-bom-jlcpcb.csv       BOM in JLCPCB's assembly format (LCSC part numbers)
  irl2fa-cpl-jlcpcb.csv       component placement list in JLCPCB's format
  irl2fa-bom.csv              full BOM with manufacturer part numbers
  irl2fa-ibom.html            interactive BOM for hand assembly
  irl2fa-schematic.pdf        schematic
  irl2fa.step                 board 3D model (used by the case fit test)

With --check the outputs are generated into a temporary folder and compared
with the committed ones (ignoring timestamps); a difference fails CI, so the
Gerbers can never silently fall behind the KiCad files.

Run in the hardware CI image (hardware/ci/Dockerfile):
    docker run --rm -v "$PWD":/w -w /w irl2fa-hw python3 hardware/tools/fab.py [--check]

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import argparse
import csv
import io
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

HW = Path(__file__).resolve().parents[1]
SCH = HW / "irl2fa.kicad_sch"
PCB = HW / "irl2fa.kicad_pcb"
FAB = HW / "fab"
LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts"
# Lines that legitimately change on every export.
VOLATILE = re.compile(r"(CreationDate|GenerationSoftware|^; ?DRILL file|^;.*date|Date:|"
                      r"^%TF\.ProjectId|^G04 Created by|^; #@! TF\.CreationDate|/CreationDate|"
                      r"/ModDate|^\s*<dc:date>|\"date\")", re.I)


def run(*args):
    subprocess.run(list(args), check=True, stdout=subprocess.DEVNULL)


def kicad_bom(out_dir):
    """Rows of (refs, value, footprint, lcsc, mpn, mfr) grouped by value+footprint+LCSC."""
    tmp = out_dir / "_bom.csv"
    run("kicad-cli", "sch", "export", "bom", "-o", str(tmp),
        "--fields", "Reference,Value,Footprint,LCSC,MPN,Manufacturer,${QUANTITY},${DNP}",
        "--labels", "Refs,Value,Footprint,LCSC,MPN,Manufacturer,Qty,DNP",
        "--group-by", "Value,Footprint,LCSC", "--sort-field", "Reference",
        "--exclude-dnp", str(SCH))
    rows = list(csv.DictReader(tmp.open()))
    tmp.unlink()
    return rows


def expand_refs(refs):
    """'C12-C19, R1' -> 'C12,C13,...,C19,R1' (JLCPCB wants every designator)."""
    out = []
    for part in refs.replace(" ", "").split(","):
        m = re.fullmatch(r"([A-Za-z]+)(\d+)-([A-Za-z]+)?(\d+)", part)
        if m:
            out += [f"{m.group(1)}{i}" for i in range(int(m.group(2)), int(m.group(4)) + 1)]
        elif part:
            out.append(part)
    return ",".join(out)


def write_csv(path, header, rows):
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(header)
    w.writerows(rows)
    path.write_text(buf.getvalue())


def deterministic_zip(zpath, files):
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(files):
            info = zipfile.ZipInfo(f.name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, f.read_bytes())


def generate(out):
    out.mkdir(parents=True, exist_ok=True)
    gdir = out / "gerbers"
    if gdir.exists():
        shutil.rmtree(gdir)
    gdir.mkdir()
    run("kicad-cli", "pcb", "export", "gerbers", "-o", str(gdir) + "/", "--layers", LAYERS,
        "--subtract-soldermask", "--use-drill-file-origin", "--no-protel-ext", str(PCB))
    run("kicad-cli", "pcb", "export", "drill", "-o", str(gdir) + "/", "--format", "excellon",
        "--drill-origin", "plot", "--excellon-separate-th", "--generate-map",
        "--map-format", "gerberx2", str(PCB))
    deterministic_zip(out / "irl2fa-gerbers.zip", [f for f in gdir.iterdir() if f.is_file()])

    # BOMs
    rows = kicad_bom(out)
    jlc, full = [], []
    for r in rows:
        if not r["LCSC"]:
            continue
        fp = r["Footprint"].split(":")[-1]
        refs = expand_refs(r["Refs"])
        jlc.append([r["Value"], refs, fp, r["LCSC"]])
        full.append([r["Qty"], refs, r["Value"], r["Manufacturer"], r["MPN"], r["LCSC"], fp])
    write_csv(out / "irl2fa-bom-jlcpcb.csv", ["Comment", "Designator", "Footprint", "LCSC Part #"], jlc)
    write_csv(out / "irl2fa-bom.csv",
              ["Quantity", "References", "Description", "Manufacturer", "MPN", "LCSC", "Footprint"], full)

    # Placement (JLCPCB format). Board-only and excluded parts are skipped by kicad-cli.
    tmp = out / "_pos.csv"
    run("kicad-cli", "pcb", "export", "pos", "-o", str(tmp), "--format", "csv", "--units", "mm",
        "--side", "both", "--use-drill-file-origin", "--exclude-dnp", str(PCB))
    placed = []
    for r in csv.DictReader(tmp.open()):
        placed.append([r["Ref"], f"{float(r['PosX']):.4f}mm", f"{float(r['PosY']):.4f}mm",
                       "Top" if r["Side"] == "top" else "Bottom", f"{float(r['Rot']):.1f}"])
    tmp.unlink()
    in_bom = {ref.strip() for row in jlc for ref in row[1].split(",")}
    placed = [p for p in placed if p[0] in in_bom]
    write_csv(out / "irl2fa-cpl-jlcpcb.csv", ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"],
              sorted(placed, key=lambda p: (re.sub(r"\d", "", p[0]), int(re.sub(r"\D", "", p[0]) or 0))))

    run("kicad-cli", "sch", "export", "pdf", "-o", str(out / "irl2fa-schematic.pdf"), str(SCH))
    run("kicad-cli", "pcb", "export", "step", "-o", str(out / "irl2fa.step"), "--subst-models",
        "--no-dnp", "--force", str(PCB))

    # Interactive BOM (MIT, github.com/openscopeproject/InteractiveHtmlBom)
    ib = shutil.which("generate_interactive_bom")
    if ib:
        subprocess.run([ib, "--no-browser", "--dest-dir", str(out), "--name-format", "irl2fa-ibom",
                        "--extra-fields", "LCSC,MPN", "--include-tracks", "--include-nets",
                        "--dark-mode", str(PCB)], check=True, capture_output=True,
                       env={"INTERACTIVE_HTML_BOM_NO_DISPLAY": "1", "PATH": "/usr/bin:/bin:/usr/local/bin",
                            "HOME": "/tmp"})


def normalise(path):
    data = path.read_bytes()
    if path.suffix in (".zip",):
        with zipfile.ZipFile(path) as z:
            return sorted((n, normalise_text(z.read(n))) for n in z.namelist())
    if path.suffix in (".pdf", ".step", ".html"):
        return None  # regenerated for releases, not compared byte-for-byte
    return normalise_text(data)


def normalise_text(data):
    try:
        text = data.decode()
    except UnicodeDecodeError:
        return data
    return "\n".join(l for l in text.splitlines() if not VOLATILE.search(l))


def check():
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp)
        generate(out)
        stale = []
        for f in sorted(p for p in out.rglob("*") if p.is_file()):
            rel = f.relative_to(out)
            committed = FAB / rel
            if not committed.exists():
                stale.append(f"missing: hardware/fab/{rel}")
                continue
            a, b = normalise(f), normalise(committed)
            if a is not None and a != b:
                stale.append(f"stale: hardware/fab/{rel}")
        if stale:
            print("Fabrication outputs do not match the KiCad files:\n  " + "\n  ".join(stale))
            print("Regenerate with: python3 hardware/tools/fab.py (in the irl2fa-hw image) and commit.")
            return 1
    print("hardware/fab is up to date")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--out", type=Path, default=FAB)
    a = ap.parse_args()
    if a.check:
        sys.exit(check())
    generate(a.out)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
