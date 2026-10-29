#!/usr/bin/env python3
"""Battery-life budget: datasheet currents (hardware/power/model.json) times
the time each part is on, as measured by running the real firmware in Renode
(hardware/power/measured.json, from firmware/renode/power.robot).

    python3 hardware/power/power.py                      # summary
    python3 hardware/power/power.py --markdown           # docs/power-calculations.md
    python3 hardware/power/power.py --svg                # docs/images/power/use-timeline.svg
    python3 hardware/power/power.py --sim-js             # sim/web/power-model.js
    python3 hardware/power/power.py --update             # rewrite all three
    python3 hardware/power/power.py --measured firmware/build/renode --check
        # CI: a fresh emulator run must agree with measured.json and still
        # give >= the required battery life in the worst case
    python3 hardware/power/power.py --measured firmware/build/renode --write
        # accept a fresh run as the new measured.json

Standard library only. SPDX-License-Identifier: CERN-OHL-S-2.0
"""
import argparse
import json
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MODEL = json.loads((HERE / "model.json").read_text())
MEASURED_PATH = HERE / MODEL["measured"]
DOC = ROOT / "docs" / "power-calculations.md"
SVG = ROOT / "docs" / "images" / "power" / "use-timeline.svg"
SIM_JS = ROOT / "sim" / "web" / "power-model.js"
H_PER_YEAR = 24 * 365
CASES = ("typ", "worst")
SESSIONS = {"use": "power-use.json", "pairing": "power-pairing.json"}

# How far a fresh emulator run may drift from measured.json before CI fails.
# Counts must match exactly (the run is deterministic); times and charge get
# a little room for Renode's quantum-level timing.
TOLERANCE = {
    "rel": 0.05,
    "exact": ("full_refreshes", "partial_refreshes", "stop2_entries", "refresh_seq"),
}


# ---------------------------------------------------------------- measuring

def summarize(raw):
    """One Renode power-*.json (models/PowerMonitor.cs) -> the per-session
    quantities the budget needs, in seconds."""
    epd, ir, i2c, mcu = MODEL["epd"], MODEL["ir"], MODEL["i2c"], MODEL["mcu"]
    busy = raw["epd_busy_ms"]
    count = raw["epd_busy_count"]
    duration_s = raw["duration_ms"] / 1000
    awake_s = duration_s - raw["stop2_ms"] / 1000
    run_s = raw["instructions"] / (raw["mips"] * 1e6)
    busy_full_s = busy.get("full", 0) / 1000
    busy_partial_s = busy.get("partial", 0) / 1000
    epd_powered_s = raw["epd_powered_ms"] / 1000
    marks = [[round(e[0] / 1000, 3), e[2]] for e in raw["events"] if e[1] == "mark"]
    return {
        "awake_s": round(awake_s, 4),
        "instructions": raw["instructions"],
        "mcu_run_s": round(run_s, 4),
        "mcu_sleep_s": round(awake_s - run_s, 4),
        "stop2_entries": raw["stop2_entries"],
        "spi_bytes": raw["spi_bytes"],
        "spi_s": round(raw["spi_bytes"] * 8 / epd["spi_hz"], 4),
        "full_refreshes": count.get("full", 0),
        "partial_refreshes": count.get("partial", 0),
        "epd_busy_full_s": round(busy_full_s, 4),
        "epd_busy_partial_s": round(busy_partial_s, 4),
        "epd_powered_s": round(epd_powered_s, 4),
        "epd_logic_s": round(max(0.0, epd_powered_s - busy_full_s - busy_partial_s), 4),
        "i2c_transactions": raw["i2c_reads"] + raw["i2c_writes"],
        "i2c_bytes": raw["i2c_bytes"],
        "i2c_s": round(raw["i2c_bytes"] * 9 / i2c["hz"], 4),
        "ir_on_s": round(raw["ir_on_ms"] / 1000, 4),
        "ir_tx_bytes": raw["ir_tx_bytes"],
        "ir_led_s": round(raw["ir_led_bits"] / ir["baud"], 4),
        "marks": marks,
        # Every refresh in order (F full, p partial) and the RTC time the
        # session started at, so the simulator can replay it exactly.
        "refresh_seq": "".join("F" if e[2] == "busy:full" else "p" for e in raw["events"]
                               if e[1] == "epd" and e[2] in ("busy:full", "busy:partial")),
        "start_unix_ms": raw.get("start_unix_ms", 0),
        "draw_s": draw_s(raw),
    }


def draw_s(raw):
    """CPU time to draw a screen before the panel is powered: from the RTC
    read in the title bar's clock to the panel's supply switching on, median
    over the refreshes where that read came fresh (under 60 ms before)."""
    last, gaps = None, []
    for t, what, detail in raw["events"]:
        if what == "i2c" and detail.startswith("read"):
            last = t
        elif what == "epd" and detail == "on":
            if last is not None and t - last < 60:
                gaps.append(t - last)
            last = None
    gaps.sort()
    return round(gaps[len(gaps) // 2] / 1000, 4) if gaps else 0.0


def timeline(raw):
    """A compact timeline for the docs chart and the simulator: state
    intervals per lane, in seconds from the start of the session."""
    t_end = raw["duration_ms"] / 1000
    lanes = {"stop2": [], "epd_on": [], "full": [], "partial": [], "ir": []}
    open_ = {}
    ticks = {"i2c": [], "ir_tx": []}

    def begin(k, t):
        open_.setdefault(k, t)

    def end(k, t):
        if k in open_:
            lanes[k].append([round(open_.pop(k), 3), round(t, 3)])

    for t_ms, what, detail in raw["events"]:
        t = t_ms / 1000
        if what == "mcu":
            (begin if detail == "stop2" else end)("stop2", t)
        elif what == "epd":
            if detail == "on":
                begin("epd_on", t)
            elif detail == "off":
                end("epd_on", t)
            elif detail in ("busy:full", "busy:partial"):
                begin(detail[5:], t)
            elif detail == "idle":
                end("full", t)
                end("partial", t)
        elif what == "ir":
            (begin if detail == "on" else end)("ir", t)
        elif what == "i2c":
            ticks["i2c"].append(round(t, 3))
        elif what == "ir_tx":
            ticks["ir_tx"].append(round(t, 3))
    for k in list(open_):
        end(k, t_end)
    # MCU duty in 100 ms bins (fraction of the time the core executes).
    per = raw["instructions_per_sample"]
    step = max(1, 100 // raw["sample_ms"])
    cap = raw["mips"] * 1e6 * raw["sample_ms"] / 1000 * step
    duty = [round(min(1.0, sum(per[i:i + step]) / cap), 3) for i in range(0, len(per), step)]
    return {"duration_s": round(t_end, 3), "lanes": lanes, "ticks": ticks,
            "duty_100ms": duty,
            "marks": [[round(e[0] / 1000, 3), e[2]] for e in raw["events"] if e[1] == "mark"]}


def measure(raw_dir):
    raw_dir = Path(raw_dir)
    out = {"_comment": "Generated by hardware/power/power.py --measured firmware/build/renode --write "
                       "from the Renode run of firmware/renode/power.robot. Times in seconds. "
                       "SPDX-License-Identifier: CERN-OHL-S-2.0",
           "sessions": {}, "timeline": {}}
    for name, fn in SESSIONS.items():
        raw = json.loads((raw_dir / fn).read_text())
        out["sessions"][name] = summarize(raw)
        if name == "use":
            out["timeline"] = timeline(raw)
    return out


def load_measured():
    return json.loads(MEASURED_PATH.read_text())


# ---------------------------------------------------------------- charge

def sleep_uA(case):
    return sum(v[case] for v in MODEL["sleep_uA"].values())


def charge_parts(s, case):
    """Charge in mA*s of one measured session, by contributor."""
    m, e, i, r = MODEL["mcu"], MODEL["epd"], MODEL["i2c"], MODEL["ir"]
    return {
        "MCU running": (s["mcu_run_s"] + s["spi_s"]) * m["run_mA"][case],
        "MCU in WFI": s["mcu_sleep_s"] * m["sleep_mA"][case],
        "e-paper refreshing": (s["epd_busy_full_s"] + s["epd_busy_partial_s"]) * e["refresh_mA"][case],
        "e-paper powered, not refreshing": s["epd_logic_s"] * e["logic_mA"][case],
        "RTC I2C": s["i2c_s"] * i["bus_mA"][case],
        "IR LED": s["ir_led_s"] * r["led_mA"][case],
        "IR receiver": s["ir_on_s"] * r["rx_mA"][case],
    }


def session_mAs(s, case):
    return sum(charge_parts(s, case).values())


def per_use_mAs(case, measured=None):
    return session_mAs((measured or load_measured())["sessions"]["use"], case)


def per_pairing_mAs(case, measured=None):
    return session_mAs((measured or load_measured())["sessions"]["pairing"], case)


def per_year_mAh(profile, case, measured=None):
    measured = measured or load_measured()
    p = MODEL["usage_profiles"][profile]
    sleep = sleep_uA(case) / 1000 * H_PER_YEAR
    use = per_use_mAs(case, measured) * p["uses_per_day"] * 365 / 3600
    pair = per_pairing_mAs(case, measured) * p["pairings_per_year"] / 3600
    return {"sleep": sleep, "use": use, "pairing": pair, "total": sleep + use + pair}


def life_years(profile, case, measured=None):
    b = MODEL["battery"]
    per_year = per_year_mAh(profile, case, measured)["total"]
    per_year += b["usable_mAh"] * b["self_discharge_pct_per_year"] / 100
    return b["usable_mAh"] / per_year


# ---------------------------------------------------------------- outputs

def _r(x, n=4):
    return float(f"{x:.{n}g}")


def sim_model(measured=None):
    """Everything the browser simulator's battery panel needs, derived from
    the same constants and measurements as the budget."""
    measured = measured or load_measured()
    use = measured["sessions"]["use"]
    pair = measured["sessions"]["pairing"]
    e = MODEL["epd"]
    out = {
        "battery": {k: MODEL["battery"][k] for k in ("part", "usable_mAh", "nominal_mAh",
                                                      "self_discharge_pct_per_year")},
        # overhead_s: per refresh, the time the STM32 keeps the core busy
        # besides BUSY: drawing the screen, then the driver (supply on,
        # resets, 2 x 4 kB of RAM). Both measured.
        "epd": {"full_refresh_s": e["full_refresh_s"], "partial_refresh_s": e["partial_refresh_s"],
                "overhead_s": round(use.get("draw_s", 0) + use["epd_logic_s"] /
                                    max(1, use["full_refreshes"] + use["partial_refreshes"]), 3)},
        "ir": {"baud": MODEL["ir"]["baud"]},
        "measured": {k: {f: v for f, v in s.items() if f != "marks"}
                     for k, s in measured["sessions"].items()},
        "cases": {},
        "profiles": {k: dict(v) for k, v in MODEL["usage_profiles"].items()},
        "requirement_years": MODEL["requirement_years"],
    }
    n_ref = max(1, use["full_refreshes"] + use["partial_refreshes"])
    for case in CASES:
        # Charge per refresh: the BUSY time at the panel current plus a share
        # of the panel's powered-but-idle time (reset, RAM writes).
        logic_per = use["epd_logic_s"] / n_ref * e["logic_mA"][case]
        parts = charge_parts(use, case)
        mcu = (parts["MCU running"] + parts["MCU in WFI"] + parts["RTC I2C"]) / use["awake_s"]
        pparts = charge_parts(pair, case)
        ir = (pparts["IR LED"] + pparts["IR receiver"]) / max(1e-9, pair["ir_on_s"])
        out["cases"][case] = {
            "sleep_uA": _r(sleep_uA(case)),
            "awake_mA": _r(mcu),
            "full_refresh_mAs": _r(e["full_refresh_s"] * e["refresh_mA"][case] + logic_per),
            "partial_refresh_mAs": _r(e["partial_refresh_s"] * e["refresh_mA"][case] + logic_per),
            "ir_mA": _r(ir),
            "use_mAs": _r(session_mAs(use, case)),
            "pairing_mAs": _r(session_mAs(pair, case)),
        }
    for name in out["profiles"]:
        out["profiles"][name]["life_years"] = {c: round(life_years(name, c, measured), 2) for c in CASES}
    return out


def sim_js(measured=None):
    body = json.dumps(sim_model(measured), indent=2)
    return ("// Generated by hardware/power/power.py --sim-js from hardware/power/model.json\n"
            "// and hardware/power/measured.json. Do not edit: CI fails if it is stale.\n"
            "// SPDX-License-Identifier: CERN-OHL-S-2.0\n"
            f"export default Object.freeze({body});\n")


def svg(measured=None):
    """The measured use session as a timeline chart (light and dark aware)."""
    tl = (measured or load_measured())["timeline"]
    T = tl["duration_s"]
    W, left, right = 900, 120, 20
    pw = W - left - right
    rows = [("MCU", 26), ("core busy", 26), ("e-paper", 26), ("RTC I2C", 16)]
    top = 34
    H = top + sum(h + 10 for _, h in rows) + 34
    x = lambda t: left + pw * t / T  # noqa: E731
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
         'font-family="system-ui, sans-serif" font-size="12" role="img" '
         'aria-label="Measured timeline of one use: MCU awake and in Stop 2, core activity, '
         'e-paper refreshes and RTC reads">',
         '<style>.bg{fill:#fff}.t{fill:#222}.g{stroke:#ccc}.awake{fill:#f4c542}.stop{fill:#8fb3d9}'
         '.full{fill:#333}.part{fill:#888}.on{fill:#ddd}.duty{fill:#d9822b}.i2c{stroke:#2a7}'
         '@media (prefers-color-scheme: dark){.bg{fill:#1b1b1d}.t{fill:#eee}.g{stroke:#444}'
         '.full{fill:#eee}.part{fill:#999}.on{fill:#444}}</style>',
         f'<rect class="bg" width="{W}" height="{H}"/>',
         f'<text class="t" x="{left}" y="20" font-weight="600">One use, measured on the emulated '
         f'STM32U083 ({T:.1f} s from the waking press)</text>']
    y = top
    lanes = tl["lanes"]
    for name, h in rows:
        o.append(f'<text class="t" x="{left - 8}" y="{y + h / 2 + 4}" text-anchor="end">{name}</text>')
        if name == "MCU":
            o.append(f'<rect class="awake" x="{left}" y="{y}" width="{pw}" height="{h}"/>')
            for a, b in lanes["stop2"]:
                o.append(f'<rect class="stop" x="{x(a):.1f}" y="{y}" width="{max(0.5, x(b) - x(a)):.1f}" height="{h}"/>')
        elif name == "core busy":
            n = len(tl["duty_100ms"])
            bw = pw * 0.1 / T
            for k, d in enumerate(tl["duty_100ms"]):
                if d > 0.001 and k < n:
                    hh = max(1.0, h * d)
                    o.append(f'<rect class="duty" x="{x(k * 0.1):.1f}" y="{y + h - hh:.1f}" '
                             f'width="{bw:.2f}" height="{hh:.1f}"/>')
            o.append(f'<line class="g" x1="{left}" x2="{left + pw}" y1="{y + h}" y2="{y + h}"/>')
        elif name == "e-paper":
            for a, b in lanes["epd_on"]:
                o.append(f'<rect class="on" x="{x(a):.1f}" y="{y}" width="{max(0.5, x(b) - x(a)):.1f}" height="{h}"/>')
            for k, cls in (("full", "full"), ("partial", "part")):
                for a, b in lanes[k]:
                    o.append(f'<rect class="{cls}" x="{x(a):.1f}" y="{y + 4}" '
                             f'width="{max(0.8, x(b) - x(a)):.1f}" height="{h - 8}"/>')
        else:
            for t in tl["ticks"]["i2c"]:
                o.append(f'<line class="i2c" x1="{x(t):.1f}" x2="{x(t):.1f}" y1="{y}" y2="{y + h}"/>')
        y += h + 10
    for t in range(0, int(T) + 1, 5):
        o.append(f'<line class="g" x1="{x(t):.1f}" x2="{x(t):.1f}" y1="{y - 6}" y2="{y}"/>')
        o.append(f'<text class="t" x="{x(t):.1f}" y="{y + 12}" text-anchor="middle">{t} s</text>')
    ly = y + 26
    legend = [("awake", "awake (Run/Sleep)"), ("stop", "Stop 2"), ("duty", "core executing"),
              ("full", "full refresh"), ("part", "partial refresh")]
    lx = left
    for cls, label in legend:
        o.append(f'<rect class="{cls}" x="{lx}" y="{ly - 9}" width="12" height="10"/>')
        o.append(f'<text class="t" x="{lx + 16}" y="{ly}">{label}</text>')
        lx += 30 + 7 * len(label)
    o.append("</svg>")
    return "\n".join(o) + "\n"


def markdown(measured=None):
    measured = measured or load_measured()
    b = MODEL["battery"]
    use, pair = measured["sessions"]["use"], measured["sessions"]["pairing"]
    e, m = MODEL["epd"], MODEL["mcu"]
    out = ["# Power calculations", "",
           "_Generated by `hardware/power/power.py` from the datasheet constants in "
           "`hardware/power/model.json` and the emulator measurements in "
           "`hardware/power/measured.json`. CI re-measures on every push and fails if the "
           f"firmware's behaviour drifts or the worst case at 20 uses a day drops below "
           f"{MODEL['requirement_years']:g} year._", "",
           f"Battery: **{b['part']}**, {b['usable_mAh']} mAh usable ({b['nominal_mAh']} mAh nominal), "
           f"{b['self_discharge_pct_per_year']} %/year self-discharge.", "",
           "## Where the numbers come from", "",
           "- **How long each part is on** is measured: the release firmware runs in Renode "
           "(`firmware/renode/power.robot`) through a scripted use and a pairing, and "
           "`firmware/renode/models/PowerMonitor.cs` records MCU Stop 2 entries and time, "
           "instructions executed, e-paper supply and BUSY periods, SPI bytes, RTC I2C "
           "transactions and every IR byte.",
           "- **How much current each part draws in each state** comes from datasheets "
           "(`model.json`, with sources below).", "",
           "## Sleep current", "", "| Contributor | Typical (µA) | Worst (µA) | Source |", "|-|-|-|-|"]
    for k, v in MODEL["sleep_uA"].items():
        out.append(f"| {k} | {v['typ']:.2f} | {v['worst']:.2f} | {v['source']} |")
    out.append(f"| **Total** | **{sleep_uA('typ'):.2f}** | **{sleep_uA('worst'):.2f}** | |")

    out += ["", "## One use, measured", "",
            "The script: wake with a press, open the contact and read the SAY/HEAR words "
            "for 10 s, back to the menu, open Codes, page down two accounts, open a code "
            "for 6 s, page through two more codes, then leave it until the firmware's "
            "30 s idle timeout puts it in Stop 2. That is a long use (words *and* a code), "
            "so the budget errs high.", "",
            "![Measured timeline of one use](images/power/use-timeline.svg)", ""]
    rows = [
        ("Awake (wake press to Stop 2)", f"{use['awake_s']:.1f} s", f"{pair['awake_s']:.1f} s"),
        ("Core executing (instructions / 16 MHz)",
         f"{use['mcu_run_s']:.3f} s ({use['instructions']:,} instr.)",
         f"{pair['mcu_run_s']:.3f} s ({pair['instructions']:,} instr.)"),
        ("Core in WFI (Sleep mode)", f"{use['mcu_sleep_s']:.1f} s", f"{pair['mcu_sleep_s']:.1f} s"),
        ("Full / partial refreshes", f"{use['full_refreshes']} / {use['partial_refreshes']}",
         f"{pair['full_refreshes']} / {pair['partial_refreshes']}"),
        ("E-paper BUSY (full + partial)",
         f"{use['epd_busy_full_s'] + use['epd_busy_partial_s']:.2f} s",
         f"{pair['epd_busy_full_s'] + pair['epd_busy_partial_s']:.2f} s"),
        ("E-paper powered, not refreshing", f"{use['epd_logic_s']:.2f} s", f"{pair['epd_logic_s']:.2f} s"),
        ("SPI to the panel", f"{use['spi_bytes']:,} B ({use['spi_s'] * 1000:.0f} ms at 8 MHz)",
         f"{pair['spi_bytes']:,} B ({pair['spi_s'] * 1000:.0f} ms)"),
        ("RTC I2C transactions", f"{use['i2c_transactions']} ({use['i2c_bytes']} B)",
         f"{pair['i2c_transactions']} ({pair['i2c_bytes']} B)"),
        ("IR receiver on / LED lit", f"{use['ir_on_s']:.1f} s / {use['ir_led_s']:.2f} s",
         f"{pair['ir_on_s']:.1f} s / {pair['ir_led_s']:.2f} s ({pair['ir_tx_bytes']} B sent)"),
        ("Stop 2 entries", str(use["stop2_entries"]), str(pair["stop2_entries"])),
    ]
    out += ["| | Use | Pairing |", "|-|-|-|"] + [f"| {a} | {u} | {p} |" for a, u, p in rows]

    out += ["", "## Charge", "",
            "| Contributor | Current typ / worst | Use: typ / worst (mA·s) | Pairing: typ / worst (mA·s) |",
            "|-|-|-|-|"]
    cur = {
        "MCU running": f"{m['run_mA']['typ']} / {m['run_mA']['worst']} mA",
        "MCU in WFI": f"{m['sleep_mA']['typ']} / {m['sleep_mA']['worst']} mA",
        "e-paper refreshing": f"{e['refresh_mA']['typ']} / {e['refresh_mA']['worst']} mA",
        "e-paper powered, not refreshing": f"{e['logic_mA']['typ']} / {e['logic_mA']['worst']} mA",
        "RTC I2C": f"{MODEL['i2c']['bus_mA']['typ']} / {MODEL['i2c']['bus_mA']['worst']} mA",
        "IR LED": f"{MODEL['ir']['led_mA']['typ']:g} / {MODEL['ir']['led_mA']['worst']:g} mA",
        "IR receiver": f"{MODEL['ir']['rx_mA']['typ']} / {MODEL['ir']['rx_mA']['worst']} mA",
    }
    ut, uw = charge_parts(use, "typ"), charge_parts(use, "worst")
    pt, pw = charge_parts(pair, "typ"), charge_parts(pair, "worst")
    for k in ut:
        out.append(f"| {k} | {cur[k]} | {ut[k]:.2f} / {uw[k]:.2f} | {pt[k]:.2f} / {pw[k]:.2f} |")
    out.append(f"| **Total** | | **{sum(ut.values()):.1f} / {sum(uw.values()):.1f}** | "
               f"**{sum(pt.values()):.1f} / {sum(pw.values()):.1f}** |")

    out += ["", "### Current sources", ""]
    srcs = [("MCU running", m["run_mA"]["source"]), ("MCU in WFI", m["sleep_mA"]["source"]),
            ("e-paper refreshing", e["refresh_mA"]["source"]),
            ("e-paper powered, not refreshing", e["logic_mA"]["source"]),
            ("Refresh times", e["_refresh_source"]),
            ("RTC I2C", MODEL["i2c"]["bus_mA"]["source"]),
            ("IR LED", MODEL["ir"]["led_mA"]["source"]), ("IR receiver", MODEL["ir"]["rx_mA"]["source"])]
    out += [f"- **{k}:** {v}" for k, v in srcs]

    out += ["", "## Battery life", "",
            "| Profile | Uses/day | Pairings/year | Typical: mAh/year | Typical: years | "
            "Worst: mAh/year | Worst: years |",
            "|-|-|-|-|-|-|-|"]
    for name, p in MODEL["usage_profiles"].items():
        t, w = per_year_mAh(name, "typ", measured), per_year_mAh(name, "worst", measured)
        out.append(f"| {name} | {p['uses_per_day']} | {p['pairings_per_year']} | {t['total']:.0f} | "
                   f"{life_years(name, 'typ', measured):.1f} | {w['total']:.0f} | "
                   f"{life_years(name, 'worst', measured):.1f} |")
    n = per_year_mAh("normal", "typ", measured)
    out += ["", f"At 20 uses a day (typical currents) the year's {n['total']:.0f} mAh splits into "
            f"{n['sleep']:.0f} mAh of sleep current, {n['use']:.0f} mAh of uses and "
            f"{n['pairing']:.1f} mAh of pairings.", "",
            "## How the measurement changed the firmware", "",
            "- **v0.1 budget, before any measurement:** a use assumed to be 40 s awake with 2 full "
            "and 3 partial refreshes (63 mA·s typical): 3.7 years typical, 2.1 worst case at 20 "
            "uses a day.",
            "- **First emulator measurement:** 77 s awake with 5 full and 20 partial refreshes "
            "(113 mA·s): 2.2 / 1.3 years, and heavy use (60 a day) under a year (0.8). The "
            "contact, code and code-list screens redrew every 5 s for their countdown bars, and "
            "every sixth redraw was a 3 s full refresh, for as long as the screen stayed up.",
            "- **Now** (`firmware/core/app.c`): a screen is redrawn when its content changes "
            "(words every 15 s, codes every 30 or 60 s); the countdown bar moves in thirds of the "
            "period with partial refreshes that never trigger a full one; a full refresh comes on "
            "the first content change after 5 partial ones; waking is a partial refresh because "
            "the sleep screen's full refresh left the panel clean; the sleep screen stays a full "
            "refresh so nothing sensitive is left on the glass. The same use is now "
            f"{use['awake_s']:.0f} s awake with {use['full_refreshes']} full and "
            f"{use['partial_refreshes']} partial refreshes ({session_mAs(use, 'typ'):.0f} mA·s): "
            f"{life_years('normal', 'typ', measured):.1f} years typical and "
            f"{life_years('normal', 'worst', measured):.1f} worst case at 20 uses a day, and "
            f"{life_years('heavy', 'typ', measured):.2f} years typical at 60 a day, so heavy use "
            "is still just short of a year. The next lever is the MCU waiting in Sleep mode at "
            "16 MHz while awake "
            f"({100 * charge_parts(use, 'typ')['MCU in WFI'] / session_mAs(use, 'typ'):.0f} % of a "
            "use, with a current that is still an estimate): idling in Stop 2 between polls, "
            "with a low-power timer as the time base, is a port change of its own.",
            "- **The clock** (`firmware/port/stm32u0/rv3032.c`): the RTC driver returned the "
            "second it last read for up to 250 ms, so just after a boundary a screen could show "
            "the previous code or word and redraw a moment later, an extra refresh. It now "
            "counts on from the RV-3032's hundredths with the millisecond tick. Found by "
            "replaying the measured use in the simulator, whose clock is exact.",
            "- **Pairing** listens before it talks, so frames rarely collide, and gives up after "
            "60 s; the LED is lit for about "
            f"{100 * pair['ir_led_s'] / max(1e-9, pair['ir_on_s']):.0f} % of the IR-on time.", "",
            "## What is still an assumption", "",
            "- **The e-paper current waveform.** The panel is modelled as a flat "
            f"{e['refresh_mA']['typ']} mA for the whole BUSY time, which is itself the datasheet "
            f"refresh time ({e['full_refresh_s']} s full, {e['partial_refresh_s']} s partial): the "
            "emulated SSD1680 holds BUSY exactly that long. The real booster draws pulses of "
            "10-20 mA, and the refresh takes longer in the cold. The first board should be "
            "measured with a current probe across a full and a partial refresh.",
            "- **MCU Run and Sleep currents** are estimates from the STM32U0 headline figures "
            "(see the sources above), not yet read off the DS14548 tables or measured.",
            "- **Cycles per instruction.** Renode advances 1/16 µs per instruction. A real "
            "Cortex-M0+ needs 1-3 cycles for loads, stores and branches, so the true run time "
            "is up to about 2x the measured one. Run time is a small share of the total, so "
            "this moves a use by a few percent.",
            "- **SPI and I2C wire time.** The emulated buses are instantaneous; their wire time is "
            "computed from the byte counts and charged at the Run current.",
            "- **The use script.** Real uses vary; the scripted one includes both the words and a "
            "code and the full idle timeout, so it is on the long side. Pressing left on the home "
            "menu sleeps at once and saves most of the idle time.",
            "- **Sleep current** is from datasheets (Stop 2, RTC, leakage); Renode does not model "
            "it. The first board's sleep current is the number to check.", "",
            "Battery life is capped in practice by the cell's shelf life (about 10 years) "
            "and by the e-paper's 0 °C lower limit in cold pockets, not by the electronics.", "",
            "Peak current is the other limit: a coin cell's internal resistance rises as it "
            "empties. `hardware/spice/battery_pulse.cir` checks that a refresh still works with a "
            "half-used cell (40 Ω), and the firmware shows the battery level from VREFINT."]
    return "\n".join(out) + "\n"


# The battery-life requirement: 20 uses a day with worst-case currents.
# (60 a day with typical currents is reported in the doc: 0.98 years with
# the MCU idling awake in Sleep mode; see "How the measurement changed".)
REQUIRED = (("normal", "worst"),)


def requirement_problems(measured=None):
    req = MODEL["requirement_years"]
    out = []
    for prof, case in REQUIRED:
        life = life_years(prof, case, measured)
        if life < req:
            out.append(f"{prof}, {case}: {life:.2f} years < {req} year requirement")
    return out


def drift(fresh, committed):
    """Differences beyond TOLERANCE between two measured.json 'sessions'."""
    problems = []
    for name, s in committed["sessions"].items():
        f = fresh["sessions"].get(name)
        if f is None:
            problems.append(f"{name}: session missing from the fresh run")
            continue
        for k, v in s.items():
            if k in ("marks", "start_unix_ms", "draw_s") or k not in f:
                continue
            if k in TOLERANCE["exact"]:
                if f[k] != v:
                    problems.append(f"{name}.{k}: {f[k]} (committed {v})")
            elif isinstance(v, (int, float)):
                tol = TOLERANCE["rel"] * abs(v) + 0.01
                if abs(f[k] - v) > tol:
                    problems.append(f"{name}.{k}: {f[k]} (committed {v}, tolerance ±{tol:.3g})")
        for case in CASES:
            a, b = session_mAs(f, case), session_mAs(s, case)
            if abs(a - b) > TOLERANCE["rel"] * b:
                problems.append(f"{name}: charge ({case}) {a:.2f} mA·s (committed {b:.2f})")
    return problems


def write_outputs(measured=None):
    DOC.write_text(markdown(measured))
    SVG.parent.mkdir(parents=True, exist_ok=True)
    SVG.write_text(svg(measured))
    SIM_JS.write_text(sim_js(measured))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--svg", action="store_true")
    ap.add_argument("--sim-js", action="store_true")
    ap.add_argument("--update", action="store_true", help="rewrite the doc, chart and sim model")
    ap.add_argument("--measured", metavar="DIR", help="Renode results directory with power-*.json")
    ap.add_argument("--check", action="store_true", help="with --measured: fail on drift or short life")
    ap.add_argument("--write", action="store_true", help="with --measured: replace measured.json")
    a = ap.parse_args()
    if a.markdown:
        print(markdown(), end="")
        return 0
    if a.svg:
        print(svg(), end="")
        return 0
    if a.sim_js:
        print(sim_js(), end="")
        return 0
    if a.measured:
        fresh = measure(a.measured)
        if a.write:
            MEASURED_PATH.write_text(json.dumps(fresh, indent=1) + "\n")
            write_outputs(fresh)
            print(f"wrote {MEASURED_PATH.relative_to(ROOT)} and the generated files")
        for name in MODEL["usage_profiles"]:
            print(f"{name:7s} typical {life_years(name, 'typ', fresh):5.1f} y   "
                  f"worst {life_years(name, 'worst', fresh):5.1f} y   (fresh measurement)")
        for k in SESSIONS:
            s = fresh["sessions"][k]
            print(f"{k:8s} awake {s['awake_s']:.1f} s, {s['full_refreshes']} full + "
                  f"{s['partial_refreshes']} partial refreshes, "
                  f"{session_mAs(s, 'typ'):.1f} / {session_mAs(s, 'worst'):.1f} mA·s")
        if a.check:
            problems = drift(fresh, load_measured())
            req = MODEL["requirement_years"]
            problems += requirement_problems(fresh)
            if problems:
                print("POWER CHECK FAILED:\n  " + "\n  ".join(problems), file=sys.stderr)
                print("If the change is intended: python3 hardware/power/power.py "
                      "--measured firmware/build/renode --write, and commit.", file=sys.stderr)
                return 1
            print("power check: fresh measurement matches hardware/power/measured.json")
        return 0
    if a.update:
        write_outputs()
        return 0
    for name in MODEL["usage_profiles"]:
        print(f"{name:7s} typical {life_years(name, 'typ'):5.1f} y   worst {life_years(name, 'worst'):5.1f} y")
    return 0


if __name__ == "__main__":
    sys.exit(main())
