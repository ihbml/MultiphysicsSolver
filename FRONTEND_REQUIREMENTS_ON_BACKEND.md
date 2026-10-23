# Frontend-Anforderungen an das Backend
> **Zweck dieser Datei:**  
> In dieser Datei definiert die **Frontend-Instanz**, welche Eingabemöglichkeiten, Parameter, Schalter, Grenzwerte und Formate sie vom **Backend** benötigt, um das UI-Panel (Fluent-Style) interaktiv und fehlersicher aufzubauen.

---

## 1. Übersicht der gewünschten Modell-Parameter & Dialoge

Das Frontend plant für jedes aktivierte Physik-Modul ein entsprechendes Einstellungsfenster (Popup oder Sub-Panel) bereitzustellen. Dazu muss das Backend pro Modul ein Parameterschema bereitstellen:

### A. Viscous & Turbulence Modul
* **Auswahl des Modells (`model_variant`):**
  * `Inviscid` (Euler)
  * `Laminar`
  * `Spalart-Allmaras`
  * `k-epsilon` (Standard, RNG, Realizable)
  * `k-omega SST` (Standard, SAS)
  * `LES WALE` / `Smagorinsky`
* **Benötigte Eingabefelder vom Backend:**
  * Turbulenzintensität $I$ in $\%$ (Default: $5\%$, Min: $0.1\%$, Max: $50\%$)
  * Turbulentes Längenmaß $L$ in $\text{m}$ oder Viskositätsverhältnis $\mu_t / \mu$
  * Schalter: `Wall Functions on/off` (für $y^+ > 30$ vs. $y^+ \approx 1$)

### B. Species & Chemistry Modul
* **Auswahl des Reaktionsmodells:**
  * `Finite-Rate / Eddy-Dissipation`
  * `Wandkatalyse (Surface Heterogeneous Kinetics)`
  * `Inerte Mischung (Species Transport)`
* **Benötigte Eingabefelder:**
  * Pfad zum Chemkin- oder OpenFOAM-Reaktionsmechanismus (`reactions`-Datei)
  * Liste der beteiligten Spezies (z. B. `["CH4", "H2O", "CO", "H2", "CO2", "N2"]`)
  * Auswahl der katalytischen Zellzone (`cellZone`, z. B. `catalyticWashcoatZone`)

### C. Discrete Phase (DPM) Modul
* **Injektions-Definition:**
  * Injektions-Typ: `Surface`, `Point-Cone`, `Volume`
  * Partikeldurchmesser $d_p$: Verteilungsmodell (Rosin-Rammler, Normal, Monodispers)
  * Massenstrom der Partikel in $\text{kg/s}$
  * Partikeldichte $\rho_p$ in $\text{kg/m}^3$
  * Kopplung: `1-Way Coupled` (nur Strömung treibt Partikel) vs. `2-Way Coupled` (Partikel verändern Strömung)

### D. Radiation Modul
* **Strahlungsmodell:**
  * `P1` (einfach, für optisch dicke Medien)
  * `fvDOM` (Diskrete Ordinaten, für feine Winkelauflösung)
* **Parameter:**
  * Winkeldiskretisierung: $n_\phi$ (Azimut) und $n_\theta$ (Polar), typisch: $3 \times 3$ oder $4 \times 4$
  * Absorptionskoeffizient $a$ in $1/\text{m}$
  * Streukoeffizient $\sigma_s$ in $1/\text{m}$

### E. Battery Thermal Modul
* **Modelltyp:** `Equivalent Circuit Model (ECM 2-RC)`
* **Parameter:**
  * Nennkapazität in $\text{Ah}$ (z. B. 5.0 Ah)
  * Innenwiderstand $R_0$ in $\Omega$ (z. B. 0.015)
  * RC-Glieder: $R_1, C_1, R_2, C_2$
  * Entropiekoeffizient $\frac{dU}{dT}$ in $\text{V/K}$
  * Schalter für Thermal Runaway Kinetik (Arrhenius-Exothermie bei $T > 120\,^\circ\text{C}$)

### F. Eulerian Wall Film (EWF) Modul
* **Parameter:**
  * Filmdichte $\rho_f$ in $\text{kg/m}^3$ (Default: 1000)
  * Kinematische Viskosität $\nu_f$ in $\text{m}^2/\text{s}$
  * Verdampfungsmodell: `None`, `StandardEvaporation`
  * Kritische Ablöseschubspannung $\tau_{\text{crit}}$

---

## 2. Format-Vorgabe für das Backend-Schema

Damit das Frontend die UI-Elemente automatisch generieren kann, bittet das Frontend das Backend, für jedes Modul eine Methode `get_parameter_schema()` bereitzustellen:

```python
{
    "parameter_name": {
        "type": "float" | "int" | "select" | "bool" | "string",
        "label": "Lesbare Bezeichnung",
        "description": "Erklärung für Tooltip",
        "default": 1.0,
        "options": ["A", "B"],  # bei select
        "min": 0.0,             # bei float/int
        "max": 100.0,
        "unit": "K"
    }
}
```

---

## 3. Status-Abfragen & Simulationskontrolle

Das Frontend benötigt vom Backend folgende Rückmeldungen:
1. `get_mesh_info()`: Anzahl Zellen, Rand-Patches, Bounding-Box ($X_{\min}, X_{\max}$, etc.).
2. `get_residual_stream()`: Zeitstempel, Iteration und aktuelle Residuen für Plotting.
3. `cancel_simulation()`: Signal zum sauberen Stoppen von MPI-Prozessen.
