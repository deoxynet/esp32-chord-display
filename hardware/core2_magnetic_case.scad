/*
  One-piece M5Stack Core2 case with TWO 10 x 3 mm disk magnets on its
  narrow BOTTOM EDGE. Magnet faces point down when the screen is upright.
  Print "case" flat back down, without supports. Glue magnets in from below.
  The front of each magnet pocket is covered.

  Core2: 54 x 54 x 16.5 mm; 5 mm case corner radius supplied by owner.
  All dimensions in mm. Preview objects never enter an STL export.
*/

/* [Output] */
part = "installed"; // [installed,case]
show_reference = true;

/* [Magnets] */
magnet_diameter = 10;
magnet_thickness = 3;
magnet_diameter_clearance = 0.2;
// Magnet faces sit slightly below the plastic for direct contact with the lip.
magnet_proud = 0.2;
magnet_spacing = 36; // Centre to centre; two magnets.
magnet_wall = 1.6;

/* [Core2 fit] */
core_width = 54;
core_height = 54;
core_thickness = 16.5;
core_corner_radius = 5;
device_clearance = 0.35; // Per side.

/* [Hidden] */
$fn = $preview ? 48 : 96;
eps = 0.02;
back = 2.4;
rim = 3;
clip_wall = 1.6;
corner_length = 7;
lip = 0.9;
lip_height = 1.2;
inner_w = core_width + 2 * device_clearance;
inner_h = core_height + 2 * device_clearance;
pocket_r = core_corner_radius + device_clearance;
front_z = back + core_thickness + 0.5;
magnet_pocket_d = magnet_diameter + magnet_diameter_clearance;
magnet_pocket_r = magnet_pocket_d / 2;
magnet_pocket_depth = magnet_thickness - magnet_proud;
foot_width = magnet_pocket_d + 2 * magnet_wall;
foot_depth = magnet_pocket_depth + magnet_wall;
foot_y = -inner_h / 2 - foot_depth;
magnet_z = back + core_thickness / 2;
// A 45-degree pocket roof closes the front without needing print supports.
magnet_roof_z = magnet_z + sqrt(2) * magnet_pocket_r;
foot_front = max(back + core_thickness, magnet_roof_z + magnet_wall);
magnet_centres = [-magnet_spacing / 2, magnet_spacing / 2];

assert(magnet_diameter > 0 && magnet_thickness > 0,
       "Magnet dimensions must be positive.");
assert(magnet_proud >= 0 && magnet_proud < magnet_thickness,
       "Magnet exposure must be smaller than its thickness.");
assert(magnet_diameter_clearance >= 0 && magnet_wall >= 1.2,
       "Check magnet clearance and wall thickness.");
assert(magnet_pocket_d + 2 * magnet_wall <= core_thickness,
       "Magnet is too wide for the bottom edge; use a smaller disk.");
assert(magnet_spacing > foot_width && magnet_spacing + foot_width <= inner_w,
       "Magnet holders must fit across the case without overlapping.");
assert(pocket_r > 0 && pocket_r < corner_length,
       "Corner clips must extend past the rounded case corners.");
assert(part == "installed" || part == "case", "Choose installed or case.");

module rounded_rectangle(w, h, r) {
    offset(r = r) square([w - 2 * r, h - 2 * r], center = true);
}

module pocket(inset = 0) {
    offset(delta = -inset) rounded_rectangle(inner_w, inner_h, pocket_r);
}

module corner_mask(sx, sy) {
    translate([sx < 0 ? -inner_w / 2 - clip_wall : inner_w / 2 - corner_length,
               sy < 0 ? -inner_h / 2 - clip_wall : inner_h / 2 - corner_length])
        square([corner_length + clip_wall, corner_length + clip_wall]);
}

module corner_clip(sx, sy) {
    translate([0, 0, back - eps])
        linear_extrude(height = front_z + lip_height - back + eps)
            intersection() {
                difference() {
                    offset(delta = clip_wall) pocket();
                    pocket();
                }
                corner_mask(sx, sy);
            }
    // Gradually sloped retaining lips avoid unsupported horizontal ledges.
    intersection() {
        difference() {
            translate([0, 0, front_z]) linear_extrude(height = lip_height)
                offset(delta = clip_wall) pocket();
            hull() {
                translate([0, 0, front_z - eps]) linear_extrude(height = eps)
                    pocket();
                translate([0, 0, front_z + lip_height])
                    linear_extrude(height = eps) pocket(lip);
            }
        }
        linear_extrude(height = front_z + lip_height + eps) corner_mask(sx, sy);
    }
}

module magnet_seat_cutout(x) {
    // Axis is parallel to Y: disks attach to the BOTTOM, not the rear panel.
    // Round disk clearance with a pointed roof: every roof slope is 45 degrees
    // or less. The opening is only on the underside, for gluing in the magnet.
    translate([x, foot_y + magnet_pocket_depth, magnet_z]) rotate([90, 0, 0])
        linear_extrude(height = magnet_pocket_depth + eps) union() {
            circle(r = magnet_pocket_r);
            polygon([[-magnet_pocket_r / sqrt(2), magnet_pocket_r / sqrt(2)],
                     [magnet_pocket_r / sqrt(2), magnet_pocket_r / sqrt(2)],
                     [0, sqrt(2) * magnet_pocket_r]]);
        }
}

module magnetic_case() {
    difference() {
        union() {
            linear_extrude(height = back)
                rounded_rectangle(inner_w + 2 * rim, inner_h + 2 * rim,
                                  pocket_r + rim);
            for (sx = [-1, 1], sy = [-1, 1]) corner_clip(sx, sy);
            for (x = magnet_centres)
                translate([x - foot_width / 2, foot_y, 0])
                    cube([foot_width, foot_depth + eps, foot_front]);
        }
        for (x = magnet_centres) magnet_seat_cutout(x);
    }
}

module installed_frame() {
    // Screen faces -Y; magnet faces sit on Z=0; the case rises above the lip.
    translate([0, back + core_thickness, -foot_y + magnet_proud])
        rotate([90, 0, 0]) children();
}

module core2_reference() {
    color("#252e37") translate([0, 0, back])
        linear_extrude(height = core_thickness)
            rounded_rectangle(core_width, core_height, core_corner_radius);
    color("#081c24") translate([0, 3, back + core_thickness + eps])
        linear_extrude(height = 0.1) rounded_rectangle(48, 38, 1.5);
    color("#b2ed80") translate([0, 3, back + core_thickness + 0.15])
        linear_extrude(height = 0.1)
            text("Cmaj7", size = 8, halign = "center", valign = "center");
    for (x = [-14, 0, 14]) color("#b6bdc5")
        translate([x, -21, back + core_thickness + eps]) cylinder(d = 3, h = 0.1);
}

module magnets_reference() {
    for (x = magnet_centres) color("#c2c7cf")
        translate([x, foot_y - magnet_proud, magnet_z]) rotate([-90, 0, 0])
            cylinder(d = magnet_diameter, h = magnet_thickness);
}

if (part == "case") magnetic_case();
if (part == "installed") {
    color("#587a91") installed_frame() magnetic_case();
    if ($preview && show_reference) {
        installed_frame() {
            core2_reference();
            magnets_reference();
        }
        // Illustrative horizontal keyboard lip; not a measured SV-2 model.
        color("#d8d8d3") translate([-43, -7, -2]) cube([86, 31, 2]);
    }
}
