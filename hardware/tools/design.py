"""IRL2FA circuit description used to bootstrap the KiCad schematic and PCB.

The KiCad files in hardware/ are the source of truth once generated: edit
them in KiCad like any other project. This file records how they were first
built and is what gen_schematic.py / gen_pcb.py read. CI checks the
schematic against hardware/pins.yaml, not against this file.

Board coordinates (x, y in mm) are seen from the FRONT (screen side), origin
at the board's top-left corner, matching hardware/board.yaml.

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
from pathlib import Path

import yaml

HW = Path(__file__).resolve().parents[1]
PINS = yaml.safe_load((HW / "pins.yaml").read_text())
BOARD = yaml.safe_load((HW / "board.yaml").read_text())

R0402 = "Resistor_SMD:R_0402_1005Metric"
C0402 = "Capacitor_SMD:C_0402_1005Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"


def part(ref, lib, value, footprint, pins, block, lcsc=None, mpn=None, mfr=None,
         datasheet="", desc="", pcb=None, bom=True):
    return dict(ref=ref, lib=lib, value=value, footprint=footprint, pins=pins, block=block,
                lcsc=lcsc, mpn=mpn, mfr=mfr, datasheet=datasheet, desc=desc, pcb=pcb, bom=bom)


def res(ref, value, a, b, block, lcsc, pcb=None, mpn=None):
    return part(ref, "Device:R", value, R0402, {"1": a, "2": b}, block, lcsc, mpn, "UNI-ROYAL",
                desc=f"Resistor {value} 1% 0402", pcb=pcb)


def cap(ref, value, a, b, block, size="0402", lcsc=None, mpn=None, pcb=None, volts=""):
    fp = {"0402": C0402, "0603": C0603, "0805": C0805}[size]
    return part(ref, "Device:C", value, fp, {"1": a, "2": b}, block, lcsc, mpn, "Samsung",
                desc=f"Capacitor {value} {volts} {size}".replace("  ", " "), pcb=pcb)


# MCU pins straight from pins.yaml: unconnected pins become no-connects.
mcu_pins = {str(p["pin"]): p["net"] for p in PINS["pins"]}

PARTS = [
    # ---------------- power ----------------
    part("BT1", "Device:Battery_Cell", "CR2450",
         "irl2fa:BatteryHolder_MYOUNG_MY-2450-01_CR2450",
         {"1": "VBAT_RAW", "2": "GND"}, "power", "C964845", "MY-2450-01", "MYOUNG",
         "https://www.lcsc.com/product-detail/C964845.html",
         "CR2450 coin cell clip, SMT, 5.7 mm (the PCB pad is the - contact)",
         pcb=("B", BOARD["battery"]["x"], BOARD["battery"]["y"], 0)),
    part("Q1", "Transistor_FET:AO3401A", "AO3401A", "Package_TO_SOT_SMD:SOT-23",
         {"1": "GND", "2": "VBAT", "3": "VBAT_RAW"}, "power", "C15127", "AO3401A", "AOS",
         "https://www.lcsc.com/datasheet/lcsc_datasheet_1811081213_Alpha-Omega-Semicon-AO3401A_C15127.pdf",
         "Reverse-battery protection P-FET", pcb=("B", 58.5, 28.5, 90)),
    cap("C1", "47u", "VBAT", "GND", "power", "0805", "C16780", "CL21A476MQYNNNE",
        pcb=("B", 58.0, 22.5, 90), volts="6.3V X5R"),
    cap("C2", "47u", "VBAT", "GND", "power", "0805", "C16780", "CL21A476MQYNNNE",
        pcb=("B", 58.0, 17.0, 90), volts="6.3V X5R"),
    # Test points TP1-TP5: one row of 1.5 mm pads on a 2.54 mm grid on the back, for
    # current measurement (TP1/TP2) and for a pogo-pin programming fixture (all five:
    # JLCPCB's factory programming, docs/pcb-design.md "Factory programming").
    part("TP1", "Connector:TestPoint", "VBAT", "TestPoint:TestPoint_Pad_D1.5mm",
         {"1": "VBAT"}, "power", desc="Test point: VBAT (current measurement, programming supply)",
         bom=False),
    part("TP2", "Connector:TestPoint", "GND", "TestPoint:TestPoint_Pad_D1.5mm",
         {"1": "GND"}, "power", desc="Test point: GND", bom=False),

    # ---------------- MCU ----------------
    part("U1", "MCU_ST_STM32U0:STM32U083KCUx", "STM32U083KCU6",
         "Package_DFN_QFN:UFQFPN-32-1EP_5x5mm_P0.5mm_EP3.5x3.5mm", mcu_pins, "mcu",
         "C22459164", "STM32U083KCU6", "STMicroelectronics",
         "https://www.st.com/resource/en/datasheet/stm32u083kc.pdf",
         "Cortex-M0+ MCU, 256 KB flash, 40 KB RAM, AES, TRNG", pcb=("B", 67.0, 17.5, 0)),
    cap("C3", "100n", "VBAT", "GND", "mcu", lcsc="C1525", mpn="CL05B104KO5NNNC",
        pcb=("B", 63.0, 14.0, 0), volts="16V X7R"),
    cap("C4", "100n", "VBAT", "GND", "mcu", lcsc="C1525", mpn="CL05B104KO5NNNC",
        pcb=("B", 71.0, 21.0, 0), volts="16V X7R"),
    cap("C5", "4.7u", "VBAT", "GND", "mcu", "0603", "C19666", "CL10A475KO8NNNC",
        pcb=("B", 62.5, 20.5, 90), volts="16V X5R"),
    cap("C6", "1u", "VBAT", "GND", "mcu", lcsc="C52923", mpn="CL05A105KA5NQNC",
        pcb=("B", 63.0, 12.3, 0), volts="25V X5R"),
    cap("C7", "10n", "VBAT", "GND", "mcu", lcsc="C15195", mpn="CL05B103KB5NNNC",
        pcb=("B", 65.0, 11.5, 0), volts="50V X7R"),
    cap("C8", "100n", "NRST", "GND", "mcu", lcsc="C1525", mpn="CL05B104KO5NNNC",
        pcb=("B", 71.5, 13.5, 0), volts="16V X7R"),
    res("R1", "10k", "BOOT0", "GND", "mcu", "C25744", pcb=("B", 71.5, 23.0, 0),
        mpn="0402WGF1002TCE"),
    part("J3", "Connector:Conn_ARM_SWD_TagConnect_TC2030-NL", "TC2030-NL",
         "Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical",
         {"1": "VBAT", "2": "SWDIO", "3": "NRST", "4": "SWCLK", "5": "GND", "6": None},
         "mcu", desc="SWD programming pads for a Tag-Connect TC2030-NL cable", bom=False,
         datasheet="https://www.tag-connect.com/wp-content/uploads/bsk-pdf-manager/TC2030-CTX_1.pdf",
         pcb=("B", 72.5, 29.0, 0)),
    part("TP3", "Connector:TestPoint", "SWDIO", "TestPoint:TestPoint_Pad_D1.5mm",
         {"1": "SWDIO"}, "mcu", desc="Test point: SWDIO (programming fixture)", bom=False),
    part("TP4", "Connector:TestPoint", "SWCLK", "TestPoint:TestPoint_Pad_D1.5mm",
         {"1": "SWCLK"}, "mcu", desc="Test point: SWCLK (programming fixture)", bom=False),
    part("TP5", "Connector:TestPoint", "NRST", "TestPoint:TestPoint_Pad_D1.5mm",
         {"1": "NRST"}, "mcu", desc="Test point: NRST (programming fixture)", bom=False),

    # ---------------- RTC ----------------
    part("U2", "irl2fa:RV-3032-C7", "RV-3032-C7",
         "Package_SON:MicroCrystal_C7_SON-8_1.5x3.2mm_P0.9mm",
         {"1": "RTC_VBACKUP", "2": "I2C_SDA", "3": "RTC_INT", "4": "GND", "5": "GND",
          "6": "VBAT", "7": None, "8": "I2C_SCL"}, "rtc", "C5127802",
         "RV-3032-C7 32.768kHz 2.5ppm-TA-QC", "Micro Crystal",
         "https://www.microcrystal.com/fileadmin/Media/Products/RTC/Datasheet/RV-3032-C7.pdf",
         "Temperature-compensated RTC, I2C, 160 nA", pcb=("B", 66.0, 5.0, 90)),
    cap("C9", "100n", "VBAT", "GND", "rtc", lcsc="C1525", mpn="CL05B104KO5NNNC",
        pcb=("B", 68.5, 5.0, 90), volts="16V X7R"),
    res("R2", "10k", "RTC_VBACKUP", "GND", "rtc", "C25744", pcb=("B", 63.5, 4.0, 90),
        mpn="0402WGF1002TCE"),
    res("R3", "4.7k", "I2C_SCL", "VBAT", "rtc", "C25900", pcb=("B", 70.5, 7.5, 0),
        mpn="0402WGF4701TCE"),
    res("R4", "4.7k", "I2C_SDA", "VBAT", "rtc", "C25900", pcb=("B", 70.5, 9.0, 0),
        mpn="0402WGF4701TCE"),

    # ---------------- input ----------------
    part("SW1", "irl2fa:Nav_Switch_5way", "SKRHABE010", "irl2fa:SW_Nav_Alps_SKRHABE010",
         {"1": "JOY_A", "2": "JOY_CENTER", "3": "JOY_C", "4": "JOY_B", "5": "GND",
          "6": "JOY_D", "MP": "GND"}, "input", "C139794", "SKRHABE010", "Alps Alpine",
         "https://tech.alpsalpine.com/e/products/detail/SKRHABE010/",
         "5-way navigation switch: the only control", pcb=("F", 72.8, 17.0, 45)),

    # ---------------- display ----------------
    part("Q2", "Transistor_FET:AO3401A", "AO3401A", "Package_TO_SOT_SMD:SOT-23",
         {"1": "EPD_PWR_N", "2": "VBAT", "3": "VEPD"}, "display", "C15127", "AO3401A", "AOS",
         "https://www.lcsc.com/datasheet/lcsc_datasheet_1811081213_Alpha-Omega-Semicon-AO3401A_C15127.pdf",
         "E-paper power switch (P-FET)", pcb=("B", 21.0, 27.5, 0)),
    res("R5", "100k", "EPD_PWR_N", "VBAT", "display", "C25741", pcb=("B", 21.0, 30.5, 0),
        mpn="0402WGF1003TCE"),
    part("J1", "Connector_Generic_MountingPin:Conn_01x24_MountingPin", "FH34SRJ-24S",
         "irl2fa:Hirose_FH34SRJ-24S-0.5SH_1x24-1MP_P0.50mm_Horizontal",
         {"1": None, "2": "EPD_GDR", "3": "EPD_RESE", "4": None, "5": "EPD_VSH2", "6": None,
          "7": None, "8": "GND", "9": "EPD_BUSY", "10": "EPD_RST", "11": "EPD_DC",
          "12": "EPD_CS", "13": "EPD_SCK", "14": "EPD_SDI", "15": "VEPD", "16": "VEPD",
          "17": "GND", "18": "EPD_VDD", "19": None, "20": "EPD_VSH1", "21": "EPD_PREVGH",
          "22": "EPD_VSL", "23": "EPD_PREVGL", "24": "EPD_VCOM", "MP": "GND"},
         "display", "C324726", "FH34SRJ-24S-0.5SH(50)", "Hirose",
         "https://www.hirose.com/product/document?clcode=CL0580-1262-0-50&productname=FH34SRJ-24S-0.5SH(50)&series=FH34S&documenttype=2DDrawing&lang=en&documentid=0001002473",
         "24-pin 0.5 mm FPC connector for the e-paper", pcb=("B", 10.0, 17.0, 90)),
    part("DS1", None, "GDEY0213B74", "irl2fa:EPD_GoodDisplay_GDEY0213B74_Outline", {},
         "display", None, "GDEY0213B74", "Good Display",
         "https://www.good-display.com/product/391.html",
         "2.13in 250x122 e-paper (SSD1680), fitted by hand", bom=False,
         pcb=None),   # placed by PLACEMENT below, centred on the glass
    part("L1", "Device:L", "47u", "Inductor_SMD:L_APV_ANR3015",
         {"1": "VEPD", "2": "EPD_SW"}, "display", "C7427088", "ANR3015T470M", "APV",
         desc="47 uH 0.36 A booster inductor", pcb=("B", 16.0, 6.0, 0)),
    part("Q3", "Transistor_FET:Q_NMOS_GSD", "NX3008NBK", "Package_TO_SOT_SMD:SOT-23",
         {"1": "EPD_GDR", "2": "EPD_RESE", "3": "EPD_SW"}, "display", "C179399",
         "NX3008NBK,215", "Nexperia",
         "https://assets.nexperia.com/documents/data-sheet/NX3008NBK.pdf",
         "Booster switch N-FET, Vgs(th) 1.1 V max (SSD1680 asks <= 1.3 V)", pcb=("B", 16.0, 10.0, 0)),
    res("R6", "1M", "EPD_GDR", "GND", "display", "C26083", pcb=("B", 13.5, 11.5, 90),
        mpn="0402WGF1004TCE"),
    res("R7", "2.2", "EPD_RESE", "GND", "display", "C25101", pcb=("B", 18.5, 11.5, 90),
        mpn="0402WGF220KTCE"),
    part("D1", "Device:D_Schottky", "MBR0530", "Diode_SMD:D_SOD-123",
         {"1": "EPD_NEG", "2": "EPD_PREVGL"}, "display", "C77336", "MBR0530", "JSCJ",
         desc="Schottky 30 V 0.5 A", pcb=("B", 20.5, 4.0, 0)),
    part("D2", "Device:D_Schottky", "MBR0530", "Diode_SMD:D_SOD-123",
         {"1": "GND", "2": "EPD_NEG"}, "display", "C77336", "MBR0530", "JSCJ",
         desc="Schottky 30 V 0.5 A", pcb=("B", 20.5, 7.0, 0)),
    part("D3", "Device:D_Schottky", "MBR0530", "Diode_SMD:D_SOD-123",
         {"1": "EPD_PREVGH", "2": "EPD_SW"}, "display", "C77336", "MBR0530", "JSCJ",
         desc="Schottky 30 V 0.5 A", pcb=("B", 20.5, 10.0, 0)),
    cap("C10", "4.7u", "VEPD", "GND", "display", "0805", "C1779", "CL21A475KAQNNNE",
        pcb=("B", 13.5, 4.0, 90), volts="25V X5R"),
    cap("C11", "4.7u", "EPD_SW", "EPD_NEG", "display", "0805", "C1779", "CL21A475KAQNNNE",
        pcb=("B", 23.5, 6.0, 90), volts="25V X5R"),
]

# The panel's eight 1 uF / 25 V+ decoupling capacitors (GDEY0213B74 reference
# circuit C2, C5..C7, C9..C12), placed in a column next to the connector.
for i, net in enumerate(["EPD_PREVGH", "EPD_VSH2", "VEPD", "EPD_VDD", "EPD_VSH1",
                         "EPD_VSL", "EPD_PREVGL", "EPD_VCOM"]):
    PARTS.append(cap(f"C{12 + i}", "1u", net, "GND", "display", "0603", "C15849",
                     "CL10A105KB8NNNC", pcb=("B", 16.5, 14.5 + 2.0 * i, 0), volts="50V X5R"))

PARTS += [
    # ---------------- IR link ----------------
    part("D4", "Device:LED", "VSMB2948SL", "irl2fa:LED_Vishay_VSMB2948SL_SideView",
         {"1": "IR_LED_K", "2": "IR_LED_A"}, "ir", "C6604342", "VSMB2948SL", "Vishay",
         "https://www.vishay.com/docs/83498/vsmb2948sl.pdf",
         "940 nm side-looking IR emitter", pcb=("B", BOARD["ir"]["emitter"]["x"],
                                                BOARD["ir"]["emitter"]["y"], 0)),
    res("R8", "68", "VBAT", "IR_LED_A", "ir", "C25131", pcb=("B", 55.0, 4.0, 90),
        mpn="0402WGF680JTCE"),
    part("Q4", "Transistor_FET:AO3400A", "AO3400A", "Package_TO_SOT_SMD:SOT-23",
         {"1": "IR_TX", "2": "GND", "3": "IR_LED_K"}, "ir", "C20917", "AO3400A", "AOS",
         "https://www.aosmd.com/res/datasheets/AO3400A.pdf",
         "IR LED driver (Vth <= 1.45 V, works from a flat cell)", pcb=("B", 58.0, 6.0, 0)),
    res("R9", "100k", "IR_TX", "GND", "ir", "C25741", pcb=("B", 61.0, 6.5, 90),
        mpn="0402WGF1003TCE"),
    part("Q5", "Device:Q_Photo_NPN", "VEMT2023SLX01",
         "irl2fa:PhotoTransistor_Vishay_VEMT2023SLX01_SideView",
         {"1": "IR_RX_PWR", "2": "IR_RX"}, "ir", "C2843576", "VEMT2023SLX01", "Vishay",
         "https://www.vishay.com/docs/84166/vemt2023slx01.pdf",
         "Side-looking IR phototransistor with daylight-blocking filter", pcb=("B", BOARD["ir"]["detector"]["x"],
                                                 BOARD["ir"]["detector"]["y"], 0)),
    res("R10", "10k", "IR_RX", "GND", "ir", "C25744", pcb=("B", 25.0, 4.0, 90),
        mpn="0402WGF1002TCE"),

    # ---------------- mechanical ----------------
]
for i, h in enumerate(BOARD["board"]["mounting_holes"]):
    PARTS.append(part(f"H{i + 1}", "Mechanical:MountingHole_Pad", "M2",
                      "MountingHole:MountingHole_2.2mm_M2_Pad", {"1": "GND"}, "mech",
                      desc="M2 mounting hole", bom=False, pcb=("F", h["x"], h["y"], 0)))

# Nets that need a PWR_FLAG for ERC (driven only through passives or FETs).
POWER_FLAGS = ["VBAT", "GND", "VBAT_RAW", "RTC_VBACKUP"]

BLOCK_TITLES = {
    "power": "Power: CR2450, reverse protection, bulk capacitance",
    "mcu": "MCU: STM32U083 + SWD (Tag-Connect TC2030)",
    "rtc": "RTC: RV-3032-C7 (temperature compensated)",
    "input": "Input: 5-way joystick (the only control)",
    "display": "E-paper: GDEY0213B74 (SSD1680), booster, power switch",
    "ir": "IR pairing link (4800 baud UART, inverted)",
    "mech": "Mechanical",
}

# ---------------------------------------------------------------------------
# PCB placement (side, x, y, rotation), board coordinates seen from the front.
# Back side, left to right: FPC connector and panel capacitors, booster (top
# left, outside the cell's circle), the CR2450 clip in the middle (cell circle
# r=12.25 around (38.5, 16); plate 25.5 wide from y 3.5 to 26.7; legs/pads out
# to x 20.1 and 56.9), and the MCU/RTC/SWD on the right. The cell slides in
# from the bottom edge, so the strip x 26-51 below the cell stays empty.
# The joystick is the only front-side part.
# rotation None = chosen by gen_pcb.py (see orient_fpc_connector).
_G = BOARD["display"]["glass"]
PLACEMENT = {
    "BT1": ("B", BOARD["battery"]["x"], BOARD["battery"]["y"], 180),   # closed end towards the IR edge
    "Q1": ("B", 56.2, 22.2, 0),     # above the test-pad row's labels
    "C1": ("B", 59.2, 9.8, 90),
    "C2": ("B", 59.6, 21.4, 90),
    # Test-pad row on the bottom edge: 2.54 mm pitch, right of the cell's insertion
    # path (x 25.9..51.1) and below J3. Fixture order GND, VBAT, SWDIO, SWCLK, NRST.
    "TP2": ("B", 52.9, 29.5, 0),
    "TP1": ("B", 52.9 + 2.54, 29.5, 0),
    "TP3": ("B", 52.9 + 2 * 2.54, 29.5, 0),
    "TP4": ("B", 52.9 + 3 * 2.54, 29.5, 0),
    "TP5": ("B", 52.9 + 4 * 2.54, 29.5, 0),
    "U1": ("B", 64.6, 16.0, 90),
    "C3": ("B", 63.0, 20.4, 0),
    "C4": ("B", 64.6, 11.6, 0),
    "C5": ("B", 59.4, 13.4, 90),
    "C6": ("B", 65.2, 20.4, 0),
    "C7": ("B", 67.3, 20.4, 0),
    "C8": ("B", 63.8, 22.0, 0),
    "R1": ("B", 68.4, 18.4, 90),
    "J3": ("B", 66.0, 26.2, 0),
    "U2": ("B", 69.6, 7.8, 0),
    "C9": ("B", 72.2, 7.8, 90),
    "R2": ("B", 69.6, 4.9, 0),
    "R3": ("B", 67.6, 10.5, 0),   # clear of U2's pin-1 mark
    "R4": ("B", 67.6, 11.9, 0),
    "SW1": ("F", BOARD["joystick"]["x"], BOARD["joystick"]["y"], 45),
    # The 14.3 mm tail ends 7.73 mm right of the glass edge on a 0.8 mm board (gen_models.py
    # prints it): 0.53 mm past J1's origin, as on the first (1.0 mm) board, ~0.8 mm insertion margin
    "J1": ("B", _G["x"] + 7.2, _G["y"] + _G["h"] / 2 - 0.2, None),
    "Q2": ("B", 18.0, 27.4, 0),
    "R5": ("B", 21.0, 29.6, 0),
    "L1": ("B", 24.6, 5.8, 0),
    "Q3": ("B", 20.8, 5.6, 0),
    "R6": ("B", 18.0, 5.8, 90),
    "R7": ("B", 20.8, 8.2, 0),
    "D1": ("B", 28.8, 4.6, 0),
    "D2": ("B", 28.3, 2.0, 0),
    "D3": ("B", 33.4, 2.0, 0),
    "C10": ("B", 16.4, 3.6, 0),
    "C11": ("B", 23.4, 10.2, 0),
    "D4": ("B", BOARD["ir"]["emitter"]["x"], BOARD["ir"]["emitter"]["y"], 180),  # lens out through the top edge (back-side placement mirrors y)
    "R8": ("B", 53.4, 2.6, 90),
    "Q4": ("B", 54.4, 5.8, 0),
    "R9": ("B", 51.6, 6.4, 90),
    "Q5": ("B", BOARD["ir"]["detector"]["x"], BOARD["ir"]["detector"]["y"], 180),  # lens out through the top edge (back-side placement mirrors y)
    "R10": ("B", 24.2, 2.6, 0),
    "DS1": ("F", _G["x"] + _G["w"] / 2, _G["y"] + _G["h"] / 2, 0),
}
# Panel capacitors in a column beside J1, leaving a gap level with J1's
# signal pins (9-14) so those can escape towards the MCU.
_CAP_Y = {"C12": 7.5, "C13": 9.3, "C14": 11.1,                      # PREVGH, VSH2, VEPD
          "C15": 17.9, "C16": 19.6, "C17": 21.3, "C18": 23.0, "C19": 24.7}
for _ref, _y in _CAP_Y.items():
    PLACEMENT[_ref] = ("B", 16.9, _y, 0)
for _p in PARTS:
    if _p["ref"] in PLACEMENT:
        _p["pcb"] = PLACEMENT[_p["ref"]]

# Placement experiments: IRL2FA_PLACE="U1=B,66.5,16.5,90;C3=..." overrides.
import os as _os  # noqa: E402

for _item in filter(None, _os.environ.get("IRL2FA_PLACE", "").split(";")):
    _ref, _v = _item.split("=")
    _side, _x, _y, _r = _v.split(",")
    for _p in PARTS:
        if _p["ref"] == _ref:
            _p["pcb"] = (_side, float(_x), float(_y), float(_r))
