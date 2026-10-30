#!/usr/bin/env python3
"""Generates everything needed to order IRL2FA boards into hardware/fab/:

  irl2fa-gerbers.zip          Gerbers + drill files (upload to JLCPCB/PCBWay/...)
  gerbers/                    the same files unzipped (for diffs and review)
  irl2fa-bom-jlcpcb.csv       BOM in JLCPCB's assembly format (LCSC part numbers)
  irl2fa-cpl-jlcpcb.csv       component placement list in JLCPCB's format
  irl2fa-bom.csv              full BOM with manufacturer part numbers
  irl2fa-ibom.html            interactive BOM for hand assembly
  irl2fa-schematic.pdf        schematic
  irl2fa.step                 board 3D model
  irl2fa.glb                  the same as a mesh (case/tests/test_mechanical.py checks it against the case)

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
import math
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


# --------------------------------------------------------------------------
# JLCPCB placement corrections (docs/assembly-review.md, findings A-01..A-03)
#
# JLCPCB does not use our footprints. It places each part from the footprint in
# its own library (the EasyEDA/LCSC one for that LCSC number), turned by the CPL
# rotation and centred on the CPL position. So the CPL has to describe *their*
# footprint, not ours:
#
#   rotation  top:    (kicad + rot) % 360
#             bottom: (180 - kicad + rot) % 360
#   position  our footprint anchor moved by `origin`, the point of our
#             footprint (library frame, mm, y down) where JLCPCB's footprint
#             has its origin.
#
# `rot` is the angle that turns JLCPCB's footprint onto ours (both unrotated, top
# view, counter-clockwise positive). The bottom-side rule is JLCPCB's current
# convention, as used by kicad-jlcpcb-tools (fabrication.py, "bottom angles need
# to be mirrored on Y-axis": 180 - rotation), JLCKicadTools (cpl_fix_rotations.py,
# "(-rotation + 180) % 360", noting JLCPCB changed it in 2020 and twice in 2022)
# and KiBot (rot_footprint mirror_bottom: "180 - component rot + angle"). It is
# what you get by looking at the back of the board (mirrored left-right): KiCad
# flips a back-side footprint top-to-bottom, so a back part at KiCad angle a is
# seen from the back unmirrored at 180 - a. KiCad's own position file writes `a`
# unchanged, which is right for parts whose JLCPCB footprint is symmetric or
# was placed at 90/270 deg, and wrong by 180 deg for everything else.
#
# Evidence: pad centres of each EasyEDA footprint were fetched from
# easyeda.com/api/products/<LCSC>/components on 2026-10-30 and matched to our
# library footprint pad by pad (hardware/tests/jlc_footprints.yaml keeps the
# fetched pads, and hardware/tests/test_assembly.py places them with this CPL
# and checks they land on our pads).
JLC = {
    # LCSC: (rot, origin, evidence)
    "C964845": (0, (0.0, 0.0), "MY-2450-01 clip: EasyEDA BAT-SMD_MY-2450-01, legs (+) at x=+-15.33, "
                "centre pad (-) at the origin, chamfered closed end at +y like ours. "
                "Pads are symmetric: only the body says which way the cell goes in."),
    "C16780": (0, (0.0, 0.0), "0805 capacitor, EasyEDA C0805, pad 1 at -x like KiCad"),
    "C1525": (0, (0.0, 0.0), "0402 capacitor, EasyEDA C0402, pad 1 at -x"),
    "C19666": (0, (0.0, 0.0), "0603 capacitor, EasyEDA C0603, pad 1 at -x"),
    "C52923": (0, (0.0, 0.0), "0402 capacitor, EasyEDA C0402, pad 1 at -x"),
    "C15195": (0, (0.0, 0.0), "0402 capacitor, EasyEDA C0402, pad 1 at -x"),
    "C1779": (0, (0.0, 0.0), "0805 capacitor, EasyEDA C0805, pad 1 at -x"),
    "C15849": (0, (0.0, 0.0), "0603 capacitor, EasyEDA C0603, pad 1 at -x"),
    "C77336": (0, (0.0, 0.0), "MBR0530 SOD-123: EasyEDA SOD-123_L2.8-W1.8-LS3.7-RD, pad 1 = cathode "
               "(symbol pin C) at -x with the cathode bar, as KiCad D_SOD-123"),
    "C6604342": (0, (0.0, 0.0), "VSMB2948SL: NO EasyEDA footprint (API 404). Assumed drawn like "
                 "C511451, the same Vishay side-view package: pad 1 at +x, lens towards +y, as ours. "
                 "Check it in JLCPCB's placement preview (finding A-05)."),
    "C324726": (0, (0.0, 1.25), "FH34SRJ-24S-0.5SH(50): EasyEDA FFC-SMD_24P-P0.50_FH34SRJ-24S-0.5SH, "
                "pad 1 at +x as ours, but its origin is the pad-field centre, midway between the "
                "signal row and the fitting nails: 1.25 mm from our anchor (finding A-02)"),
    "C7427088": (0, (0.0, 0.0), "ANR3015T470M: NO EasyEDA footprint (API 404). Unpolarised 3x3 mm; "
                 "assumed centred with terminals along x like ours"),
    "C15127": (180, (0.0, 0.0), "AO3401A SOT-23: EasyEDA SOT-23_L2.9-W1.3-P1.90-LS2.4-BR has pins 1,2 "
               "at +x and pin 3 at -x; KiCad SOT-23 has pins 1,2 at -x. The well-known SOT-23 180"),
    "C179399": (180, (0.0, 0.0), "NX3008NBK,215 SOT-23: EasyEDA SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR (fetched "
                "2026-10-30), pins 1,2 at +x (1 at -y, y up) and pin 3 at -x; KiCad SOT-23 has pins 1,2 at -x. "
                "The SOT-23 180, as C15127/C20917"),
    "C20917": (180, (0.0, 0.0), "AO3400A SOT-23: EasyEDA SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR, pins 1,2 at +x"),
    "C2843576": (180, (0.0, 0.0), "VEMT2023SLX01: EasyEDA LED-SMD_VISHAY_VEMT2023SLX01 (fetched 2026-10-30) "
                 "is drawn the other way round from the VEMT2523SLX01's: pad 1 (C) at -x (x = -1.6), pad 2 "
                 "at +x, pin-1 dot by pad 1, lens dome (silkscreen arc) at -y (y up). Ours has pad 1 = C "
                 "at +x and the lens at +y (y up): turning theirs 180 lands both pads and the lens"),
    "C25744": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402"),
    "C25900": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402"),
    "C25741": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402"),
    "C26083": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402"),
    "C25101": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402"),
    "C25131": (0, (0.0, 0.0), "0402 resistor, EasyEDA R0402 (fetched 2026-10-30), pad 1 at -x"),
    "C139794": (0, (0.0, 0.0), "SKRHABE010: EasyEDA SW-SMD_SKRHABE010, pads 1-3 at -x (1 at top), 4-6 at "
                "+x (4 at top), small guide boss at the top, as ours"),
    "C22459164": (0, (0.0, 0.0), "STM32U083KCU6 UFQFPN-32: EasyEDA UFQFPN-32_L5.0-W5.0-P0.50-TL-EP3.5, "
                  "pin 1 top of the left column with the dot beside it, as KiCad"),
    "C5127802": (270, (0.0, 0.0), "RV-3032-C7: EasyEDA OSC-SMD_8P-L3.2-W1.5-P0.90-BL is drawn landscape "
                 "with pin 1 bottom-left; KiCad MicroCrystal_C7_SON-8 is portrait with pin 1 top-left: "
                 "turning theirs 270 (90 clockwise) lands every pin on ours"),
}


def jlc_placement(lcsc, x, y, rot, side):
    """Our footprint anchor (CPL frame: mm, y up) and KiCad angle -> JLCPCB's
    (x, y, rotation) for the part."""
    if lcsc not in JLC:
        raise SystemExit(f"fab.py: no JLCPCB placement correction for {lcsc}; "
                         "add it to JLC after checking its EasyEDA footprint")
    corr, (ox, oy), _ = JLC[lcsc]
    if side == "Bottom":
        oy = -oy                      # KiCad flips back-side footprints top to bottom
    a = math.radians(rot)             # KiCad: counter-clockwise on screen, y down
    dx = ox * math.cos(a) + oy * math.sin(a)
    dy = -ox * math.sin(a) + oy * math.cos(a)
    r = (rot + corr) % 360 if side == "Top" else (180 - rot + corr) % 360
    return x + dx, y - dy, round(r, 6) % 360


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
    lcsc_of = {ref.strip(): row[3] for row in jlc for ref in row[1].split(",")}
    placed = []
    for r in csv.DictReader(tmp.open()):
        if r["Ref"] not in lcsc_of:
            continue
        side = "Top" if r["Side"] == "top" else "Bottom"
        x, y, rot = jlc_placement(lcsc_of[r["Ref"]], float(r["PosX"]), float(r["PosY"]),
                                  float(r["Rot"]), side)
        placed.append([r["Ref"], f"{x:.4f}mm", f"{y:.4f}mm", side, f"{rot:.1f}"])
    tmp.unlink()
    write_csv(out / "irl2fa-cpl-jlcpcb.csv", ["Designator", "Mid X", "Mid Y", "Layer", "Rotation"],
              sorted(placed, key=lambda p: (re.sub(r"\d", "", p[0]), int(re.sub(r"\D", "", p[0]) or 0))))

    run("kicad-cli", "sch", "export", "pdf", "-o", str(out / "irl2fa-schematic.pdf"), str(SCH))
    run("kicad-cli", "pcb", "export", "step", "-o", str(out / "irl2fa.step"), "--subst-models",
        "--no-dnp", "--force", str(PCB))
    run("kicad-cli", "pcb", "export", "glb", "-o", str(out / "irl2fa.glb"), "--subst-models",
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
    if path.suffix in (".pdf", ".step", ".glb", ".html"):
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
