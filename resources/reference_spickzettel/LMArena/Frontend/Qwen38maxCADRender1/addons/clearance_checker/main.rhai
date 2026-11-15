fn on_init(cad) {
    cad.log("Clearance Analyzer initialized");
}

fn on_ui(cad) {
    cad.ui_heading("Proximity & Bounding Envelope");
    let show_env = cad.ui_checkbox("show_env", "Draw All Part Envelopes", false);
    let threshold = cad.ui_slider("gap_thresh", "Proximity Threshold", 0.02, 0.80, 0.15);

    let parts = cad.list_parts();
    let sel_id = cad.selected_pick_id();
    let sel_idx = -1;

    let i = 0;
    while i < parts.len() {
        let p = parts[i];
        if p.pick_id == sel_id {
            sel_idx = i;
        }
        if show_env && p.visible {
            cad.draw_box(p.min_x, p.min_y, p.min_z, p.max_x, p.max_y, p.max_z, 0.35, 0.72, 0.85);
        }
        i += 1;
    }

    if sel_idx >= 0 {
        let sel = parts[sel_idx];
        let near_count = 0;
        let closest_name = "None";
        let closest_dist = 9999.0;

        let j = 0;
        while j < parts.len() {
            if j != sel_idx && parts[j].visible {
                let d = cad.min_aabb_distance(sel_idx, j);
                if d < closest_dist {
                    closest_dist = d;
                    closest_name = parts[j].name;
                }
                if d <= threshold {
                    near_count += 1;
                    let other = parts[j];
                    cad.draw_line(
                        sel.center_x, sel.center_y, sel.center_z,
                        other.center_x, other.center_y, other.center_z,
                        1.0, 0.68, 0.18, true
                    );
                }
            }
            j += 1;
        }

        cad.ui_readout("Selected:", sel.name);
        cad.ui_readout("Closest Neighbor:", closest_name);
        cad.ui_readout("Min AABB Gap:", "" + ((closest_dist * 1000.0).round() / 1000.0));
        cad.ui_readout("Neighbors <= Thresh:", "" + near_count);
    } else {
        cad.ui_label("Click a part in the viewport to inspect clearance.");
    }
}
