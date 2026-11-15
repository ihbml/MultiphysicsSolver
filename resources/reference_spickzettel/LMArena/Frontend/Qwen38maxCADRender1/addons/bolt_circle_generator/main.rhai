fn on_init(cad) {
    cad.log("Bolt-Circle Generator ready (click button in Inspector to spawn)");
}

fn on_ui(cad) {
    cad.ui_heading("Parametric Flange & Fasteners");
    let count_f = cad.ui_slider("bolt_count", "Bolt Count", 4.0, 16.0, 8.0);
    let pcd = cad.ui_slider("pcd_r", "Pitch Radius", 0.70, 2.20, 1.35);
    let thick = cad.ui_slider("thickness", "Flange Height", 0.12, 0.45, 0.22);
    let teeth_f = cad.ui_slider("gear_teeth", "Companion Gear Teeth", 12.0, 36.0, 20.0);

    if cad.ui_button("spawn_flange_btn", "Spawn Bolt-Circle Assembly") {
        cad.clear_my_parts();
        let outer_r = pcd + 0.34;
        let hub_r = pcd * 0.48;
        let bore_r = pcd * 0.24;

        cad.spawn_flange(
            "Parametric Flange",
            outer_r,
            bore_r,
            thick,
            hub_r,
            thick * 1.4,
            3.6,
            0.0,
            0.0,
            0.18,
            0.52,
            0.84
        );

        let count = count_f.to_int();
        let tau = 6.28318530718;
        let i = 0;
        while i < count {
            let ang = (i * 1.0 / count) * tau;
            let bx = 3.6 + pcd * ang.cos();
            let bz = pcd * ang.sin();
            cad.spawn_bolt(
                "Bolt #" + (i + 1),
                0.085,
                0.08,
                0.048,
                thick + 0.16,
                bx,
                thick,
                bz,
                0.0,
                0.0,
                0.0
            );
            i += 1;
        }

        let teeth = teeth_f.to_int();
        cad.spawn_gear(
            "Companion Spur Gear",
            pcd * 0.65,
            teeth,
            0.24,
            bore_r,
            3.6,
            thick * 2.4 + 0.20,
            0.0,
            0.80,
            0.62,
            0.28
        );
        cad.log("Spawned flange + " + count + " bolts + " + teeth + "T gear");
    }

    if cad.ui_button("clear_btn", "Clear Generated Parts") {
        cad.clear_my_parts();
        cad.log("Cleared addon parts");
    }
}
