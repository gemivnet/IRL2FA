// IRL2FA - 3D-printable case
// SPDX-License-Identifier: CC-BY-SA-4.0
// (c) IRL2FA contributors. See case/LICENSE.
//
// Every board dimension comes from board_dims.scad, which is GENERATED from
// hardware/board.yaml by `python case/tools/gen_dims.py`. Never type a board
// number into this file: change board.yaml and regenerate.
//
// Model coordinates: X = board x, Y = board_height - board y (so the IR "top"
// edge, board y = 0, is at +Y), Z = 0 on the PCB FRONT (screen side), +Z
// towards the viewer. The PCB occupies z = -board_thickness .. 0.
//
// Export one part at a time:  openscad -D 'part="front"' -o front.stl irl2fa_case.scad
// Parts: "front" | "back" | "door" | "cap" | "assembly" | "exploded" | "info"
// Exported parts are already in print orientation (largest flat face on z = 0).

include <board_dims.scad>

/* [Export] */
part = "assembly";        // "front" | "back" | "door" | "cap" | "assembly" | "exploded" | "info"
explode = 14;             // mm between layers in the "exploded" view
from_stl = false;         // assembly/exploded: import the built STLs instead of recomputing (fast renders)
show_board = true;        // assembly: draw the PCB, display, joystick, battery placeholders
flip_view = false;        // assembly: turn the device over (about the Y axis) to look at the back

/* [Tolerances] */
fit_clearance  = 0.25;    // gap between mating printed parts (lip/rebate, door rails)
pcb_clearance  = 0.35;    // PCB outline to case inner wall
part_clearance = 0.40;    // components (glass, joystick, back-side parts) to case; tests demand >= 0.3

/* [Walls] */
wall            = 1.6;    // side walls
top_thickness   = 1.2;    // front skin over the display
floor_thickness = 1.2;    // back floor (and battery door plate)
edge_r_front    = 1.0;    // round-over on the front face perimeter (ends in a 45 deg chamfer at the bed)
edge_r_back     = 2.0;    // round-over on the back face perimeter (ends in a 45 deg chamfer at the bed)
lip_height      = 1.0;    // alignment lip on the back shell, rises into a rebate in the front shell
seam_groove     = 0.4;    // V reveal at the parting line; hides small print variations

/* [Display window] */
window_margin  = 0.3;     // window is the ACTIVE area grown by this (rounded corners of this radius)
bezel_chamfer  = 0.6;     // 45 deg chamfer on the window edge at the front face
glass_frame_w  = 1.0;     // width of the rib that locates the glass edges
glass_frame_depth = 0.5;  // how far the rib reaches down the glass edge (from the glass top)

/* [Joystick cap] */
cap_d           = 7.0;    // cap diameter
cap_gap         = 0.4;    // radial gap cap <-> case hole (allows the 4-way tilt)
cap_protrusion  = 1.5;    // target height of the cap rim above the front face (raised automatically if the roof would be too thin)
cap_roof        = 0.6;    // minimum material above the stem top, at the bottom of the finger dish
cap_dish        = 0.3;    // finger dish depth
cap_top_round   = 0.8;    // round-over on the cap's top edge
stem_shape      = "square";// "square" | "round". SKRHABE010 (Alps STEP): 3.0 mm square stem, r0.3 corners,
                           // aligned with the body, i.e. at 45 deg on this board
stem_socket_extra = 0.20; // socket = stem + this (before the crush ribs)
stem_rib_interference = 0.10; // radial interference of each crush rib on the stem
stem_rib_count  = 3;      // crush ribs in a round socket (a square socket gets one per face)
// Round collars between the SKRH body and its stem, [diameter, top above PCB front],
// from the Alps STEP model (not in board.yaml). The cap clears them by part_clearance.
joystick_collars = [[5.2, 2.15], [4.0, 2.95]];
stem_top_gap    = 0.05;   // gap between the stem top and the socket roof

/* [Fasteners] */
fastener        = "selftap"; // "selftap": M2x6 thread-forming screw straight into the boss
                             // "insert" : M2 heat-set insert in the boss, M2x6 machine screw
screw_length    = 6;      // under-head length
screw_head_d    = 4.0;    // M2 pan/cheese head diameter (ISO 7045 / DIN 7985: 4.0)
screw_head_h    = 1.6;    // head height
screw_seat      = 0.8;    // material under the head, above the PCB front
screw_clear_d   = 2.4;    // clearance hole through the front shell
pilot_d         = 1.7;    // self-tap pilot hole in the back boss
insert_d        = 3.2;    // heat-set insert hole (check your insert's datasheet)
insert_length   = 3.0;    // heat-set insert length
boss_d_selftap  = 4.6;    // boss diameter for self-tapping screws
boss_d_insert   = 5.6;    // boss diameter for heat-set inserts

/* [Battery door] */
door_margin     = 1.5;    // opening beyond the cell edge on each side (outer face): room to tip the cell out
door_w          = battery_cell_diameter + 2 * door_margin;     // door width at the outer face
door_lead_y     = battery_y - battery_cell_diameter / 2 - 1.0; // board y of the leading edge (outer face)
door_top_gap    = 0.1;    // gap between door wall and front shell at the parting line
detent_r        = 0.55;   // detent bump radius
detent_protrude = 0.45;   // bump protrusion normal to the rail
relief_beam     = 1.4;    // width of the flexible rail beam on the door (outer face)
relief_w        = 0.8;    // relief slot width
relief_len      = 14;     // relief slot length
detent_y        = battery_y + 9; // board y of the detent bumps

/* [IR windows] */
ir_window        = "open"; // "open": chamfered slot | "thin": 0.6 mm wall (print the back in IR-clear filament)
ir_window_w      = 3.0;    // along x
ir_window_h      = 2.6;    // down from the PCB back surface (side-looker optical axis ~1.2-1.5 mm)
ir_window_chamfer = 0.5;
ir_thin_wall     = 0.6;

/* [Lanyard] */
lanyard_corner  = "left"; // "left" (bottom-left) | "right" (bottom-right)
lanyard_t       = 4.0;    // tab thickness (from the back face)
lanyard_r       = 4.2;    // tab end radius
lanyard_hole_d  = 4.0;
lanyard_reach   = 2.8;    // hole centre beyond the case corner arc

/* [Quality] */
fn_corner = 72;
$fa = 4;
$fs = 0.25;

// ---------------------------------------------------------------- derived
W  = board_width;
H  = board_height;
BR = board_corner_radius;
eps = 0.01;

function M(x, y) = [x, H - y];          // board (x, y) -> model (X, Y)

z_pcb_front = 0;
z_pcb_back  = -board_thickness;
z_seam      = z_pcb_back;               // parting line = PCB back plane
glass_top   = display_adhesive + display_glass_t;
z_skin_in   = glass_top + part_clearance;
z_top       = z_skin_in + top_thickness;
z_cav_bot   = z_pcb_back - back_keepout_height - part_clearance;
z_bot       = z_cav_bot - floor_thickness;

R_out   = BR + pcb_clearance + wall;
lip_t   = wall / 2 - fit_clearance / 2;
corners = [[BR, BR], [W - BR, BR], [BR, H - BR], [W - BR, H - BR]];

joy     = M(joystick_x, joystick_y);
joy_top = joystick_body_height;
cap_bottom = max(joy_top, joystick_collars[0][1]) + part_clearance;
cap_top = max(z_top + cap_protrusion,
              joystick_stem_top + stem_top_gap + cap_roof + cap_dish);
cap_collar_top = joystick_collars[len(joystick_collars) - 1][1] + part_clearance; // socket starts here
cap_hole_d = cap_d + 2 * cap_gap;

boss_d  = fastener == "insert" ? boss_d_insert : boss_d_selftap;
z_seat  = z_pcb_front + screw_seat;
screw_tip = z_seat - screw_length;

door_x   = battery_x;
door_t   = floor_thickness;
Y_lead   = H - door_lead_y;
Y_end    = -(pcb_clearance + wall) - 6;    // beyond the bottom edge
door_ww  = door_w + 2 * door_t;           // door wall section width
Y_det    = H - detent_y;

out_W = W + 2 * (pcb_clearance + wall);
out_H = H + 2 * (pcb_clearance + wall);
out_T = z_top - z_bot;

lan_dir    = lanyard_corner == "right" ? [1, -1] / sqrt(2) : [-1, -1] / sqrt(2);
lan_corner = lanyard_corner == "right" ? [W - BR, BR] : [BR, BR];
lan_hole   = lan_corner + lan_dir * (R_out + lanyard_reach);

assert(joy_top + part_clearance + 0.6 <= z_top + 1e-6,
       "joystick body leaves < 0.6 mm of front skin; raise top_thickness");
assert(screw_tip > z_cav_bot + 0.6, "screw would break through the back floor");
assert(cap_bottom < z_top - 0.1, "cap would not reach into the case hole");
assert(joystick_stem_top - cap_collar_top >= 1.0, "less than 1 mm of stem engages the cap");

echo(str("IRL2FA case outer size: ", out_W, " x ", out_H, " x ", out_T,
         " mm (W x H x T, without lanyard tab); cap protrusion ", cap_top - z_top, " mm"));
if (cap_top - z_top > cap_protrusion + eps)
    echo(str("NOTE: cap protrusion raised from ", cap_protrusion, " to ", cap_top - z_top,
             " mm to keep a ", cap_roof, " mm roof over the stem"));

// ---------------------------------------------------------------- 2D helpers
module rr2d(r) hull() for (c = corners) translate(c) circle(r = r, $fn = fn_corner);
module board2d(grow = 0) rr2d(BR + grow);
module cavity2d() board2d(pcb_clearance);
module rect2d(x, y, w, h) translate(M(x, y + h)) square([w, h]);

// Revolved profile: vertical side of radius R between z0 and z1, rounded at
// both ends. A bed-facing round-over turns into a 45 deg chamfer for its last
// 45 deg so that it prints without supports ("teardrop" edge).
function _arc(cx, cz, r, a0, a1, n) = [for (i = [0 : n]) let(a = a0 + (a1 - a0) * i / n) [cx + r * cos(a), cz + r * sin(a)]];
function profile(R, z0, z1, rb, rt, top_round = false, n = 8) = concat(
    [[0, z0], [R - rb * (2 - sqrt(2)), z0]],
    _arc(R - rb, z0 + rb, rb, -45, 0, n),
    top_round ? _arc(R - rt, z1 - rt, rt, 0, 90, 2 * n)
              : concat(_arc(R - rt, z1 - rt, rt, 0, 45, n), [[R - rt * (2 - sqrt(2)), z1]]),
    [[0, z1]]);

module puck(R, z0, z1, rb, rt, top_round = false)
    rotate_extrude($fn = fn_corner) polygon(profile(R, z0, z1, rb, rt, top_round));

// ---------------------------------------------------------------- outer body
module body_main() hull() for (c = corners) translate(c) puck(R_out, z_bot, z_top, edge_r_back, edge_r_front);

module lanyard_tab()
    hull() for (p = [lan_hole, lan_corner])
        translate(p) puck(lanyard_r, z_bot, z_bot + lanyard_t, edge_r_back, lanyard_t - edge_r_back - 0.5, true);

module lanyard_hole() translate(lan_hole) {
    translate([0, 0, z_bot - 1]) cylinder(d = lanyard_hole_d, h = lanyard_t + 2);
    translate([0, 0, z_bot - eps]) cylinder(d1 = lanyard_hole_d + 1.2, d2 = lanyard_hole_d - eps, h = 0.6 + eps);
    translate([0, 0, z_bot + lanyard_t - 0.6]) cylinder(d1 = lanyard_hole_d - eps, d2 = lanyard_hole_d + 1.2, h = 0.6 + eps);
}

module seam_groove() difference() {
    translate([0, 0, z_seam - seam_groove]) linear_extrude(2 * seam_groove) rr2d(R_out + 2);
    hull() {
        translate([0, 0, z_seam - eps / 2]) linear_extrude(eps) rr2d(R_out - seam_groove);
        translate([0, 0, z_seam - seam_groove - eps]) linear_extrude(eps) rr2d(R_out + eps);
        translate([0, 0, z_seam + seam_groove]) linear_extrude(eps) rr2d(R_out + eps);
    }
}

// boss footprint: disc + the corner quadrant, so it merges into the wall
module boss2d(h) intersection() {
    cavity2d();
    union() {
        translate(h) circle(d = boss_d);
        s = [h[0] < W / 2 ? -1 : 1, h[1] < H / 2 ? -1 : 1];
        translate(h) mirror([s[0] < 0 ? 1 : 0, 0]) mirror([0, s[1] < 0 ? 1 : 0]) square(10);
    }
}
holes = [for (p = board_mounting_holes) M(p[0], p[1])];

// ---------------------------------------------------------------- back shell
module lip()
    translate([0, 0, z_seam]) linear_extrude(lip_height)
        difference() { board2d(pcb_clearance + lip_t); cavity2d(); }

// door envelope; s shrinks it (fit clearance, normal to the rails)
module door_frustum(s = 0, top_extra = 0) {
    hs = s * sqrt(2);
    hull() {   // 45 deg rails between the outer face (z_bot) and the inner face (z_cav_bot)
        translate([door_x - door_w / 2 + hs, Y_end, z_bot - eps])
            cube([door_w - 2 * hs, Y_lead - hs - Y_end, eps]);
        translate([door_x - door_w / 2 - door_t + hs, Y_end, z_cav_bot - eps])
            cube([door_w + 2 * door_t - 2 * hs, Y_lead + door_t - hs - Y_end, eps]);
    }
    translate([door_x - door_w / 2 + hs, Y_end, z_bot - 1])   // below the outer face
        cube([door_w - 2 * hs, Y_lead - hs - Y_end, 1]);
    if (top_extra > 0)
        translate([door_x - door_w / 2 - door_t + hs, Y_end, z_cav_bot])
            cube([door_w + 2 * door_t - 2 * hs, Y_lead + door_t - hs - Y_end, top_extra]);
}
module door_wall_block(s = 0, ztop = z_seam + lip_height + 1, yin = -pcb_clearance + eps)
    translate([door_x - door_ww / 2 + s, Y_end, z_bot - 1])
        cube([door_ww - 2 * s, yin - Y_end, ztop - z_bot + 1]);

module door_cut() { door_frustum(0, 1); door_wall_block(0); }

// detent bump centre on a rail (side = +1 right, -1 left), at the rail's mid-height
function rail_point(side) = let(zm = (z_bot + z_cav_bot) / 2)
    [door_x + side * (door_w / 2 + (zm - z_bot)), Y_det, zm];
function rail_normal(side) = [-side, 0, 1] / sqrt(2);   // from case material into the channel

module detent_bumps() intersection() {
    for (sd = [-1, 1]) translate(rail_point(sd) - rail_normal(sd) * (detent_r - detent_protrude))
        sphere(r = detent_r, $fn = 24);
    translate([0, Y_det - 5, z_bot]) cube([W, 10, door_t]);
}

module ir_windows() for (x = [ir_emitter_x, ir_detector_x]) {
    y_in  = H + pcb_clearance;
    y_out = y_in + wall;
    if (ir_window == "thin") {
        translate([x - ir_window_w / 2, y_in - 1, z_seam - ir_window_h])
            cube([ir_window_w, 1 + wall - ir_thin_wall, ir_window_h]);
    } else {
        translate([x - ir_window_w / 2, y_in - 1, z_seam - ir_window_h])
            cube([ir_window_w, wall + 2, ir_window_h]);
        c = ir_window_chamfer;
        hull() {
            translate([x - ir_window_w / 2, y_out - c, z_seam - ir_window_h])
                cube([ir_window_w, eps, ir_window_h]);
            translate([x - ir_window_w / 2 - c, y_out, z_seam - ir_window_h - c])
                cube([ir_window_w + 2 * c, 1, ir_window_h + c]);
        }
    }
}

module back_holes() for (h = holes) translate(h) {
    if (fastener == "insert") {
        translate([0, 0, z_seam - insert_length - 0.4]) cylinder(d = insert_d, h = insert_length + 0.4 + eps);
        translate([0, 0, z_seam - 0.4]) cylinder(d1 = insert_d, d2 = insert_d + 0.8, h = 0.4 + eps);
    } else {
        translate([0, 0, z_seam - 0.3]) cylinder(d1 = pilot_d, d2 = pilot_d + 0.6, h = 0.3 + eps);
    }
    translate([0, 0, screw_tip - 0.6]) cylinder(d = fastener == "insert" ? screw_clear_d : pilot_d,
                                               h = z_seam - screw_tip + 0.6 + eps);
}

module back_shell() union() {
    difference() {
        union() {
            difference() {
                union() {
                    intersection() {
                        union() { body_main(); lanyard_tab(); }
                        translate([-50, -50, z_bot - 1]) cube([W + 100, H + 100, z_seam - z_bot + 1]);
                    }
                    lip();
                }
                translate([0, 0, z_cav_bot]) linear_extrude(z_seam + lip_height + 1 - z_cav_bot) cavity2d();
            }
            for (h = holes) translate([0, 0, z_cav_bot - eps]) linear_extrude(z_seam - z_cav_bot + eps) boss2d(h);
        }
        door_cut();
        ir_windows();
        back_holes();
        lanyard_hole();
        seam_groove();
    }
    detent_bumps();
}

// ---------------------------------------------------------------- battery door
module door() difference() {
    intersection() {
        body_main();
        union() {
            intersection() {
                door_frustum(fit_clearance);
                translate([-50, -50, z_bot - 1]) cube([W + 100, H + 100, z_cav_bot - z_bot + 1]);
            }
            door_wall_block(fit_clearance, z_seam - door_top_gap);
        }
    }
    seam_groove();
    // detent dimples
    for (sd = [-1, 1]) translate(rail_point(sd)) sphere(r = detent_r + 0.1, $fn = 24);
    // relief slots make the rails springy
    for (sd = [-1, 1]) {
        xs = door_x + sd * (door_w / 2 - fit_clearance * sqrt(2) - relief_beam - relief_w / 2);
        hull() for (dy = [-1, 1]) translate([xs, Y_det + dy * (relief_len - relief_w) / 2, z_bot - 1])
            cylinder(d = relief_w, h = door_t + 2, $fn = 16);
    }
    // grip grooves (V, 45 deg) on the outer face
    for (i = [0 : 3]) translate([door_x, 0.9 + 1.3 * i, z_bot])
        rotate([45, 0, 0]) cube([door_w - 8, 0.45, 0.45], center = true);
}

// ---------------------------------------------------------------- front shell
module glass_rect2d(grow = 0)
    translate(M(display_glass_x, display_glass_y + display_glass_h) - [grow, grow])
        square([display_glass_w + 2 * grow, display_glass_h + 2 * grow]);

module joystick_pocket2d() translate(joy) rotate(45)
    square(joystick_body + 2 * part_clearance, center = true);

module glass_frame() {
    fy = display_glass_y + display_glass_h / 2;      // FPC centre
    translate([0, 0, glass_top - glass_frame_depth]) linear_extrude(z_skin_in - glass_top + glass_frame_depth + eps)
    intersection() {
        cavity2d();
        difference() {
            glass_rect2d(part_clearance + glass_frame_w);
            glass_rect2d(part_clearance);
            // FPC exit: leave the left rib open over the flex
            if (display_fpc_exit == "left")
                translate(M(display_glass_x - 5, fy + display_fpc_width / 2 + part_clearance + 0.5))
                    square([6, display_fpc_width + 2 * part_clearance + 1]);
            offset(delta = 0.3) joystick_pocket2d();
        }
    }
}

module display_window() {
    a = [display_active_x, display_active_y, display_active_w, display_active_h];
    translate([0, 0, z_skin_in - 1]) linear_extrude(z_top - z_skin_in + 2)
        offset(r = window_margin) rect2d(a[0], a[1], a[2], a[3]);
    hull() {
        translate([0, 0, z_top - bezel_chamfer]) linear_extrude(eps)
            offset(r = window_margin) rect2d(a[0], a[1], a[2], a[3]);
        translate([0, 0, z_top]) linear_extrude(eps)
            offset(r = window_margin + bezel_chamfer + eps) rect2d(a[0], a[1], a[2], a[3]);
    }
}

module joystick_cut() {
    translate([0, 0, z_seam - 1]) linear_extrude(joy_top + part_clearance - z_seam + 1) joystick_pocket2d();
    translate([joy[0], joy[1], z_seam - 1]) cylinder(d = cap_hole_d, h = z_top - z_seam + 2);
    translate([joy[0], joy[1], z_top - 0.4]) cylinder(d1 = cap_hole_d, d2 = cap_hole_d + 0.8 + 2 * eps, h = 0.4 + eps);
}

module front_screw_cuts() for (h = holes) translate(h) {
    translate([0, 0, z_seat]) cylinder(d = screw_head_d + 0.4, h = z_top - z_seat + 1);
    translate([0, 0, z_top - 0.3]) cylinder(d1 = screw_head_d + 0.4, d2 = screw_head_d + 1.0 + eps, h = 0.3 + eps);
    translate([0, 0, z_seam - 1]) cylinder(d = screw_clear_d, h = z_seat - z_seam + 1 + eps);
}

module front_shell() difference() {
    union() {
        difference() {
            intersection() {
                body_main();
                translate([-50, -50, z_seam]) cube([W + 100, H + 100, z_top - z_seam + 1]);
            }
            translate([0, 0, z_seam - 1]) linear_extrude(z_skin_in - z_seam + 1) cavity2d();
            translate([0, 0, z_seam - 1]) linear_extrude(1 + lip_height + fit_clearance)
                board2d(pcb_clearance + lip_t + fit_clearance);
        }
        for (h = holes) translate([0, 0, z_pcb_front]) linear_extrude(z_skin_in - z_pcb_front + eps) boss2d(h);
        glass_frame();
    }
    display_window();
    joystick_cut();
    front_screw_cuts();
    seam_groove();
}

// ---------------------------------------------------------------- joystick cap
module cap() {
    dd = cap_d - 2 * cap_top_round + 0.4;              // dish diameter
    rs = (dd * dd / 4 + cap_dish * cap_dish) / (2 * cap_dish);
    sr = joystick_stem_diameter / 2 + stem_socket_extra / 2;   // socket radius / half-side
    rib_r = 0.25;
    translate([joy[0], joy[1], 0]) {
        difference() {
            rotate_extrude($fn = 96) polygon(concat(
                [[0, cap_bottom], [cap_d / 2 - 0.2, cap_bottom], [cap_d / 2, cap_bottom + 0.2]],
                _arc(cap_d / 2 - cap_top_round, cap_top - cap_top_round, cap_top_round, 0, 90, 12),
                [[0, cap_top]]));
            translate([0, 0, cap_top - cap_dish + rs]) sphere(r = rs, $fn = 96);
            // socket
            translate([0, 0, cap_bottom - 1]) {
                if (stem_shape == "square") rotate(45) translate([-sr, -sr, 0]) cube([2 * sr, 2 * sr, joystick_stem_top + stem_top_gap - cap_bottom + 1]);
                else cylinder(r = sr, h = joystick_stem_top + stem_top_gap - cap_bottom + 1, $fn = 48);
            }
            // clear the collars below the stem (the next collar up is the widest one in the cap)
            cd = joystick_collars[len(joystick_collars) - 1][0] + 2 * part_clearance;
            translate([0, 0, cap_bottom - 1]) cylinder(d = cd, h = cap_collar_top - cap_bottom + 1, $fn = 48);
            translate([0, 0, cap_collar_top - eps]) cylinder(d1 = cd, d2 = 2 * sr * 0.9, h = (cd - 1.8 * sr) / 2, $fn = 48);
        }
        // crush ribs
        n = stem_shape == "square" ? 4 : stem_rib_count;
        intersection() {
            for (i = [0 : n - 1]) rotate(i * 360 / n + (stem_shape == "square" ? 45 : 90))
                translate([sr + rib_r - (stem_socket_extra / 2 + stem_rib_interference), 0, cap_collar_top])
                    cylinder(r = rib_r, h = joystick_stem_top + stem_top_gap - cap_collar_top, $fn = 12);
            translate([0, 0, cap_bottom]) cylinder(r = cap_d / 2 - 0.5, h = cap_top - cap_bottom);
        }
    }
}

// ---------------------------------------------------------------- placeholders (assembly only)
module board_model() {
    // PCB
    color([0.10, 0.42, 0.22, 0.55]) translate([0, 0, z_pcb_back]) linear_extrude(board_thickness)
        difference() {
            board2d();
            for (h = holes) translate(h) circle(d = board_mounting_hole_drill);
            rect2d(fpc_slot_x, fpc_slot_y, fpc_slot_w, fpc_slot_h);
        }
    // display glass + active area with a TOTP code
    color([0.80, 0.80, 0.78]) translate([0, 0, display_adhesive]) linear_extrude(display_glass_t)
        glass_rect2d();
    a = M(display_active_x, display_active_y + display_active_h);
    color([0.90, 0.90, 0.86]) translate([a[0], a[1], glass_top]) cube([display_active_w, display_active_h, 0.02]);
    color([0.12, 0.12, 0.12]) translate([a[0] + display_active_w / 2, a[1] + display_active_h * 0.55, glass_top])
        linear_extrude(0.04) text("482 913", size = display_active_h * 0.30, font = "DejaVu Sans Mono:style=Bold",
                                  halign = "center", valign = "center");
    color([0.12, 0.12, 0.12]) translate([a[0] + 2.5, a[1] + 2.4, glass_top]) linear_extrude(0.04)
        text("example.org", size = 2.3, font = "DejaVu Sans");
    color([0.12, 0.12, 0.12]) translate([a[0] + display_active_w - 2.5, a[1] + 2.4, glass_top]) linear_extrude(0.04)
        text("1/3", size = 2.3, font = "DejaVu Sans", halign = "right");
    color([0.12, 0.12, 0.12]) translate([a[0] + 2.5, a[1] + display_active_h - 3.2, glass_top]) linear_extrude(0.04)
        square([(display_active_w - 5) * 0.62, 0.9]);
    // FPC
    fy = display_glass_y + display_glass_h / 2;
    color([0.85, 0.55, 0.15]) translate([0, 0, display_adhesive]) linear_extrude(0.15)
        rect2d(fpc_slot_x + fpc_slot_w / 2, fy - display_fpc_width / 2, display_glass_x - fpc_slot_x, display_fpc_width);
    // joystick: body, collars, square stem
    color([0.15, 0.15, 0.17]) translate([joy[0], joy[1], 0]) rotate(45)
        translate([-joystick_body / 2, -joystick_body / 2, 0]) cube([joystick_body, joystick_body, joystick_body_height]);
    color([0.55, 0.56, 0.58]) translate([joy[0], joy[1], 0]) for (c = joystick_collars) cylinder(d = c[0], h = c[1]);
    color([0.92, 0.92, 0.92]) translate([joy[0], joy[1], 0]) rotate(45) linear_extrude(joystick_stem_top)
        offset(r = 0.3) square(joystick_stem_diameter - 0.6, center = true);
    // battery holder (MYOUNG BS-24-B4AA016: base, two end blocks with lips, side spring) + CR2450
    b = M(battery_x, battery_y);
    hb = [27.2, 16];                                   // holder body (datasheet), not in board.yaml
    lip = 1.5;                                         // lip thickness over the cell
    zc = z_pcb_back - (battery_holder_height - lip);   // cell face towards the floor
    color([0.22, 0.22, 0.24]) translate([b[0], b[1], 0]) {
        translate([-hb[0] / 2, -hb[1] / 2, z_pcb_back - 0.8]) cube([hb[0], hb[1], 0.8]);
        for (sx = [-1, 1]) translate([sx > 0 ? hb[0] / 2 - 2.6 : -hb[0] / 2, -hb[1] / 2, z_pcb_back - battery_holder_height])
            cube([2.6, hb[1], battery_holder_height]);
    }
    color([0.80, 0.78, 0.60]) translate([b[0], b[1], z_pcb_back - 0.15]) for (sx = [-1, 1])
        translate([sx * 16.69 - 1.8, -2.25, 0]) cube([3.6, 4.5, 0.15]);
    color([0.78, 0.79, 0.82]) translate([b[0], b[1], zc]) cylinder(d = battery_cell_diameter, h = battery_cell_thickness);
    color([0.6, 0.6, 0.62]) translate([b[0], b[1], zc - 0.01]) linear_extrude(0.02)
        text("CR2450", size = 3, font = "DejaVu Sans:style=Bold", halign = "center", valign = "center");
    // IR emitter / detector: Vishay side-lookers on the back, lens tip at the board edge, facing +Y
    for (p = [[ir_emitter_x, ir_emitter_y], [ir_detector_x, ir_detector_y]]) {
        q = M(p[0], p[1]);
        color([0.1, 0.1, 0.1]) translate([q[0] - 1.9, q[1] - 1.18, z_pcb_back - 2.3]) cube([3.8, 2.4, 2.3]);
        color([0.55, 0.65, 0.95]) translate([q[0], q[1] + 1.18, z_pcb_back - 1.15]) rotate([-90, 0, 0])
            cylinder(d1 = 1.8, d2 = 0.6, h = 0.18);
    }
}

// ---------------------------------------------------------------- print transforms
// print -> assembly; each is its own inverse except for the sign of z offsets
module to_print(p) {
    if (p == "front") translate([0, H, z_top]) rotate([180, 0, 0]) children();
    else if (p == "cap") translate([0, 0, -cap_bottom]) children();
    else translate([0, 0, -z_bot]) children();
}
module from_print(p) {
    if (p == "front") translate([0, H, z_top]) rotate([180, 0, 0]) children();
    else if (p == "cap") translate([0, 0, cap_bottom]) children();
    else translate([0, 0, z_bot]) children();
}

module part_geom(p) {
    if (from_stl) from_print(p) import(str("stl/irl2fa-", p, ".stl"), convexity = 10);
    else if (p == "front") front_shell();
    else if (p == "back") back_shell();
    else if (p == "door") door();
    else if (p == "cap") cap();
}

c_shell = [0.30, 0.32, 0.35];
c_door  = c_shell;
c_cap   = [0.93, 0.42, 0.13];

module screws() color([0.62, 0.63, 0.66]) for (h = holes) translate([h[0], h[1], z_seat]) difference() {
    union() {
        cylinder(d = screw_head_d, h = screw_head_h - 0.4);
        translate([0, 0, screw_head_h - 0.4]) cylinder(d1 = screw_head_d, d2 = screw_head_d - 0.8, h = 0.4);
    }
    for (a = [0, 90]) rotate(a) translate([-1.1, -0.25, screw_head_h - 0.7]) cube([2.2, 0.5, 1]);
}

module assembly(e = 0) {
    color(c_shell) translate([0, 0, 2 * e]) part_geom("front");
    if (show_board) translate([0, 0, 2.6 * e]) screws();
    color(c_cap)   translate([0, 0, 3.2 * e]) part_geom("cap");
    if (show_board) translate([0, 0, e]) board_model();
    color(c_shell) part_geom("back");
    color(c_door)  translate([0, -0.6 * e, -0.8 * e]) part_geom("door");
}

// ---------------------------------------------------------------- dispatch
if (part == "front") to_print("front") front_shell();
else if (part == "back") to_print("back") back_shell();
else if (part == "door") to_print("door") door();
else if (part == "cap") to_print("cap") cap();
else if (part == "assembly") rotate([0, flip_view ? 180 : 0, 0]) assembly(0);
else if (part == "exploded") assembly(explode);
else if (part == "info") {
    // machine-readable summary for the tests (build.sh stores it as stl/irl2fa-case-info.json)
    echo(str("CASEINFO{",
        "\"board_height\":", H, ",\"z_top\":", z_top, ",\"z_bot\":", z_bot, ",\"z_seam\":", z_seam,
        ",\"z_skin_in\":", z_skin_in, ",\"z_cav_bot\":", z_cav_bot, ",\"glass_top\":", glass_top,
        ",\"cap_bottom\":", cap_bottom, ",\"cap_top\":", cap_top, ",\"cap_d\":", cap_d,
        ",\"cap_hole_d\":", cap_hole_d, ",\"boss_d\":", boss_d, ",\"wall\":", wall,
        ",\"top_thickness\":", top_thickness, ",\"floor_thickness\":", floor_thickness,
        ",\"pcb_clearance\":", pcb_clearance, ",\"part_clearance\":", part_clearance,
        ",\"fit_clearance\":", fit_clearance, ",\"ir_window\":\"", ir_window, "\"",
        ",\"ir_window_w\":", ir_window_w, ",\"ir_window_h\":", ir_window_h, ",\"ir_thin_wall\":", ir_thin_wall,
        ",\"door_x\":", door_x, ",\"door_w\":", door_w, ",\"door_lead_y\":", door_lead_y,
        ",\"lanyard_hole\":[", lan_hole[0], ",", lan_hole[1], "],\"lanyard_r\":", lanyard_r,
        ",\"outer\":[", out_W, ",", out_H, ",", out_T, "]",
        ",\"print\":{\"front\":{\"flip\":true,\"ty\":", H, ",\"tz\":", z_top, "},",
        "\"back\":{\"flip\":false,\"ty\":0,\"tz\":", -z_bot, "},",
        "\"door\":{\"flip\":false,\"ty\":0,\"tz\":", -z_bot, "},",
        "\"cap\":{\"flip\":false,\"ty\":0,\"tz\":", -cap_bottom, "}}",
        "}"));
}
