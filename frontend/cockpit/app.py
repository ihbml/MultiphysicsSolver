"""
app.py
======
Trame + PyVista 3D CFD Cockpit & Model Control Center.
Bietet ein interaktives Web-Dashboard (WebGL 60 FPS) mit Modell-Panel
nach Vorbild moderner CFD-Software (Fluent / Star-CCM+).
"""

from __future__ import annotations
import sys
from pathlib import Path

# Workspace Pfade einbinden
ROOT_DIR = Path(__file__).resolve().parent.parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from backend.physics_modules.registry import ModuleRegistry
from backend.core.case_generator import CaseGenerator

import pyvista as pv
from trame.app import get_server
from trame.ui.vuetify import SinglePageLayout
from trame.widgets import vuetify, vtk as vtk_widgets


def create_cockpit_app(server=None):
    if server is None:
        server = get_server()
    state, ctrl = server.state, server.controller

    # 1. Physik-Module & Case-Generator laden
    registry = ModuleRegistry()
    case_gen = CaseGenerator(registry)
    all_modules = registry.get_all_modules()

    # 2. PyVista Plotter (Client-Side Rendering via WebGL / VTK.js)
    plotter = pv.Plotter(off_screen=True)
    plotter.set_background("#1e1e24")

    # Start-Demo-Geometrie (Wird bei Case-Laden durch echtes Netz ersetzt)
    sphere = pv.Sphere(radius=1.0)
    sphere["Temperature"] = sphere.points[:, 2] * 200.0 + 800.0
    actor = plotter.add_mesh(sphere, scalars="Temperature", cmap="inferno", show_edges=True)
    plotter.reset_camera()

    # 3. Trame UI State initialisieren
    state.active_field = "Temperature"
    state.slice_normal = "X"
    state.slice_offset = 0.0
    state.modules_state = {m: all_modules[m].config.enabled for m in all_modules}

    def on_module_toggle(mod_name, value):
        if value:
            registry.enable_module(mod_name)
        else:
            registry.disable_module(mod_name)
        state.modules_state[mod_name] = value
        warnings = registry.validate_configuration()
        state.validation_warnings = warnings

    # 4. GUI Layout erstellen
    with SinglePageLayout(server) as layout:
        layout.title.set_text("CFDSolver — 3D Cockpit & Physics Control")

        with layout.toolbar:
            vuetify.VSpacer()
            vuetify.VSelect(
                v_model=("active_field", "Temperature"),
                items=(["Temperature", "Velocity", "Pressure", "Species_H2", "FilmHeight"],),
                label="Aktives Feld",
                dense=True,
                outlined=True,
                hide_details=True,
                classes="mx-2",
                style="max-width: 200px;",
            )
            vuetify.VBtn("Kamera Reset", click=ctrl.view_reset_camera, outlined=True, small=True)

        # Linke Seitenleiste: Physik-Module (Fluent-Style)
        with layout.drawer as drawer:
            drawer.width = 340
            vuetify.VCardTitle("Physikalische Modelle")
            vuetify.VDivider()

            with vuetify.VList(dense=True):
                for mod_name, mod in all_modules.items():
                    with vuetify.VListItem():
                        with vuetify.VListItemContent():
                            vuetify.VListItemTitle(mod_name)
                            vuetify.VListItemSubtitle(f"{mod.category} | {mod.config.model_variant}")
                        with vuetify.VListItemAction():
                            vuetify.VSwitch(
                                v_model=(f"modules_state['{mod_name}']", mod.config.enabled),
                                dense=True,
                                hide_details=True,
                                change=(on_module_toggle, f"['{mod_name}', $event]"),
                            )
                    vuetify.VDivider()

        # Zentraler Bereich: 3D-Ansichtsfenster (Trame VTK Viewport)
        with layout.content:
            with vuetify.VContainer(fluid=True, classes="fill-height pa-0"):
                html_view = vtk_widgets.VtkRemoteView(plotter.render_window)
                ctrl.view_update = html_view.update
                ctrl.view_reset_camera = html_view.reset_camera

    return server


if __name__ == "__main__":
    app_server = create_cockpit_app()
    app_server.start(port=8080)
