# Suggestions & Ideenpool: Frontend & 3D-Cockpit
> **Fokus:** Trame, PyVista, WebGL / WebGPU, UI/UX, Fluent-Style Controls & Interaktion.

---

## 1. 3D-Visualisierung & Viewport
* **Hybrid-Rendering (Client-Side vs. Server-Side):**
  * **Client-Side (VTK.js / WebGL):** Standard für Schaufeloberflächen, Stromlinien und Netze bis ca. 2 Millionen Dreiecke. Läuft lokal mit 60 FPS im Browser ohne Netzwerklatenz.
  * **Server-Side (GPU Remote View):** Umschaltbar für volumetrische 8M-Zellengitter, gerendert auf der Workstation-GPU und als komprimierter interaktiver H.264/JPEG-Stream übertragen.
* **Interaktive Schnittflächen (Dynamic Slicing Planes):**
  * Schieberegler für X-, Y- und Z-Schnitte mit Echtzeit-Kontur-Berechnung und kontinuierlicher Farbpalette (z. B. `turbo`, `plasma`, `coolwarm`).
* **3D-Partikel- und Stromlinien-Animation:**
  * Zeitabhängige Partikelspuren (Streaklines / Pathlines) entlang des Geschwindigkeitsvektors $\mathbf{U}$, animiert direkt im WebGL-Shader.

---

## 2. Fluent-Style UI/UX & Workflow
* **Interaktives "Models"-Baummenü:**
  * Aufbau analog zu Ansys Fluent mit Statusanzeige (z. B. grüner Punkt = aktiv, grauer Punkt = inaktiv).
  * Klick auf ein Modell öffnet ein modales Dialogfenster mit den Untereinstellungen (z. B. Klick auf *Viscous* öffnet $k-\omega$ SST Einstellungen).
* **Multi-View Dashboard (Splitscreen):**
  * Linker Quadrant: 3D-Geometrie & Schnittansicht.
  * Rechter oberer Quadrant: Live-Residuenkurven ($p, U, h$).
  * Rechter unterer Quadrant: 1D-Liniendiagramm entlang einer Schaufelkontur (z. B. Wandtemperatur $T_w$ über Sehnenlänge $x/c$).
* **Farb-Skalen & Einheiten-Umschalter:**
  * Schneller Wechsel zwischen Kelvin $[K]$ und Celsius $[^\circ\text{C}]$, sowie $\text{Pa}$ und $\text{bar}$.
