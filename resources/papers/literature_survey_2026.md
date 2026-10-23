# Wissenschaftlicher Literatur- und Architektur-Report (2024–2026)
> **Zweck:** Fundamentale mathematische Modelle, State-of-the-Art Publikationen (inkl. der lokalen Referenz-Bibliothek in [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers)) und Integrationspfade für die eigenständige Multi-Physik- & Optimierungs-Plattform.

---

## 1. Universelle Reaktionskinetik & Multi-Spezies-Transport
*Fokus: Beliebige Gas- & Flüssigphasenreaktionen, detaillierter Transport, Wandkatalyse (Washcoats)*

### Schlüsselpublikationen:
1. **F. Wang, L. An, T.-L. Chan (2025):**  
   *„An OpenFOAM solver incorporating detailed transport model for reacting flow simulations“*  
   *Computer Physics Communications*, Vol. 306, 109400. DOI: [10.1016/j.cpc.2024.109400](https://doi.org/10.1016/j.cpc.2024.109400)
2. **S. Raghav, B. H. Saracoglu (2025/2026):**  
   *„Bridging OpenFOAM and Cantera: A Runtime-Configurable Solver and Its Validation“*  
   *AIAA / ResearchGate*, DOI: [10.13140/RG.2.2.21734.95048](https://www.researchgate.net/publication/415340341)
3. **E. A. Daymo, M. Hettel et al. (2025):**  
   *„Fixed bed reactor modeling with internal and external transport resistances“*  
   *Meccanica*, 60, DOI: [10.1007/s11012-024-01764-8](https://doi.org/10.1007/s11012-024-01764-8)

### Mathematische Kern-Gleichungen:
* **Universeller Speziestransport (Maxwell-Stefan / Hirschfelder-Curtiss & Soret-Effekt):**
  $$\frac{\partial (\rho Y_i)}{\partial t} + \nabla \cdot (\rho \mathbf{u} Y_i) = \nabla \cdot \left(\rho D_{i,m} \nabla Y_i + D_{T,i} \frac{\nabla T}{T}\right) + \dot{\omega}_i^{hom} + \dot{\omega}_i^{het}$$
* **Porendiffusion im Washcoat (Bosanquet-Kombination aus Molekular- & Knudsen-Diffusion):**
  $$\frac{1}{D_{eff,i}} = \frac{\tau_p}{\varepsilon_p} \left( \frac{1}{D_{i,m}} + \frac{1}{D_{K,i}} \right), \quad D_{K,i} = \frac{d_{pore}}{3} \sqrt{\frac{8 R T}{\pi M_i}}$$
* **Generische Wand-Randbedingung (Robin-BC mit Thiele-Wirkungsgrad $\eta$):**
  $$-D_{eff,i} \left. \frac{\partial Y_i}{\partial n} \right|_{wall} = \eta \cdot M_i \sum_{r} \nu_{i,r} R_r^{intrinsic}(T_w, C_{w,j})$$

---

## 2. Eulerian Wall Film (EWF) & Phasenwechsel
*Fokus: Tiefengemittelte 2D-Grenzflächenströmung, Verdampfung, Kondensation & Sieden*

### Schlüsselpublikationen:
1. **Y. Yang et al. (2025):** *„Flow evolution and fingering instability of a thin water film under shear airflow“*, *Physics of Fluids*, 37(7), 074126. DOI: [10.1063/5.0215705](https://doi.org/10.1063/5.0215705)
2. **J. Leong et al. (2026):** *„Coupled LES–Lagrangian–Film Framework“*, *Advanced Inkjet Technology*, 2(1), 6. DOI: [10.2352/adv.inkjet.technol.2026.2.1.6](https://doi.org/10.2352/adv.inkjet.technol.2026.2.1.6)

### Mathematische Kern-Gleichungen:
* **2D-Kontinuität auf Wand-Manifolds (Finite Area Method `faMesh`):**
  $$\frac{\partial (\rho_f \delta_f)}{\partial t} + \nabla_s \cdot (\rho_f \delta_f \bar{\mathbf{u}}_f) = \dot{m}_{dep} - \dot{m}_{evap} + \dot{m}_{cond}$$
* **Tiefengemittelter Impuls mit Wandschubspannung & Marangoni-Effekt:**
  $$\frac{\partial (\rho_f \delta_f \bar{\mathbf{u}}_f)}{\partial t} + \nabla_s \cdot (\rho_f \delta_f \bar{\mathbf{u}}_f \bar{\mathbf{u}}_f) = -\delta_f \nabla_s p_f + \boldsymbol{\tau}_{gas} - \frac{3 \mu_f}{\delta_f} \bar{\mathbf{u}}_f + \nabla_s \sigma + \rho_f \delta_f \mathbf{g}_{\tau}$$

---

## 3. Lagrange-Partikelverfolgung (DPM) & Spray-Wand-Kollision
*Fokus: Two-Way-Kopplung, Tropfenzerstäubung, Bai-Gosman/Kuhnke Wandregime*

### Schlüsselpublikationen:
1. **B. Li, X. Wang et al. (2026):** *„Coupled VOF–DPM numerical simulation of pressure-swirl sprays“*, *Chem. Eng. Sci.*, 308, 121532. DOI: [10.1016/j.ces.2026.121532](https://doi.org/10.1016/j.ces.2026.121532)
2. **H. P. Lien et al. (2025):** *„Spray-wall Interactions under Flash-boiling Conditions“*, *Int. J. Multiphase Flow*, 185, 105156. DOI: [10.1016/j.ijmultiphaseflow.2025.105156](https://doi.org/10.1016/j.ijmultiphaseflow.2025.105156)

### Mathematische Kern-Gleichungen:
* **Kollisionsregime nach Bai-Gosman / Kuhnke ($K = We \cdot Oh^{-0.4}$):**
  - **Stick:** Tropfen deponiert vollständig in den Wandfilm ($\dot{m}_{dep}$).
  - **Rebound ($T_w > T_{Leidenfrost}$):** Elastisches Abprallen am Dampfpolster.
  - **Splash ($K > K_{crit} \approx 2600 \dots 3200$):** Aufplatzen in Sekundärtröpfchen + Teilfilm-Deposition.

---

## 4. Fortgeschrittene Multiphysik-Kopplungen
*Fokus: Strahlung (fvDOM), CHT, Ablation & TPS, Batterie-Elektrochemie, MHD, Aeroakustik*

### Schlüsselpublikationen:
1. **Reaktive Wandablation & Hitzeschutz:**  
   - **J. Zhang et al. (2026):** *„Moving ablation interfaces with Darcy-Brinkman-Stokes porous medium“*, *Adv. Aerodyn.*, 8, 257. DOI: [10.1186/s42774-026-00257-4](https://doi.org/10.1186/s42774-026-00257-4)
   - **H. Liang et al. (2026):** *„radAblationFoam: Coupled ablation with in-depth radiation“*, *Appl. Therm. Eng.*, 297, 130915.
2. **Batterie-Thermische Simulation (ECM & Thermal Runaway):**  
   - **Q. Chen et al. (2025):** *„High-performance multiphysics simulation framework for lithium-ion batteries“*, *J. Energy Storage*, 108, 115200.
3. **Magnetohydrodynamik (MHD):**  
   - **S. Siriano et al. (2024):** *„Multi-region and multiphase MHD OpenFOAM solver“*, *Fusion Eng. Des.*, 201, 114240.
4. **Aeroakustik (FWH / Curle):**  
   - **S. Sharma (2026):** *„fwhFoam: Ffowcs Williams--Hawkings solver with on-surface acoustic localization“*, *AIAA Aeroacoustics 2026*, SSRN: 7499668.

---

## 5. High-Performance Computing (C++20, ISO-Parallelismus, GPU & Rust)

### Schlüsselpublikationen:
1. **S. Bnà et al. (2026):** *„Spuma: a minimally invasive approach to the GPU porting of OpenFOAM“*, *Comp. Phys. Comm.*, 2026.
2. **M. Kumar et al. (2026):** *„Cross-platform GPU Implementation of OpenFOAM Using Only ISO C++ standard parallelism“*, *ACM PASC '26*, DOI: [10.1145/3815572.3815755](https://doi.org/10.1145/3815572.3815755)
3. **G. Olenik, H. Anzt et al. (2025):** *„Platform-portable linear algebra backend for OpenFOAM (Ginkgo)“*, *Meccanica*, 60, DOI: [10.1007/s11012-024-01806-1](https://doi.org/10.1007/s11012-024-01806-1)
4. **J. W. Gärtner et al. (2024):** *„Dynamic load balancing for chemistry in OpenFOAM (DLBFoam)“*, *Comp. Phys. Comm.*, 299, 109151.
5. **L. Lombardo & F. Durastante (2026):** *„Evaluating Rust for Sparse Matrix Kernels in Scientific Computing“*, arXiv: [2606.19213](https://arxiv.org/abs/2606.19213).

---

## 6. Graph-Based Differentiable FVM & Adaptive SPH-FVM Kopplung (Lokale Referenz-Paper)
*Fokus: Vektorisierte FVM-Operatoren auf unstrukturierten Gittern, End-to-End Differenzierbarkeit und adaptive Partikel-Gitter-Kopplung*

### Lokale Referenz-Publikationen in [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers):
1. **[`Meshing_FVM_DiFVM_2603.15920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_FVM_DiFVM_2603.15920v1.pdf) — P. Du, Y. Li, M. Xu, J.-X. Wang (März 2026):**  
   *„DiFVM: A Vectorized Graph-Based Finite Volume Solver for Differentiable CFD on Unstructured Meshes“*, arXiv:2603.15920v1.
2. **[`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf) — Z. Wang, O. J. Haidn, X. Hu (TU München, 2023/2024):**  
   *„Incorporating the algorithm for the boundary condition from FVM into the framework of Eulerian SPH“*, arXiv:2312.03542v1.
3. **[`MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf) — Y.-D. Zhang, F.-R. Ming, P.-P. Wang, D.-F. Liang (2025):**  
   *„An adaptive SPH-FVM method with optimized particle-mesh interpolation scheme for strongly compressible multiphase flows“*, *Int. J. Multiphase Flow*, Vol. 189, 105256.

### Mathematische & Algorithmische Kern-Bausteine für unseren Solver:
* **Isomorphie zwischen unstrukturiertem FVM und Graph Message-Passing (DiFVM):**
  Das unstrukturierte Polyeder-Gitter wird als statischer gerichteter Graph $\mathcal{G} = (\mathcal{V}, \mathcal{E})$ mit Knoten $v_i$ (Zellzentren) und gerichteten Kanten $e_{ji}$ (Flächen von Nachbar $j$ zu Owner $i$ mit statischen Attributen $\mathbf{g}_{ji} = (\mathbf{S}_f, \mathbf{x}_f)$) dargestellt. Jede FVM-Bilanz reduziert sich auf zwei verlustfreie, 100 % vektorisierbare **Gather-Compute-Scatter**-Kerne:
  1. *Edge Message Construction (parallel über alle $|\mathcal{E}|$ Flächen):*
     $$\mathbf{m}_{ji} = \mathcal{M}(\mathbf{u}_i, \mathbf{u}_j, \mathbf{g}_{ji}) = -\dot{m}_f \mathbf{u}_f + \nu \left( |\boldsymbol{\Delta}_f| \frac{\mathbf{u}_j - \mathbf{u}_i}{|\mathbf{d}_{ij}|} + (\nabla \mathbf{u})_f \cdot \mathbf{k}_f \right) - \frac{1}{\rho} p_f \mathbf{S}_f$$
     mit **Over-Relaxed Non-Orthogonality Correction** $\boldsymbol{\Delta}_f = \frac{|\mathbf{S}_f|}{|\hat{\mathbf{S}}_f \cdot \hat{\mathbf{d}}_{ij}|} \hat{\mathbf{d}}_{ij}$ und $\mathbf{k}_f = \mathbf{S}_f - \boldsymbol{\Delta}_f$ sowie **Deferred Correction** für Verfahren höherer Ordnung (SOU / QUICK): $\dot{m}_f \phi_f^{HO} = \dot{m}_f \phi_f^U + \dot{m}_f (\phi_f^{HO} - \phi_f^U)_{\text{explicit}}$.
  2. *Flux Aggregation (Parallel Scatter-Add über `owner`/`neighbour`):*
     $$\mathbf{R}_i = \sum_{j \in \mathcal{N}(i)} \sigma_{ij} \mathbf{m}_{ji}$$
* **Implizite Differenzierung (Implicit Function Theorem) für den Druck-Poisson-Löser (DiFVM Gl. 25):**
  Anstatt tausende Iterationen des linearen CG/BiCGStab/AMG-Drucklösers für Automatic Differentiation im RAM auszurollen, wird die Adjungierte der konvergierten Druckgleichung $\mathcal{F}(p^*, \mathbf{u}^*) = 0$ exakt über das implizite Funktionentheorem gelöst:
  $$\frac{\partial p^*}{\partial \mathbf{u}^*} = -\left( \frac{\partial \mathcal{F}}{\partial p^*} \right)^{-1} \frac{\partial \mathcal{F}}{\partial \mathbf{u}^*} \quad \Longleftrightarrow \quad \left(\frac{\partial \mathcal{F}}{\partial p^*}\right)^T \boldsymbol{\lambda} = \left(\frac{\partial \mathcal{L}}{\partial p^*}\right)^T$$
  Kombiniert mit **Binomial Gradient Checkpointing** sinkt der Speicherbedarf bei transienter Rückwärts-Differenzierung auf $\mathcal{O}(\sqrt{n_{\text{steps}}} \cdot N_c)$.
* **Konsistente FVM-SPH Rand- & Interface-Kopplung (Wang et al. & Zhang et al.):**
  * **Nullte-Ordnung-Konsistenz am Rand (Eulerian SPH $\leftrightarrow$ FVM):** Für Randpartikel mit unvollständigem Nachbarschaftsträger ($\nabla \cdot \mathbf{r}_i = \sum_j V_j \mathbf{r}_{ij} \cdot \nabla W_{ij} < 0.75\,d$) wird der fehlende Oberflächenvektor exakt wie eine FVM-Randfläche geschlossen:
    $$\mathbf{A}_{iS}^{ESPH} = -\sum_{P} V_P \nabla W_{iP} \quad \Longrightarrow \quad \mathbf{A}_{iS}^{ESPH} + \sum_{P} \mathbf{A}_{iP}^{ESPH} = \mathbf{0}$$
  * **Optimized Particle-Mesh Interpolation Scheme (OPMIS):** Korrigierter Kernel-Gradient $\langle \nabla_i W_{ij} \rangle^{\mathbf{B}} = \mathbf{B}_i \nabla_i W_{ij}$ mit Renormierungsmatrix $\mathbf{B}_i = \left[\sum_j (\mathbf{x}_i - \mathbf{x}_j) \otimes \nabla_i W_{ij} V_j\right]^{-1}$ und Taylor-Rekonstruktion $\phi_o^m = \frac{1}{N} \sum_{i=1}^N \left(\phi_i^p + (\mathbf{x}_o^m - \mathbf{x}_i^p) \cdot \nabla \phi_i^p\right)$ verhindert Druckwellen-Oszillationen am Übergang zwischen SPH-Kernzone ($P_{com}, P_{trn}, P_{imt}$) und FVM-Gitter ($M_{slp}, M_{imt}, M_{com}$).

---

## 7. Topologieoptimierung (SIMP, Level-Set), Robuste Optimierung (FOSM) & Globale/Lokale Hybrid-Suche
*Fokus: Gradientenbasierte Thermo-Fluid-Topologieoptimierung, Unsicherheitsquantifizierung ohne Monte-Carlo-Explosion und mehrstufige Parameteroptimierung*

### Lokale Referenz-Publikationen in [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers):
1. **[`thesis_Elisabetta_Chimisso.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/thesis_Elisabetta_Chimisso.pdf) — E. Chimisso (Politecnico di Milano):**  
   *Thermo-Fluid Topology Optimization, Brinkman Penalization, Algorithmic Differentiation & Deep Learning Surrogates*.
2. **[`ArbitraryFunctionOptimization_s00158-023-03540-w.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ArbitraryFunctionOptimization_s00158-023-03540-w.pdf) — M. Kranz, J. K. Lüdeker, B. Kriegesmann (TU Hamburg, 2023):**  
   *„A generalized approach for robust topology optimization using the first-order second-moment method for arbitrary response functions“*, *Struct. Multidiscip. Optim.*, 66:98. DOI: [10.1007/s00158-023-03540-w](https://doi.org/10.1007/s00158-023-03540-w).
3. **[`Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf) — K. A. James, J. R. R. A. Martins (2010):**  
   *„Topology Optimization Using a Level Set Method with an Arbitrary Structured Mesh“*, *AIAA 2010-2842*.
4. **[`optimization-curved-fir-tree-turbine-15.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/optimization-curved-fir-tree-turbine-15.pdf) — S. Tan et al. (Shanghai Turbine Works):**  
   *„Optimization of Curved Fir-Tree Root of Steam Turbine Using Computer Aided Optimization and Finite Element Analysis“* (MIGA + Downhill Simplex).

### Mathematische & Algorithmische Kern-Bausteine für `backend/optimization/`:
* **Dreistufige Dichte-Filterung & RAMP-Interpolation für SIMP (Chimisso & Kranz et al.):**
  1. *Helmholtz-/Nachbarschaftsfilterung:* $\tilde{\gamma}_e$ verhindert Schachbrett-Artefakte (Checkerboarding) und Gitterabhängigkeit.
  2. *Glatte Heaviside-Projektion (Wang et al. / Kranz Gl. 17):* Erzwingt scharfe $0/1$-Konturen gegen Graustufen-Zwischenwerte:
     $$\bar{\gamma}_e = \frac{\tanh(\beta \eta) + \tanh(\beta(\tilde{\gamma}_e - \eta))}{\tanh(\beta \eta) + \tanh(\beta(1 - \eta))}$$
  3. *RAMP-Interpolation (Stolpe & Svanberg, Kranz Gl. 26):* Besitzt im Gegensatz zu reinem SIMP ($\bar{\gamma}^p$) eine nicht-verschwindende Ableitung bei $\bar{\gamma}_e = 0$, was für Strömungs-Brinkman-Terme und thermische Spannungen entscheidend ist:
     $$E_e(\bar{\gamma}_e) = E_{\min} + (E_0 - E_{\min}) \frac{\bar{\gamma}_e}{1 + p(1 - \bar{\gamma}_e)}$$
* **Kreisselmeier-Steinhauser (KS) Aggregation für lokale Maxima (Kranz Gl. 27 & 63–65):**
  Da $\max_e(T_e)$ (Spitzentemperatur) oder $\max_e(\sigma_{\text{VM},e})$ (Maximalspannung) nicht differenzierbar sind, wird das Maximum glatt und differenzierbar über die obere KS-Schranke aggregiert:
  $$q^{KS} = q_{\max} + \frac{1}{\gamma_{KS}} \ln \left[ \sum_{e=1}^{n_e} \exp\left(\gamma_{KS}(q_e - q_{\max})\right) \right], \quad \frac{\partial q^{KS}}{\partial x} = \frac{\sum_e \exp(\gamma_{KS}(q_e - q_{\max})) \frac{\partial q_e}{\partial x}}{\sum_e \exp(\gamma_{KS}(q_e - q_{\max}))}$$
* **Generalisierte FOSM (First-Order Second-Moment) für Robust Design Optimization (Kranz Gl. 4–16):**
  Statt tausender Monte-Carlo-Samples pro Topologie-Iteration approximiert FOSM Mittelwert und Varianz bezüglich eines Zufallsvektors $\boldsymbol{\alpha}$ (Kovarianzmatrix $\boldsymbol{\Sigma}_{\alpha}$) analytisch:
  $$\Phi_g = \mu_g + \kappa \sigma_g, \quad \mu_g \approx g(\boldsymbol{\mu}_{\alpha}), \quad \sigma_g^2 \approx \left(\frac{\mathrm{D}g}{\mathrm{D}\boldsymbol{\alpha}}\right)^T \boldsymbol{\Sigma}_{\alpha} \left(\frac{\mathrm{D}g}{\mathrm{D}\boldsymbol{\alpha}}\right), \quad \frac{\mathrm{D}g}{\mathrm{D}\boldsymbol{\alpha}} = \frac{\partial g}{\partial \boldsymbol{\alpha}} - \boldsymbol{\lambda}_g^T \frac{\partial \mathbf{R}}{\partial \boldsymbol{\alpha}}$$
  Über das gekoppelte Adjungierten-System $(\boldsymbol{\lambda}_u, \boldsymbol{\lambda}_{\alpha})$ (Gleichung 15) erfordert die komplette Varianz-Sensitivität $\frac{\mathrm{D}\sigma_g^2}{\mathrm{D}\mathbf{v}}$ **maximal 2 zusätzliche Adjoint-Lösungen** – völlig unabhängig davon, ob 10 oder 1.000.000 unsichere Parameter vorliegen!
* **Level-Set-Oberflächenschärfung via Jacobi-Transformation (James & Martins Gl. 6–14):**
  Für glatte, direkt fertigbare CAD-Wandgrenzen $\partial \Omega = \{\mathbf{x} \mid \psi(\mathbf{x}) = 0\}$ wird die Hamilton-Jacobi-Gleichung $\frac{\partial_T \psi}{\partial t} + V_T |\nabla_T \psi| = 0$ über die Jacobi-Matrix $\mathbf{J}(\xi, \eta, \zeta) = \frac{\partial(x,y,z)}{\partial(\xi,\eta,\zeta)}$ direkt im zellzentrierten Gitter gelöst (mit Fast-Sweeping Eikonal-Reinitialisierung $|\nabla \psi| - 1 = 0$).
* **Zweistufige Globale + Lokale Parameter-Optimierung (Tan et al.):**
  Kombination aus **Multi-Island Genetic Algorithm (MIGA) / Particle Swarm (PSO)** (globale Exploration mit Sub-Populationen und periodischer Migration gegen lokale Minima) und **Downhill Simplex (Nelder-Mead) / L-BFGS** (schnelle lokale Konvergenz).

---

## 8. Mikroskopische Fundierung & Quantum CFD / Quantum Optimization (Future Horizon)
*Fokus: Brücke zwischen Quantenmechanik, Kinetischer Gastheorie und Makro-CFD sowie Ausführung auf Quanten-Prozessoren (Google Willow, Microsoft Majorana 1, IBM Nighthawk)*

### 1. Skalen-Hierarchie (Bottom-Up Information Reduction):
$$\text{Schrödinger / Born-Oppenheimer (PES)} \;\longrightarrow\; \text{Molekulardynamik (MD)} \;\longrightarrow\; \text{Boltzmann-Gleichung } f(\mathbf{x},\mathbf{v},t) \;\longrightarrow\; \text{Navier-Stokes (FVM)}$$
* **Kinetische Theorie & Transportkoeffizienten:** Aus den quantenmechanischen Ab-initio-Potentialflächen (z. B. Parker-Rotations-/Vibrationsrelaxation, Valentini $\text{N}_2$-Dissoziation, Variable Hard Sphere VHS) folgen direkt die makroskopischen Viskositäts-, Wärmeleitungs- und Diffusionskoeffizienten sowie chemische Reaktionsraten unseres Solvers.

### 2. Mathematische Isomorphie: Madelung-Transformation & Hydrodynamic Schrödinger Equation
Setzt man die quantenmechanische Wellenfunktion $\psi(\mathbf{x}, t) = \sqrt{\rho(\mathbf{x}, t)} \, \exp\left(\frac{i S(\mathbf{x}, t)}{\hbar}\right)$ mit $\mathbf{u} = \frac{1}{m}\nabla S$ in die zeitabhängige Schrödinger-Gleichung $i\hbar \frac{\partial \psi}{\partial t} = \left(-\frac{\hbar^2}{2m}\nabla^2 + V\right)\psi$ ein, so zerfällt sie exakt in ein kompressibles hydrodynamisches Euler-System:
$$\frac{\partial \rho}{\partial t} + \nabla \cdot (\rho \mathbf{u}) = 0$$
$$\frac{\partial \mathbf{u}}{\partial t} + (\mathbf{u} \cdot \nabla)\mathbf{u} = -\frac{1}{m} \nabla (V + Q), \quad Q = -\frac{\hbar^2}{2m} \frac{\nabla^2 \sqrt{\rho}}{\sqrt{\rho}} \;\text{(Bohm-Quantendruck)}$$
Umgekehrt erlaubt die **Hydrodynamic Schrödinger Equation (Meng & Yang)** sowie die **Carleman-Linearisierung** und das **Quantum Lattice Boltzmann Method (QLBM)**, klassische Wirbelströmungen in unitäre, lineare Operatoren $\mathbf{U} = \exp(-i \mathbf{H} \Delta t)$ zu übersetzen, die nativ auf Quanten-Gates laufen.

### 3. Hardware-Roadmapping für QPU-Ko-Prozessoren:
1. **IBM Nighthawk / Heron (Kurz- bis Mittelfristig — Hybrid Quantum-Centric Supercomputing):**
   * **QUBO / QAOA für binäre $0/1$-Topologieoptimierung:** Direkte Lösung des diskreten Materialverteilungsproblems $\gamma_i \in \{0, 1\}$ als quadratisches binäres Optimierungsproblem ($\min_{\boldsymbol{\gamma} \in \{0,1\}^N} \boldsymbol{\gamma}^T \mathbf{Q} \boldsymbol{\gamma}$) ohne Graustufen-Relaxation.
   * **VQE (Variational Quantum Eigensolver):** Berechnung exakter elektronischer Aktivierungsbarrieren $E_a$ und Potentialflächen für das `species_chem`-Kinetikmodul.
2. **Google Willow & Nachfolger (Mittel- bis Langfristig — Surface-Code Fault-Tolerant QPUs):**
   * **VQLS / HHL Quantum Linear Solver:** Beschleunigung unseres rechenintensivsten FVM-Schritts – der dünnbesetzten Druck-Poisson-Gleichung $\mathbf{A}\mathbf{p} = \mathbf{b}$ (Graph Laplacian) – über Quanten-Phasenschätzung / Block-Encoding in $\mathcal{O}(\text{polylog}(N_c) \cdot \kappa)$.
3. **Microsoft Majorana 1 & Topologische QPUs (Langfristig — Skalierung auf $10^5$–$10^6$ logische Qubits):**
   * Vollständige 3D **Quantum Lattice Boltzmann (QLBM)** und **Carleman-linearisierte Navier-Stokes**-Zeitschrittintegration auf fehlertoleranten topologischen Majorana-Arrays.
