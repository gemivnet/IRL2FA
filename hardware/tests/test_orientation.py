"""Direction-sensitive parts face the right way on the routed board.

The IR LED and phototransistor are side-looking and sit on the back, aimed
out through the top edge (y = 0). Their two pads are symmetric, so ERC, DRC
and the footprint checks all pass with the part turned to face into the
board; only the body and the lens marking show it. Placing a back-side part
mirrors it top to bottom, which is how this first went wrong.

SPDX-License-Identifier: CERN-OHL-S-2.0
"""
from pathlib import Path

import pytest

pcbnew = pytest.importorskip("pcbnew")

HW = Path(__file__).resolve().parents[1]
ORIGIN_Y = 100.0  # board top edge in KiCad coordinates (tools/gen_pcb.py ORIGIN)


@pytest.fixture(scope="module")
def board():
    return pcbnew.LoadBoard(str(HW / "irl2fa.kicad_pcb"))


@pytest.mark.parametrize("ref,label", [("D4", "IR OUT"), ("Q5", "IR IN")])
def test_ir_lens_faces_the_top_edge(board, ref, label):
    fp = board.FindFootprintByReference(ref)
    pad_y = pcbnew.ToMM(next(iter(fp.Pads())).GetPosition().y) - ORIGIN_Y
    texts = [g for g in fp.GraphicalItems() if g.GetClass() == "PCB_TEXT" and g.GetText() == label]
    assert texts, f"{ref} has no {label!r} marking on its fab layer"
    text_y = pcbnew.ToMM(texts[0].GetBoundingBox().GetCenter().y) - ORIGIN_Y
    assert text_y < pad_y, f"{ref}: lens marking at y={text_y:.2f} is inside the pads (y={pad_y:.2f}): the part faces into the board"
    crt = fp.GetCourtyard(pcbnew.B_CrtYd)
    top = pcbnew.ToMM(crt.BBox().GetTop()) - ORIGIN_Y
    assert top <= 0.0, f"{ref}: courtyard starts at y={top:.2f}, so the lens does not reach the board edge"
