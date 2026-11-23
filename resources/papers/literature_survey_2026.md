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
1. **S. Bnà et al. (2026):** *„Spuma: a minimally invasive approach to the GPU porting of OpenFOAM“*, *Comp. Phys. Comm.*, 2026, `arXiv:2512.22215`.
2. **M. Kumar et al. (2026):** *„Cross-platform GPU Implementation of OpenFOAM Using Only ISO C++ standard parallelism“*, *ACM PASC '26*, DOI: [10.1145/3815572.3815755](https://doi.org/10.1145/3815572.3815755)
3. **G. Olenik, H. Anzt et al. (2025):** *„Platform-portable linear algebra backend for OpenFOAM (Ginkgo)“*, *Meccanica*, 60, DOI: [10.1007/s11012-024-01806-1](https://doi.org/10.1007/s11012-024-01806-1)
4. **J. W. Gärtner et al. (2024):** *„Dynamic load balancing for chemistry in OpenFOAM (DLBFoam)“*, *Comp. Phys. Comm.*, 299, 109151.
5. **L. Lombardo & F. Durastante (2026):** *„Evaluating Rust for Sparse Matrix Kernels in Scientific Computing“*, arXiv: [2606.19213](https://arxiv.org/abs/2606.19213).

### Architektonische Konsequenzen für unseren Memory-Pool & Linearen Gleichungslöser:
* **Block-CSR (`BAIJ` $4\times 4$) in C++20 vs. Standard-CSR/CSC in Rust (Lombardo & Durastante 2026):**
  Während nativer Rust-Code bei skalaren CSR/CSC-Matrizen mit C++ (`Eigen`) gleichzieht, fällt er bei **geblockten CSR-Formaten (`BAIJ` / `BSR`)** gegenüber spezialisierten C++-Kernen (`PETSc` / `Ginkgo` / `Kokkos`) zurück. Da unsere gekoppelten MRF-, Zweiphasen- und Monolithik-Solver (**Kap. 9.18 & 9.19**) intensiv auf dichten **$4\times 4$-Blöcken $(u, v, w, p)$** (`alignas(64) Block4x4`, 1 Spaltenindex pro 16 `double`-Einträge $\implies 75\,\%$ weniger Index-Bandbreite) arbeiten, wird der lineare Block-Gleichungslöser zwingend in **C++20 (mit AVX2/AVX-512 $4\times 4$-Mikrokernen bzw. CUDA/Ginkgo)** ausgeführt, während Rust Orchestrierung, I/O (`.mphys`, `foam_fast_io`) und Networking übernimmt.
* **GPU-vs.-CPU Crossover-Schwelle für den Memory-Pool & Auto-Tuner (SPUMA, Bnà et al. 2026):**
  Aufgrund von Kernel-Launch-, Reduktions- und Speicherpool-Overheads schlägt eine moderne Rechenzentrums-GPU (A100/H100) einen vielkernigen CPU-Knoten bei unstrukturierten FVM-Gittern erst ab ca. **8–10 Mio. Zellen pro GPU** deutlich (bzw. ab ~1–2 Mio. Zellen gegenüber einer Workstation-CPU). Unser `hardware_profile.json` Auto-Tuner hält daher kleine und mittlere Gitter latenzfrei im 28-Thread C++20/OpenMP-CPU-Pfad und schaltet erst oberhalb der empirischen Crossover-Schwelle in den GPU-Device-/Unified-Memory-Pool.

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

---

## 9. Next-Generation Industrial CFD Meshing: Vollständiges Kompendium der 14 Referenz-Paper (inkl. Formeln & Code-Snippets)
*Fokus: Produktionsreife 3D-Fluidgebietsvernetzung, ungeclippte VoroCrust-Polyeder, ANSYS Mosaic Poly-Hexcore & PUMA, Dirty-CAD Enveloping (`fTetWild`), WSRD Cut-Cells, Point-Cloud Wall Shear Stress, Anisotrope Metrik-/Adjoint-Adaption, Kollisionsfreie Grenzschicht-Extrusion und Differenzierbares Moving-Mesh (`TINE`, `JamTet`, `UM2N`, `VoroMesh`)*

---

### 9.1 [`VoroCrust_1902.08767v3.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroCrust_1902.08767v3.pdf) — Ungeclipptes Voronoi-Meshing mit exakter Primal-Dual-Orthogonalität
* **Autoren & Quelle:** A. Abdelkader, C. L. Bajaj, M. S. Ebeida, A. H. Mahmoud, S. A. Mitchell, J. D. Owens, A. A. Rushdi (2020 / rev. Nov. 2023), *„VoroCrust: Voronoi Meshing Without Clipping“*, *ACM Trans. Graph. (TOG)* 39(3), Art. 23, `arXiv:1902.08767v3`, DOI: `10.1145/3337680`.
* **Kernproblem & Versagen von Standardmethoden:**
  Klassische *Restricted Voronoi Diagrams (RVD)* schneiden innere Voronoi-Zellen an der CAD-Wand ab (*Clipping*). An konkaven Kanten entstehen dadurch nicht-konvexe Zellen (3 %–96 % der Randzellen), winzige Splitter-Facetten und ein Bruch der **Primal-Dual-Orthogonalität** ($\mathbf{d}_f = \mathbf{s}_j - \mathbf{s}_i \not\parallel \mathbf{S}_f$).
* **Mathematische Kern-Gleichungen (Abschnitt 2 & Appendix A):**
  1. **Die 4 Kugel-Bedingungen (C1–C4) auf der Oberfläche $\mathcal{T}$:**
     Für Kugelzentren $p \in \mathcal{T}$ mit Radius $r_p$, Lipschitz-Konstante $L = 0.25$ und Deep-Coverage-Parameter $\alpha = 1 - \frac{\sqrt{3}}{2} \approx 0.13$:
     $$r_p \le r_q + L \cdot \|p - q\| \quad \text{(C3: } L\text{-Lipschitz)}, \qquad \|p - q\| \ge (1 - \alpha)\max(r_p, r_q) \quad \text{(C4: Poisson-Disk)}$$
     Initialisierung & Schrumpfung gegen das nächste nicht-koglatte Oberflächen-Feature $q^* \in \mathcal{T}$ (Knickwinkel $\ge \theta^\sharp$):
     $$r_p = \min\!\big(sz(p),\, 0.49\,\|p - q^*\|,\, r_q + L\,\|p - q\|\big)$$
  2. **Gespiegelte Saatpunkt-Paare $(g^\uparrow, g^\downarrow) \equiv (\mathbf{s}^+, \mathbf{s}^-)$ aus Kugel-Tripeln (Abschnitt 2.4):**
     Für jedes Tripel überlappender Kugeln $(b_p, b_q, b_w)$ schneiden sich die drei Sphären $\partial b_p \cap \partial b_q \cap \partial b_w$ in genau zwei Punkten $\{g^\uparrow, g^\downarrow\}$ beiderseits des Oberflächendreiecks $(p, q, w)$:
     $$\|g^{\uparrow,\downarrow} - p\|^2 = r_p^2, \quad \|g^{\uparrow,\downarrow} - q\|^2 = r_q^2, \quad \|g^{\uparrow,\downarrow} - w\|^2 = r_w^2$$
     Ein Saatpunkt $g \in \{g^\uparrow, g^\downarrow\}$ wird akzeptiert, wenn er von keiner vierten Kugel $b_u$ überdeckt wird ($\|g - u\| \ge r_u$). Die gemeinsame Voronoi-Facette von $(g^\uparrow, g^\downarrow)$ liegt exakt auf dem Oberflächendreieck $(p, q, w)$ und erfüllt **exakt** $\mathbf{k}_f \equiv \mathbf{0}$!
  3. **Sliver-Elimination durch Halbüberdeckungs-Detektion (Abschnitt 2.4, S. 8–9):**
     Wird bei einem Paar $(g^\uparrow, g^\downarrow)$ genau *einer* der beiden Punkte von einer vierten Kugel $b_q$ überdeckt ($\|q - g^\downarrow\| < r_q$, aber $\|q - g^\uparrow\| \ge r_q$), entsteht ein flacher Sliver-Tetraeder. VoroCrust schrumpft die verletzende Kugel exakt auf:
     $$r_q^{\text{new}} = \|q - g^\downarrow\|, \qquad \Delta = \frac{r_q - \|q - g^\downarrow\|}{r_q} \le \frac{\|q - g^\uparrow\|}{\|q - g^\downarrow\|} - 1$$
  4. **Lokale $k$-d-Tree Suchschranke & Volumen-Grading (Appendix A.3–A.5):**
     $$\|p - q\| \le \frac{2}{1 - L} r_p, \qquad r_z = r_s + L \cdot \|z - s\| \quad (\text{für innere Saatpunkte } z \in \mathcal{S}^{\downarrow\downarrow})$$
* **Referenz-Snippet für `backend/meshing/`:**
  ```python
  def compute_vorocrust_seed_pair(p1: np.ndarray, r1: float, p2: np.ndarray, r2: float, p3: np.ndarray, r3: float):
      """Exakte Schnittpunkte (s_plus, s_minus) dreier VoroCrust-Oberflächenkugeln (Sec. 2.4)."""
      e1 = p2 - p1
      d = np.linalg.norm(e1)
      if d < 1e-12 or d > r1 + r2 or d < abs(r1 - r2):
          return None
      e1 /= d
      i = np.dot(e1, p3 - p1)
      e2_vec = (p3 - p1) - i * e1
      j = np.linalg.norm(e2_vec)
      if j < 1e-12:
          return None
      e2 = e2_vec / j
      e3 = np.cross(e1, e2)
      x = (r1**2 - r2**2 + d**2) / (2.0 * d)
      y = (r1**2 - r3**2 + i**2 + j**2 - 2.0 * i * x) / (2.0 * j)
      z_sq = r1**2 - x**2 - y**2
      if z_sq <= 1e-12:
          return None
      z = np.sqrt(z_sq)
      base = p1 + x * e1 + y * e2
      return base + z * e3, base - z * e3
  ```
* **Wichtige Integrationsregel für allgemeine Polygon-Facetten & Power-Diagramme (Kap. 9.11 & 9.12):**
  Während die gemeinsame Voronoi-Facette eines gespiegelten Saatpunkt-Paares $(g^\uparrow, g^\downarrow)$ exakt planar und orthogonal ist ($\mathbf{k}_f \equiv \mathbf{0}$), entstehen an Übergängen unterschiedlich großer Kugeln (gewichtete Power-/Laguerre-Diagramme in Kap. 9.11) sowie in Poly-Hexcore-/ALE-Übergangszellen allgemeine $m$-Eck-Polygone. Im C++-Mesher (`octree_hexcore.cpp`, `difvm_mesh.cpp`) werden Flächenvektor $\mathbf{S}_f$ und Flächenschwerpunkt $\mathbf{x}_f$ deshalb **immer über eine konsistente Zerlegung in Sub-Dreiecke (Triangle Fan) um den arithmetischen Knotenschwerpunkt $\overline{\mathbf{x}}_f = \frac{1}{m}\sum_{k=0}^{m-1}\mathbf{x}_{v_k}$** berechnet:
  $$\mathbf{S}_f = \frac{1}{2}\sum_{k=0}^{m-1} (\mathbf{x}_{v_k} - \overline{\mathbf{x}}_f)\times(\mathbf{x}_{v_{k+1}} - \overline{\mathbf{x}}_f), \qquad \mathbf{x}_f = \frac{\sum_{k=0}^{m-1} \|\mathbf{S}_{f,k}\|\,\frac{1}{3}(\overline{\mathbf{x}}_f + \mathbf{x}_{v_k} + \mathbf{x}_{v_{k+1}})}{\sum_{k=0}^{m-1} \|\mathbf{S}_{f,k}\|}$$
  Dadurch heben sich nach dem diskreten Satz von Stokes die orientierten Kantenbeiträge jedes geschlossenen Polyeders teleskopartig auf und garantieren **maschinengenaue Divergenzfreiheit $\sum_{f \in \partial\Omega_i} \mathbf{S}_f = \mathbf{0}$ ($\le 10^{-16}$)**.

---

### 9.2 [`NotDownloadable/1 Ansys ICEM CFD Meshing Adaptation/`](file:///G:/Datein/Projects/CFDSolver/resources/papers/NotDownloadable/1%20Ansys%20ICEM%20CFD%20Meshing%20Adaptation) — ANSYS Mosaic Poly-Hexcore & PUMA Adaptation
* **Autoren & Quelle:** C. Aliaga, J. Selvanayagam, K. Zore, L. Zori, B. Makarov (2026), *„Reynolds-Averaged Navier–Stokes Mesh Adaptation Simulations for High-Lift Configuration Aircraft“*, *AIAA Journal of Aircraft* (HLPW-5), DOI: `10.2514/1.C038661`.
* **Kern-Architektur (Mosaic Poly-Hexcore + PUMA + FMG):**
  1. **3-Zonen Mosaic Poly-Hexcore Topologie:**
     - **Wand-Grenzschicht:** Orthogonale Poly-Prismen ($y^+ \le 0.5\dots 1.0$, Wachstumsrate $r \in [1.10, 1.25]$).
     - **Fernfeld / Bulk:** Achsenparalleles Kartesisches **Octree-Hexcore** ($\theta_f = 0^\circ$, minimale numerische Diffusion).
     - **Mosaic-Übergangsschicht:** Eine einzige Schicht allgemeiner **konvexer Polyeder** verbindet die Prismen-Deckfläche und 2:1-Octree-Sprünge **100 % flächenkonform ohne Hanging Nodes** (ein 2:1 geteiltes Quad-Face wird auf der ungeteilten Nachbarzelle einfach als 4 koplanare Teil-Faces eines 9-flächigen Polyeders in `owner`/`neighbour` geführt!).
  2. **PUMA (Polyhedral Unstructured Mesh Adaptation) ohne Hanging Nodes:**
     - **Verfeinerung (Conformal Polyhedral Subdivision):** Jede markierte Zelle $\Omega_k$ halbiert alle Kanten am Mittelpunkt $\mathbf{x}_e = \frac{1}{2}(\mathbf{x}_a + \mathbf{x}_b)$, teilt jedes $m$-Eck-Face über den Flächenschwerpunkt $\mathbf{x}_f$ in $m$ Vierecks-Subfaces und verbindet diese mit dem Zellschwerpunkt $\mathbf{x}_c$ zu $N_v$ Kind-Polyedern/Hexaedern. Unverfeinerte Nachbarzellen ersetzen in ihrer Face-Liste lediglich das alte Face durch dessen $m$ Subfaces.
     - **Hierarchische Vergröberung (Agglomeration):** Speichert den Eltern-Kind-Baum; ein Eltern-Polyeder wird exakt dann wiederhergestellt, wenn **alle** seine Kinder gleichzeitig zur Vergröberung markiert sind (niemals gröber als das Baseline-Netz).
  3. **Kombinierter Multiskalen-Hesse-Sensor mit Entropie (Gl. 10–11, Adapt-II):**
     Aus einer abstandsgewichteten quadratischen Least-Squares-Rekonstruktion über den Knoten-Nachbarschaftsstencil wird die absolute Hesse-Matrix $|\mathbf{H}| = \mathbf{R}\operatorname{diag}(|\lambda_1|, |\lambda_2|, |\lambda_3|)\mathbf{R}^T$ bestimmt und mit der Zellmetrik $\mathbf{M}^{-1/2} \approx V_k^{1/3}\mathbf{I}$ skaliert:
     $$\|u - u_h\|_{L^p(\Omega_k)}^p = \int_{\Omega_k} \operatorname{tr}\!\left(\mathbf{M}^{-1/2} |\mathbf{H}| \mathbf{M}^{-1/2}\right)^p \mathrm{d}\mathbf{x} \approx V_k \left(V_k^{2/3} \operatorname{tr}|\mathbf{H}_k|\right)^p \quad \text{(Gl. 11)}$$
     **Wichtigstes Ergebnis (Adapt-II vs. Adapt-I):** Die Kombination aus **statischem Druck $p$, Machzahl $M$ und Entropie $s = c_v \ln(p/\rho^\gamma)$** (statt $\mu_t$) konvergiert schneller, erzeugt **25 % kleinere Netze**, ist komplett **turbulenzmodell-unabhängig** (RANS, DES, LES) und eliminiert unphysikalische Ablöse-Artefakte („Pizza Slices“) durch präzise Auflösung von Vorflügel-Nachläufen (*Slat Wakes*).
  4. **Full Multigrid (FMG) Initialisierung:** 5 Gitterstufen aus der Hierarchie ($1000 \to 400$ Zyklen Euler mit $\text{CFL}=0.5$) vor dem eigentlichen RANS-Lauf.

---

### 9.3 [`FastTetrahedralMeshing_1908.03581v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FastTetrahedralMeshing_1908.03581v2.pdf) & Spickzettel [`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild) — „Dirty-CAD“-Enveloping & Conformal AMIPS
* **Autoren & Quelle:** Y. Hu, T. Schneider, B. Wang, D. Zorin, D. Panozzo (2020), *„Fast Tetrahedral Meshing in the Wild“*, *ACM TOG* 39(4), Art. 117, `arXiv:1908.03581v2`, DOI: `10.1145/3386569.3392385`. (C++-Quellcode lokal in [`resources/reference_spickzettel/Meshing/fTetWild/src/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src)).
* **Mathematische & Algorithmische Kern-Bausteine (`LocalOperations.cpp`, `CutMesh.cpp`, `MeshImprovement.cpp`):**
  1. **Skalen- & rotationsinvariante 3D Conformal AMIPS Energie (`LocalOperations.cpp:1240-1393`):**
     Für die Jacobi-Matrix $\mathbf{J} = [\mathbf{v}_1 - \mathbf{v}_0,\; \mathbf{v}_2 - \mathbf{v}_0,\; \mathbf{v}_3 - \mathbf{v}_0]\mathbf{D}_0^{-1}$ bezogen auf das reguläre Referenz-Tetraeder:
     $$E_{\text{AMIPS}}(T) = \frac{\operatorname{tr}(\mathbf{J}^T \mathbf{J})}{(\det \mathbf{J})^{2/3}} = \left[ \frac{27}{16} \left(\det[\mathbf{v}_1 - \mathbf{v}_0, \mathbf{v}_2 - \mathbf{v}_0, \mathbf{v}_3 - \mathbf{v}_0]\right)^{-2} \left( \sum_{0 \le i < j \le 3} \|\mathbf{v}_i - \mathbf{v}_j\|_2^2 \right)^3 \right]^{1/3} \ge 3$$
     Für $\det \mathbf{J} \to 0^+$ divergiert $E_{\text{AMIPS}} \to +\infty$; für $E_{\text{AMIPS}} > 10^8$ schaltet `fTetWild` automatisch auf exakte Multipräzisions-Arithmetik (`triwild::Rational`) und Shewchuks `orient_3d` um.
  2. **Konservative $\epsilon$-Envelope mit Dreiecksgitter-Sampling & Hint-Caching (`Parameters.h`, `AABBWrapper.h`):**
     Mit Bounding-Box-Diagonale $D$, $\epsilon_{\text{input}} = 10^{-3}D$ und Sampling-Schrittweite $d_d = \frac{\epsilon_{\text{input}}}{1.5 \cdot \text{stage}}$ garantiert der reduzierte Prüfradius
     $$\epsilon_{\text{usable}} = \epsilon_{\text{input}} - \frac{d_d}{\sqrt{3}}$$
     per Dreiecksungleichung, dass das *gesamte kontinuierliche Dreieck* innerhalb des $\epsilon_{\text{input}}$-Schlauchs um die CAD-Geometrie bleibt. `AABBWrapper` cached `prev_facet` des vorherigen Sample-Punkts ($\mathcal{O}(1)$ Kurzschluss-Test vor AABB-Baum-Traversierung).
  3. **Tabellengesteuertes Floating-Point `CutMesh` (`CutMesh.cpp`, `TriangleInsertion.cpp`):**
     Snapping grenznahe Knoten ($|d_i| < \epsilon_{\text{coplanar}}$) auf die Dreiecksebene (mit Rücknahme falls ein Nachbar-Tet invertiert), gefolgt von einer **64-Fall Marching-Tetrahedra Lookup-Tabelle (`CutTable`)** über den 6-Bit-Kantenmasken-Index `config_id` $\in [1, 63]$ und konsistenter Vierecks-Diagonalwahl per globaler Vertex-ID (`diag_config_id` $\in [0, 15]$).
  4. **Die 4 lokalen Optimierungs-Operationen mit strenger Inversions-Schranke:**
     - **Edge Splitting:** Max-Heap, teilt Kanten mit $l^2 > \left(\frac{4}{3} l_{\text{loc}}\right)^2$.
     - **Edge Collapsing:** Min-Heap, klappt Kanten mit $l^2 < \left(\frac{4}{5} l_{\text{loc}}\right)^2$ zusammen, **nur wenn** kein Tet invertiert, $\max E_{\text{AMIPS}}$ sinkt und die $\epsilon$-Envelope eingehalten wird.
     - **Edge Swapping:** 3-zu-2, 4-zu-4 und 5-zu-6 Flips für innere Kanten der Valenz 3, 4, 5 bei strikter Senkung von $\max E_{\text{AMIPS}}$.
     - **Vertex Smoothing:** Analytisches 3D-Newton-Verfahren ($\mathbf{x}_{\text{next}} = \mathbf{x} - \alpha \mathbf{H}^{-1}\mathbf{g}$ aus `AMIPS_jacobian` und `AMIPS_hessian`, bis zu 15 Schritte mit 10 Backtracking-Halbierungen) parallel über Graph-Coloring.
  5. **Fast Winding Number & CSG-Booleans (`MeshImprovement.cpp:1848-1910`):**
     Klassifiziert alle Tetraeder-Schwerpunkte $\mathbf{c}_k$ in $\mathcal{O}(\log N_t)$ über die harmonische Raumwinkelformel $W(\mathbf{c}_k) = \frac{1}{4\pi}\sum_{f} \Omega_f(\mathbf{c}_k)$ (inkl. automatischer Normalen-Invertierungs-Erkennung falls $n_{\text{tets}} == 0$ und direkten CSG-Booleans: `Difference` für $\Omega_{\text{fluid}} = \Omega_{\text{farfield}} \setminus \Omega_{\text{solid}} \iff W_{\text{farfield}}(\mathbf{c}_k) > 0.5 \land W_{\text{solid}}(\mathbf{c}_k) \le 0.5$).

---

### 9.4 [`CutCell_Meshing2308.16332v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CutCell_Meshing2308.16332v2.pdf) — Beweisbar CFL-stabile Cut-Cells via *Weighted State Redistribution (WSRD)*
* **Autoren & Quelle:** M. Berger, A. Giuliani (Flatiron Institute & Courant Institute NYU, 2024), *„A New Provably Stable Weighted State Redistribution Algorithm“*, *SIAM J. Sci. Comput.*, `arXiv:2308.16332v2`, DOI: `10.1137/23M1596999`.
* **Kernproblem:** Angeschnittene Kartesische Cut-Cells mit winzigem Volumenanteil $\alpha_i = V_i / h^d \ll 1$ (bis $10^{-17}$ in 3D) erzwingen ohne Stabilisierung $\Delta t \propto \alpha_i h \to 0$. Ungewichtetes SRD hat bei $\alpha_{\text{target}} = 0.5$ einen unstetigen Sprung und verletzt die Monotonie bei $\text{CFL} = 1$.
* **Mathematische Kern-Gleichungen (Gl. 4.1, 4.2, 4.5, 4.10 & A.12):**
  1. **Einmaliges Pre-Merging der Anfangsbedingung (Gl. 4.1):**
     $$U_j^0 = \operatorname{SRD}(U^{\text{init}})_j = \sum_{k \in W_j} w_{j,k} \widehat{Q}_k^{-1}, \qquad \widehat{Q}_k^{-1} = \frac{1}{\widehat{V}_k} \sum_{i \in M_k} w_{i,k} V_i U_i^{\text{init}} \quad \text{(Gl. 4.1)}$$
  2. **Monotones WSRD-Update nach jedem FVM-Schritt $\widehat{U}_i^{n-1}$ (Gl. 4.2):**
     Für jede Merging-Nachbarschaft $M_j$ ($\alpha_j < \alpha_{\text{target}} = 0.5$) und Überlappungsmenge $W_i = \{j : i \in M_j\}$ mit Überlappungszahl $N_i = |W_i|$:
     $$\widehat{Q}_j^{n-1} = \frac{1}{\widehat{V}_j} \sum_{i \in M_j} w_{i,j} V_i \widehat{U}_i^{n-1}, \qquad \widehat{V}_j = \sum_{i \in M_j} w_{i,j} V_i, \qquad U_i^n = \sum_{j \in W_i} w_{i,j} \widehat{Q}_j^{n-1} \quad \text{(Gl. 4.2)}$$
     mit den stetigen, streng konservativen ($\sum_{j \in W_i} w_{i,j} = 1$) und für $\alpha_j \to \alpha_{\text{target}}$ glatt abschaltenden Gewichten:
     $$w_{i,j} = \frac{1}{N_i - 1}\left(1 - \frac{\alpha_j}{\alpha_{\text{target}}}\right) \;\;\forall j \in W_i \setminus \{i\}, \qquad w_{i,i} = 1 - \sum_{j \in W_i \setminus \{i\}} w_{i,j} \quad \text{(Gl. 4.2)}$$
     Spezialfall 2-Zellen-Normal-Merging ($N_1 = 2, \alpha_{\text{target}} = 1/2$, Gl. 4.5):
     $$w_{0,0} = 1, \quad w_{1,0} = 1 - 2\alpha_0 \ge \frac{1}{2} - \alpha_0 \;\text{(Monotonie-Bedingung Gl. A.12)}, \quad w_{1,1} = 2\alpha_0 \quad \text{(Gl. 4.5)}$$
  3. **2.-Ordnung Rekonstruktion mit Expanded-Stencil Minmod-Limiter (Gl. 4.10):**
     $$U_i^n = \sum_{j \in W_i} w_{i,j} \left(\widehat{Q}_j^{n-1} + \widehat{\boldsymbol{\sigma}}_j^{n-1} \cdot (\mathbf{x}_i - \hat{\mathbf{x}}_j)\right), \quad \widehat{\sigma}_0 = \operatorname{minmod}\!\left(\frac{\widehat{Q}_1 - \widehat{Q}_0}{\hat{x}_1 - \hat{x}_0},\, \frac{\widehat{Q}_1 - \widehat{Q}_{-1}}{\hat{x}_1 - \hat{x}_{-1}},\, \frac{\widehat{Q}_0 - \widehat{Q}_{-2}}{\hat{x}_0 - \hat{x}_{-2}}\right) \quad \text{(Gl. 4.10)}$$

---

### 9.5 [`WallShearStress_pointCloud_Meshing_2609.24069v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/WallShearStress_pointCloud_Meshing_2609.24069v1.pdf) — Exakte Wandschubspannung auf Cut-Cells & Punktwolken
* **Autoren & Quelle:** M. Jaiswal, M.-C. Hsu (Iowa State University, Sept. 2026), *„Accurate wall shear stress in immersed flow analysis with application to point cloud-based CFD“*, `arXiv:2609.24069v1`.
* **Kernproblem:** Auf angeschnittenen Cut-Cells ohne Prismen-Grenzschicht schwankt der Wandabstand stark; symmetrische Nitsche-Strafen ($\tau^B \propto \mu / h_{\text{eff}} \to \infty$) verstärken kleinste Geschwindigkeitsresiduen zu massiven unphysikalischen Schubspannungs-Oszillationen.
* **Mathematische Kern-Gleichungen (Gl. 6–29):**
  1. **Unsymmetrische Nitsche-Wandbedingung ($\gamma = -1$, Gl. 9) + Ghost-Penalty (Gl. 6–8):**
     Durch $\gamma = -1$ ist die Koerzivität für **jedes** $\tau_{\text{TAN}}^B \ge 0$ garantiert, wodurch $\tau_{\text{TAN}}^B$ komplett von der Cut-Cell-Geometrie entkoppelt wird!
  2. **Spalding-Wandgesetz & Subgrid Tangential-Parameter $\tau_{\text{TAN}}^B$ (Gl. 10–15):**
     An einem Abtastpunkt $\text{A}$ im Abstand $y^{\text{A}} \equiv \delta_n$ entlang der Wandnormalen (1 Zelle ins Fluid) wird $u^*$ per Newton-Raphson aus Spaldings universellem Wandgesetz gelöst:
     $$y^+ = u^+ + e^{-\kappa B}\left(e^{\kappa u^+} - 1 - \kappa u^+ - \frac{(\kappa u^+)^2}{2} - \frac{(\kappa u^+)^3}{6}\right), \quad y^+ = \frac{y^{\text{A}} u^* \rho}{\mu}, \quad u^+ = \frac{\|\mathbf{u}_t^{\text{A}} - \mathbf{g}_t\|}{u^*} \quad \text{(Gl. 11)}$$
     $$\tau_{\text{TAN}}^B = \frac{1}{\|\mathbf{u}_t - \mathbf{g}_t\|}\left(\rho u^{*2} - \mu \frac{\|\mathbf{u}_t^{\text{A}}\| - \|\mathbf{u}_t\|}{y^{\text{A}}}\right), \qquad \tau_{\text{NOR}}^B = C_b \left(\frac{\mu}{h_n} + \rho \|\mathbf{u}^h\| + \frac{\rho h_n}{\Delta t}\right) \quad \text{(Gl. 13–15)}$$
  3. **Traktions-kompatible 3-Ring-Patch-Rekonstruktion ($24 \times 24$ SPD-System, Gl. 16–29):**
     Über einen 3-Ring-Patch $\Omega^p$ *nur aus Cut-Zellen* wird der deviatorische Spannungstensor $\mathbf{s}^*(\mathbf{x}) = \mathbf{M}(\mathbf{x})\mathbf{a}$ ($\mathbf{P}(\mathbf{x}) = [1, x, y, z]$) durch Minimierung von Volumenspannung + Nitsche-Wandtraktion $\mathbf{t}_t^h$ (Gl. 24–25) bestimmt:
     $$J(\mathbf{a}) = \frac{1}{2}\int_{\Omega^p} \|\mathbf{M}\mathbf{a} - \mathbf{s}^h\|^2\,\mathrm{d}\Omega + \frac{\alpha}{2}\int_{\Gamma^p} \|\mathbf{Q}\mathbf{M}\mathbf{a} - \mathbf{t}_t^h\|^2\,\mathrm{d}\Gamma \implies (\mathbf{A}_\Omega + \alpha \mathbf{A}_\Gamma)\mathbf{a} = \mathbf{b}_\Omega + \alpha \mathbf{b}_\Gamma \quad \text{(Gl. 26–28)}$$
     $$\boldsymbol{\tau}_w(\mathbf{x}) = \mathbf{t}^* - (\mathbf{t}^* \cdot \mathbf{n})\mathbf{n}, \qquad \mathbf{t}^* = \mathbf{Q}\mathbf{s}^*(\mathbf{x}) \quad \text{(Gl. 29)}$$
  4. **Übertragungsregel von Immersogeometrischer VMS/FEM auf unseren Zellzentrierten FVM-Solver:**
     Jaiswal & Hsu formulieren die unsymmetrische Nitsche-Methode ($\gamma = -1$) zusammen mit einer *Ghost-Penalty* über geschnittene Elementkanten innerhalb einer immersogeometrischen Variational-Multiscale-(VMS/FEM)-Formulierung. Bei der Übertragung in unseren **zellzentrierten FVM-Solver** gilt:
     - **WSRD statt Ghost-Penalty:** Für kleine angeschnittene Cut-Cells ($\alpha_c < 0.5$) ersetzt unsere **gewichtete Zustandsumverteilung (WSRD, Berger & Giuliani 2024, Kap. 9.4)** die FEM-Ghost-Penalty vollständig und garantiert $\text{CFL}=1$-Stabilität ohne erweiterte Kanten-Penalties.
     - **1:1-Übernahme von Spalding-Wandfluss & $24\times 24$-Voigt-Patch-Rekonstruktion:** Das Spalding-Wandgesetz ($u^*$, Gl. 11) liefert direkt den impliziten tangentialen FVM-Wandschubspannungsfluss $\boldsymbol{\tau}_w\|\mathbf{S}_b\|$ (kombiniert mit dem Tangentialprojektor $\mathbf{I} - \mathbf{n}_b\otimes\mathbf{n}_b$ aus Kap. 9.18), und die **dreiringige $24\times 24$-Least-Squares-Spannungsrekonstruktion in Voigt-Notation (Gl. 26–29)** lässt sich 1:1 auf unsere FVM-Zellzentren anwenden.

---

### 9.6 [`Meshing_ErrorCorrection_contmesh.part1.final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_ErrorCorrection_contmesh.part1.final.pdf), [`Error_Estimators1-s2.0-S0045793021003601-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Error_Estimators1-s2.0-S0045793021003601-main.pdf) & [`ErrorCorrection_Meshing1712.00693v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ErrorCorrection_Meshing1712.00693v1.pdf) — Continuous Mesh Framework, Anisotrope Metrik- & Adjoint-Fehlerschätzer
* **Autoren & Quellen:**
  1. **A. Loseille, F. Alauzet (2011):** *„Continuous Mesh Framework Part I: Well-Posed Continuous Interpolation Error“*, *SIAM J. Numer. Anal.* 49(1), 38–60, DOI: `10.1137/090754078`.
  2. **A. Balan, M. A. Park, S. L. Wood, W. K. Anderson, A. Rangarajan et al. (2022):** *„A review and comparison of error estimators for anisotropic mesh adaptation for flow simulations“*, *Computers & Fluids* 234, 105259, DOI: `10.1016/j.compfluid.2021.105259`.
  3. **S. M. Kast (2017):** *„An Introduction to Adjoints and Output Error Estimation in Computational Fluid Dynamics“*, `arXiv:1712.00693v1`.
* **Mathematische Kern-Gleichungen:**
  1. **Kontinuierliche Metrik-Dualität & Quasi-Unit-Qualität (Loseille & Alauzet Prop. 2.3–2.4, Gl. 2.1–2.5):**
     $$\mathcal{M}(\mathbf{x}) = d^{\frac{2}{3}}(\mathbf{x})\,\mathcal{R}(\mathbf{x})\operatorname{diag}\!\left(r_1^{-\frac{2}{3}}, r_2^{-\frac{2}{3}}, r_3^{-\frac{2}{3}}\right)\mathcal{R}(\mathbf{x})^T, \quad d = \sqrt{\det\mathcal{M}} = \frac{1}{h_1 h_2 h_3}, \quad \mathcal{C}(\mathcal{M}) = \int_\Omega \sqrt{\det\mathcal{M}}\,\mathrm{d}\mathbf{x}$$
     Für jedes Einheits-Tetraeder ($\ell_{\mathcal{M}}(\mathbf{e}_i) = 1$) gilt exakt die Spur-Invariante $\sum_{i=1}^6 \mathbf{e}_i^T H \mathbf{e}_i = 2\operatorname{tr}(\mathcal{M}^{-1/2} H \mathcal{M}^{-1/2})$ (Gl. 2.3) sowie das Qualitätsmaß:
     $$Q_{\mathcal{M}}(K) = \frac{36}{3^{1/3}} \frac{|K|_{\mathcal{M}}^{2/3}}{\sum_{i=1}^6 \ell_{\mathcal{M}}^2(\mathbf{e}_i)} \in [0, 1], \qquad |u - \pi_{\mathcal{M}} u|(\mathbf{x}) = \frac{1}{10}\operatorname{tr}\!\left(\mathcal{M}^{-\frac{1}{2}} |H(\mathbf{x})| \mathcal{M}^{-\frac{1}{2}}\right) \quad \text{(Gl. 2.5, 3.2)}$$
  2. **Optimale Multiskalen-$L^q$-Metrik vs. Goal-Oriented Adjoint-Metrik (Balan Gl. 15–16, 24–26, 60–62):**
     - *Multiskalen-$L^q$-Metrik ($q=2$ oder $q=4$ für viskose Strömungen):*
       $$\mathcal{M}_{L^q}(\mathbf{x}) = D_{L^q} \left(\det |H(\mathbf{x})|\right)^{-\frac{1}{2q+3}} |H(\mathbf{x})|, \qquad D_{L^q} = \left(\frac{C_t}{\int_\Omega (\det |H|)^{\frac{q}{2q+3}}\,\mathrm{d}\mathbf{x}}\right)^{2/3} \quad \text{(Gl. 15–16)}$$
     - *Loseille Goal-Oriented Adjoint-Metrik ($q=1$):*
       $$\mathcal{M}_{\text{go}}(\mathbf{x}) = D_{\text{go}} \left(\det |Q(\mathbf{x})|\right)^{-\frac{1}{5}} |Q(\mathbf{x})|, \qquad Q(\mathbf{x}) = \sum_{j=1}^5 \sum_{m \in \{x,y,z\}} \left|\frac{\partial \psi_j}{\partial m}\right| \cdot \left|H\!\left(F_m^{(j)}(w)\right)\right| \quad \text{(Gl. 24–25)}$$
  3. **Fidkowski Entropie-Variablen als „Gratis-Adjoint“ ohne linearen Gleichungslöser (Balan Gl. 65–66):**
     Ersetzt den teuren adjungierten Zustandsvektor $\boldsymbol{\psi}_h$ direkt durch die analytischen Entropie-Variablen $\mathbf{V}$ (liefert bei Widerstandskonvergenz fast identische Genauigkeit wie ein voller diskreter Adjoint-Löser zum Nulltarif!):
     $$\mathbf{V} = \left[\frac{\gamma - S}{\gamma - 1} - \frac{\rho \|\mathbf{u}\|^2}{2p},\; \frac{\rho u}{p},\; \frac{\rho v}{p},\; \frac{\rho w}{p},\; -\frac{\rho}{p}\right]^T, \qquad S = \ln\!\left(\frac{p}{\rho^\gamma}\right) \quad \text{(Gl. 65–66)}$$
  4. **Dual-Weighted Residual (DWR) Output-Korrektur & 3.-Ordnung Becker-Rannacher-Schätzer (Kast Gl. 2.101–2.106, A.62):**
     $$\delta J_{\text{est}} = -\sum_{e=1}^{N_e} \left(\mathbf{\Psi}_{h,e} - \mathbf{\Psi}_{h,e}^H\right)^T \mathbf{R}_{h,e}(\mathbf{U}_h^H), \qquad J_{\text{corr}} = J_H(\mathbf{U}_H) + \delta J_{\text{est}}, \qquad \varepsilon_e = \left|\left(\mathbf{\Psi}_{h,e} - \mathbf{\Psi}_{h,e}^H\right)^T \mathbf{R}_{h,e}(\mathbf{U}_h^H)\right| \quad \text{(Gl. 2.105–2.106)}$$
     Und mit quadratischer Restfehler-Tilgung ($\mathcal{O}(\delta u^3)$ nach Becker & Rannacher, Gl. A.62):
     $$J(u) - J(u_H) \approx -\frac{1}{2}\langle r(u_H), \psi - \psi_H\rangle - \frac{1}{2}\langle r^*[u_H](\psi_H), u - u_H\rangle + \mathcal{O}(\delta u^3) \quad \text{(Gl. A.62)}$$

---

### 9.7 **Loseille & Löhner (2013)** & [`Boundarylayer_Collision_1901.00992v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Boundarylayer_Collision_1901.00992v1.pdf) — Kollisionsfreie Grenzschicht-Extrusion (Multi-Normals, Cavity-Imprinting & Medial-Object)
* **Autoren & Quellen:**
  1. **A. Loseille, R. Löhner (2013):** *„Robust Boundary Layer Mesh Generation“*, *Proc. 21st International Meshing Roundtable (IMR)*, Springer, pp. 457–474, DOI: `10.1007/978-3-642-33573-0_27` *(Vollständige Aufbereitung des nicht direkt herunterladbaren Papers!)*.
  2. **J. Marcon, M. Turner, J. Peiró, D. Moxey et al. (2019):** *„High-order curvilinear hybrid mesh generation for CFD simulations“*, *AIAA SciTech*, `arXiv:1901.00992v1`.
* **Mathematische & Algorithmische Kern-Bausteine (Loseille & Löhner 2013 + Marcon et al. 2019):**
  1. **Sichtbarkeitskegel (*Visibility Cone*) & Constrained Laplacian Normal Smoothing (Loseille & Löhner):**
     An jedem Wandknoten $P_i$ mit inzidenten Wanddreiecken $\mathcal{S}(P_i)$ (Einheitsnormalen $\mathbf{n}_{T_j}$ ins Fluid) liefert ein Extrusionsvektor $\mathbf{n}_i$ genau dann strikt positive Prismen-Volumina ($V_i > 0$), wenn er im **Sichtbarkeitskegel** $\mathcal{V}(P_i)$ liegt:
     $$\forall T_j \in \mathcal{S}(P_i): \quad \langle \mathbf{n}_i, \mathbf{n}_{T_j} \rangle \ge \cos(\theta_{\text{vis}}) > 0 \quad (\theta_{\text{vis}} \approx 75^\circ\dots 80^\circ)$$
     Die optimale Einzelnormale $\mathbf{n}_i^* = \arg\max_{\|\mathbf{n}\|=1} \min_{T_j \in \mathcal{S}(P_i)} \langle \mathbf{n}, \mathbf{n}_{T_j}\rangle$ wird per Laplace-Glättung über den Oberflächen-Nachbarschaftsring $\mathcal{N}_S(P_i)$ geglättet, wobei ein Glättungsschritt **nur akzeptiert wird, wenn die Sichtbarkeitsbedingung für alle $T_j \in \mathcal{S}(P_i)$ erhalten bleibt**!
  2. **Multi-Normals an scharfen Kanten, Graten & Hinterkanten (*Ridges & Corners*, Loseille & Löhner):**
     Überschreitet der Knickwinkel zwischen inzidenten Flächen an $P_i$ den Schwellenwert $\theta_{\text{ridge}} \approx 60^\circ$ (oder ist $\mathcal{V}(P_i) = \emptyset$), werden die Flächen $\mathcal{S}(P_i)$ in $m_i \ge 2$ glatte Cluster $\{\mathcal{G}_1, \dots, \mathcal{G}_{m_i}\}$ zerlegt ($\langle \mathbf{n}_{T_a}, \mathbf{n}_{T_b}\rangle \ge \cos\theta_{\text{ridge}}$). Jeder Cluster erhält seine eigene Normale $\mathbf{n}_{i,c}$ plus Winkelhalbierende $\mathbf{n}_{i,\text{bisect}} = \frac{\mathbf{n}_{i,1} + \mathbf{n}_{i,2}}{\|\mathbf{n}_{i,1} + \mathbf{n}_{i,2}\|_2}$, sodass konvexe Kanten mit einem Keil-/Tetraeder-Fächer ummantelt werden, ohne dass die Schichtdicke $h_1$ kollabiert ($1/\cos(\theta/2)$-Längenkorrektur bei moderaten Ecken).
  3. **3D Medial-Axis-Radius $R_{\text{medial}}(\mathbf{x})$ & Drei Kollisions-/Abbruchkriterien (Loseille & Löhner + Marcon Sec. III.B):**
     Der lokale Medial-Axis-Abstand zur gegenüberliegenden Wand berechnet sich direkt über die maximale eingeschriebene Kugel:
     $$R_{\text{medial}}(\mathbf{x}_i) = \min_{\substack{\mathbf{x}_j \in \partial\Omega \\ \mathbf{n}_j \cdot \mathbf{n}_i < -0.3}} \frac{\|\mathbf{x}_j - \mathbf{x}_i\|_2^2}{2\,\max\!\left((\mathbf{x}_j - \mathbf{x}_i)\cdot \mathbf{n}_i,\, 0\right)}$$
     Vor dem Setzen von Schichtknoten $P_{i,k} = P_i + \left(\sum_{l=1}^k h_l\right)\mathbf{n}_i$ (mit $h_l = h_1 r^{l-1}$) greifen drei harte Schranken:
     - **Proximity-/Medial-Axis-Schranke:** $\delta_{i,k} = \sum_{l=1}^k h_l \le \alpha_{\text{coll}}\, d_{\text{opp}}(P_i, \mathbf{n}_i)$ mit $\alpha_{\text{coll}} \in [0.35, 0.45]$ (bzw. $\delta_{i,k} < 0.8\,R_{\text{medial}}(\mathbf{x}_i)$).
     - **Konkave Krümmungs-Schranke (Jacobi-Positivität):** $1 - \kappa_i \delta_{i,k} > \varepsilon_\kappa \implies \delta_{i,k} < \frac{1 - \varepsilon_\kappa}{\max(\kappa_i, 0)}$.
     - **Isotrope Größen-Terminierung:** Stopp der Extrusion sobald $h_k \ge \beta_{\text{iso}}\, h_{\text{iso}}(P_{i,k})$ ($\beta_{\text{iso}} \approx 0.8$).
     **Wichtig für $y^+ \approx 1$:** Bei engem Spalt wird **niemals** die erste Schicht $h_1$ auf $0$ multipliziert, sondern es erfolgt ein **Layer Step-Down (lokale Reduktion der Schichtanzahl $k_{\max}(i)$)** oder eine Dämpfung der Wachstumsrate $r_i \to 1.0$!
  4. **Einheitlicher 3D Cavity-Imprinting-Operator ($\mathcal{H}_{k+1} = \mathcal{H}_k - \mathcal{C}_P + \mathcal{B}_P$, Loseille & Löhner) & Isoparametrisches Macro-Splitting (Marcon Sec. IV.B):**
     - *Loseille & Löhner:* Punkte $P_{i,k}$ werden per sternförmigem Cavity-Operator ($\forall F \in \partial\mathcal{C}_P: \operatorname{vol}([P, F]) > 0$) direkt in das Hintergrund-Volumennetz eingedrückt, wobei bereits erzeugte Grenzschicht-Faces als `locked` geschützt sind.
     - *Marcon et al.:* Alternativ wird zuerst eine einzige gültige **Makro-Grenzschicht-Schale** der Gesamtdicke $H_{\text{macro}} < R_{\text{medial}}$ erzeugt ($\det J_\chi > 0$) und anschließend im Referenzraum $(\xi_1, \xi_2, \eta)$ über $\chi_k = \chi \circ f_k$ mit $\Delta\eta_k = \Delta\eta_1 r^{k-1}$ in $N$ dünne Schichten unterteilt ($\det J_{\chi_k} = \det J_\chi \cdot \det J_{f_k} > 0$ **garantiert für beliebig hohe Streckung**!). An konkaven Flügel-Rumpf-Ecken ersetzt eine **C-Typ-Topologie** (bidirektional gesplittete Eck-Hexaeder) die kollidierende O-Typ-Prismenextrusion.

---

### 9.8 [`Moving_Mesh_2006.14051v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2006.14051v1.pdf), [`MovingMesh_Morphing_2610.08880v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MovingMesh_Morphing_2610.08880v1.pdf), [`Moving_Mesh_2407.00382v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2407.00382v4.pdf) & [`VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf) — Inversionssicheres ALE, Sphere-Packing Morphing (`JamTet`), `UM2N` & `VoroMesh`
* **Autoren & Quellen:**
  1. **A. Shamanskiy, B. Simeon (2021):** *„Mesh deformation techniques in fluid-structure interaction: robustness, accumulated distortion and computational efficiency“*, *Comp. Mech.* 67, 1231–1252, `arXiv:2006.14051v1`.
  2. **J. Lin, H. Lipson (Okt. 2026) — `JamTet`:** *„Physics-Based Sphere Packing for Lagrangian Mesh Morphing“*, `arXiv:2610.08880v1`.
  3. **M. Zhang, C. Wang, S. Kramer, M. D. Piggott et al. (2024) — `UM2N`:** *„Towards Universal Mesh Movement Networks“*, *NeurIPS 2024*, `arXiv:2407.00382v4`.
  4. **N. Maruani, R. Klokov, M. Ovsjanikov, P. Alliez, M. Desbrun (2023) — `VoroMesh`:** *„Learning Watertight Surface Meshes with Voronoi Diagrams“*, *ICCV 2023*, `arXiv:2308.14616v1`.
* **Mathematische & Algorithmische Kern-Bausteine:**
  1. **Tangential Incremental Nonlinear Elasticity (`TINE`) auf $\Omega_f^0$ gegen *Accumulated Distortion* (Shamanskiy & Simeon Gl. 44–54):**
     Inkrementelle Gitterbewegung auf dem deformierten Gitter $\Omega_f^i$ wirkt wie ein hypoelastisch-plastisches Material und akkumuliert unter periodischen Schwingungen unaufhaltsam Gitterverzerrung. **TINE** formuliert die logarithmische Neo-Hooke-Elastizität **strikt auf dem Ausgangsgitter $\Omega_f^0$**:
     $$\operatorname{div}\mathbf{P}_a(\mathbf{u}_a^i) = \mathbf{0} \;\text{in } \Omega_f^0, \qquad \mathbf{P}_a = \mathbf{F}_a \mathbf{S}_a, \qquad \mathbf{S}_a = \lambda_a \ln J_a \mathbf{C}_a^{-1} + \mu_a(\mathbf{I} - \mathbf{C}_a^{-1}), \qquad \mathbf{C}_a = \mathbf{F}_a^T \mathbf{F}_a \quad \text{(Gl. 44–45)}$$
     und löst pro Zeitschritt **nur einen einzigen linearisierten Tangentialschritt** $DR(\mathbf{u}_a^i, \mathbf{w})\cdot \delta\mathbf{u}_a^{i+1} = -R(\mathbf{u}_a^i, \mathbf{w})$ (Gl. 49–51), gewichtet mit der **Jacobi-Volumenversteifung** (Gl. 54):
     $$E_{\text{eff}}(e) = E_0 \left(V_e^0\right)^{-\chi}, \qquad \chi \in [2.0, 2.5] \;\text{(optimal: }\chi = 2.5\text{)} \quad \text{(Gl. 54)}$$
  2. **`JamTet` 2-Stufen Kugelpackung & Differenzierbare Morphing-Energie (Lin & Lipson Gl. 1–5):**
     Hält bei großen Topologie-/Formänderungen $n$ innere Knoten als persistente Kugeln mit Abstand $s = 0.93(|\Omega|/n)^{1/3}$ und Radius $r = \left(\frac{3|\Omega|}{4\pi n}\right)^{1/3} \approx 0.667\,s$ (100 % Knoten-Identität, 0,00 % invertierte Elemente!):
     - *Water Stage (Langreichweitige Gauß-Abstoßung zum Füllen von Hohlräumen, Cutoff $2.2s$, $k_{\mathrm{w}}=300$, Gl. 1):*
       $$\mathbf{f}_i^{\mathrm{w}} = k_{\mathrm{w}} \sum_{j \neq i} \exp\!\left(-\frac{\|\mathbf{p}_i - \mathbf{p}_j\|^2}{s^2}\right)\hat{\mathbf{u}}_{ij} \quad \text{(Gl. 1)}$$
     - *Jam Stage (Harmonischer Kontakt bis zum Jamming-Gleichgewicht, $k_{\mathrm{c}}=500$, Gl. 2–3):*
       $$\mathbf{f}_i^{\mathrm{j}} = k_{\mathrm{c}} \sum_{j \neq i} \max(2r - \|\mathbf{p}_i - \mathbf{p}_j\|, 0)\hat{\mathbf{u}}_{ij}, \quad \mathbf{v}_i \leftarrow \gamma\mathbf{v}_i + \Delta t\mathbf{f}_i, \quad \mathbf{p}_i \leftarrow \Pi(\mathbf{p}_i + \Delta t\mathbf{v}_i) \quad \text{(Gl. 2–3)}$$
       wobei $\Pi$ jeden inneren Kugelmittelpunkt per 5 Newton-Schritten auf $\phi(\mathbf{p}) \le -s$ hält.
     - *Differenzierbare Feder + Neo-Hooke-Volumenenergie (Gl. 5):*
       $$E(\mathbf{x}) = \sum_{e=(i,j)} \frac{1}{2}k_s \left(\|\mathbf{x}_i - \mathbf{x}_j\| - a_e(t)L_e\right)^2 + \sum_{\tau} \mu V_\tau \left(J_\tau - 1 - \ln J_\tau\right) \quad \text{(Gl. 5)}$$
  3. **`UM2N` PDE-unabhängige Monge-Ampère $r$-Adaption (Zhang et al. Gl. 1–7, A5):**
     Löst das Äquidistributionsprinzip $m(\mathbf{x})\det(\mathbf{I} + \mathbf{H}(\phi(\boldsymbol{\xi}))) = \theta$ (Gl. 1–3) über die normierte Hesse-/Gradienten-Monitorfunktion:
     $$m(\mathbf{x}) = 1 + \max\!\left(\alpha \frac{\|\mathbf{H}(u)\|_F}{\max\|\mathbf{H}(u)\|_F},\; \beta \frac{\|\nabla u\|_2}{\max\|\nabla u\|_2}\right) \quad (\alpha = 5,\; \beta = 10) \quad \text{(Gl. A5)}$$
     mit strikter 1-Hop Graph-Attention-Bewegungsschranke und nicht-negativem Volumen-Check (`CheckMeshNotTangle`).
  4. **`VoroMesh` Theorem 1 & Differenzierbarer `VoroLoss` ohne Voronoi-Konstruktion (Maruani et al. Sec. 3.1):**
     *Theorem 1:* Der Abstand eines Oberflächenpunktes $x \in X$ zur nächsten Voronoi-Facette ist **exakt gleich** dem Abstand zur nächsten Mittelsenkrechten-Ebene $H_{i_x, j}$ zwischen dem nächsten Generator $q_{i_x}$ ($i_x = \arg\min_i \|x - q_i\|_2$) und einem Nachbar-Generator $q_j$:
     $$\operatorname{VoroLoss}(X, \mathbf{Q}) = \sum_{x \in X} \min_{j \neq i_x} \|x - H_{i_x, j}\|^2, \qquad \|x - H_{i,j}\| = \frac{\left|\left(x - \frac{q_i + q_j}{2}\right)\cdot (q_j - q_i)\right|}{\|q_j - q_i\|_2}$$
     Dadurch lassen sich die 3D-Voronoi-Saatpunkte $\mathbf{Q}$ rein über $k$-NN-Abfragen ($k=16$) in $\mathcal{O}(N + |X|k)$ analytisch auf die CAD/Level-Set-Oberfläche optimieren; das resultierende Polyedernetz ist **per Konstruktion zu 100 % wasserdicht und selbstschnittfrei**!


---

### 9.9 [`SuperElements_Db_115554.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/SuperElements_Db_115554.pdf) — Combinatorial Meshing, ASP-optimierte Super Elements ($\min SJ \ge 0.302$), Rohteil-Precursor-Mesh & Top-Down Entity Mapping
* **Autor & Quelle:** H. Stromberg (2023), *„Combinatorial Meshing for Mechanical FEM“*, Dissertation D 104, Fakultät für Mathematik/Informatik und Maschinenbau, Institut für Maschinenwesen (IMW), Technische Universität Clausthal (Hauptreferent: Prof. Dr.-Ing. A. Lohrengel; Korreferent: Prof. Dr. R. Ehlers; ASP/Clingo-Modell in Zusammenarbeit mit V. Mayer-Eichberger).
* **Kernproblem & Motivation:**
  1. **Empfindlichkeit lokaler Kerbspannungen $\boldsymbol{\sigma}_{\max}$ & künstliche Versteifung (*Excessive Stiffening*):** Der diskrete Differentialoperator $\underline{\underline{B}} = \frac{\delta\mathbf{E}}{\delta\vec{u}}$ (Gl. 3.6, 3.19) reagiert an Kerbradien empfindlich auf Elementverzerrungen.
  2. **Verbot von Hanging Nodes UND Hanging Edges (Kap. 3.1.2 & 4.1, S. 53–54):** Treffen zwei Tetraeder-Dreiecksflächen ohne zwischengeschaltete Pyramide direkt auf eine Vierecksfläche eines Hexaeders, entsteht entlang der Diagonale eine **Hanging Edge** (Abb. 4.1, S. 54). Bei quadratischen Ansatzfunktionen besitzt die Diagonalkante der Tetraeder einen Midside-Knoten, den das biquadratische Viereck nicht besitzt ($\implies$ Sprung im Verschiebungsfeld und Spannungssingularität).
  3. **Schwächen klassischer Kartesischer Grid-/Octree-Mesher (Kap. 3.4.4 & 4.2):** Orientierungssensitivität bei gedrehten/rotationssymmetrischen Bauteilen (Zahnräder, Wellen), *Staircase*-Effekt mit unrettbaren Randwinkeln ($SJ \le 0$ nach dem Snapping) und Verlust des CAD-Entity-Bezugs.
* **Mathematische & Algorithmische Kern-Bausteine (Kap. 3.3, 4, 5, 6, 7 & Anhang B/C):**
  1. **Vereinheitlichte Qualitätsmetrik $J_{\text{ENS}}$ für gemischte Netze (Hex8, Prism6, Pyr5, Tet4) + Pyramiden-Basis-Check (Kap. 3.3, Gl. 3.28–3.32, S. 36–38):**
     - *Aspektverhältnis-sensitiver Knoten-Scaled-Jacobian (Gl. 3.28 & 3.30):* Normiert $\det(J_i)$ nicht über das Produkt der drei lokalen Kantenlängen, sondern über die **dritte Potenz der maximalen Elementkantenlänge** $\max_k l_k$, um auch extrem gestreckte Elemente zu bestrafen:
       $$J_{s,i} = \frac{\det\big([\vec{x}_{i,1} - \vec{x}_i,\; \vec{x}_{i,2} - \vec{x}_i,\; \vec{x}_{i,3} - \vec{x}_i]\big)}{\left(\max_k l_k\right)^3} \in [-1, 1] \quad \text{(Gl. 3.30)}$$
     - *Element Normalized Scaled Jacobian $J_{\text{ENS}}$ (Lobos 2015 / Stromberg Gl. 3.32, S. 38):* Normiert das elementweite Minimum $J_S = \min_i J_{s,i}$ über den elementspezifischen Idealwert $J_{S,\text{opt}}$ auf $[-1, 1]$:
       $$J_{\text{ENS}} = \begin{cases} (1 + J_{S,\text{opt}}) - J_S & \text{falls } J_S > J_{S,\text{opt}} \\ J_S / J_{S,\text{opt}} & \text{falls } -J_{S,\text{opt}} \le J_S \le J_{S,\text{opt}} \\ -(1 + J_{S,\text{opt}}) - J_S & \text{falls } J_S < -J_{S,\text{opt}} \end{cases}, \qquad J_{S,\text{opt}} = \begin{cases} 1.0 & (\text{Hex8}) \\ \sqrt{3}/2 \approx 0.8660 & (\text{Prism6}) \\ \sqrt{2}/2 \approx 0.7071 & (\text{Tet4, Pyr5}) \end{cases} \quad \text{(Gl. 3.32)}$$
     - *Erweiterung für nicht-konvexe/invertierte Pyramiden-Basen (S. 38, Abb. 3.19):* An der Pyramidenspitze wird das Minimum über alle $\binom{4}{3} = 4$ Kanten-Tripel gebildet; zusätzlich wird über der Vierecks-Basis ein **fiktives Hexaeder** der Höhe $\bar{l}_{\text{base}} = \frac{1}{4}\sum_{b=1}^4 l_b$ errichtet und dessen 8 Knoten-SJs in das Pyramiden-Minimum einbezogen.
  2. **Precursor Mesh aus dem CAD-Halbzeug (*Bar Stock*, Kap. 4.2, S. 54–56):**
     Statt einer achsenparallelen Bounding-Box wird das reale Fertigungs-Rohteil (Zylinder/Rundstab bei Drehteilen, Extrusionsprofil bei Walzteilen) aus der CAD-Feature-Historie als **Precursor Mesh** per Sweeping vernetzt ($\implies$ **100 % orientierungsinvariant** bei Zahnrädern/Wellen wie MAMBO B42!).
  3. **23 Kanonische Hexaeder-Super-Elemente via Answer Set Programming (ASP / Clingo, Kap. 5, S. 61–76 & Anhang B/C, S. 121–159):**
     - Durch Rotations- und Spiegelsymmetrie reduzieren sich die $2^n$ An-/Abwesenheitsmuster der $n$ *Outer Nodes* auf **23 eindeutige Fälle beim Hexaeder** ($n=8, 2^8=256$), **5 beim Tetraeder** ($n=4$), **13 beim Prisma** ($n=6$) und **12 bei der Pyramide** ($n=5$) (Tab. 5.1, S. 64).
     - Im Referenzwürfel $[-1, 1]^3$ werden **35 Kandidatenknoten** definiert (Tab. B.1, S. 121–122): 8 Ecken $(\pm 1, \pm 1, \pm 1)$, 12 Kantenmitten, 6 Flächenmitten, 1 Zentrum $(0,0,0)$ sowie **8 innere Oktanten-Stützknoten $(\pm 0.5, \pm 0.5, \pm 0.5)$**, auf denen sich die Pyramiden der Außenflächen mit hohem $SJ$ abstützen!
     - Da das Oberflächenmuster jeder Außenfläche (9 Dreiecks-Muster in Abb. 5.1, 16 Vierecks-Muster in Abb. 5.2) **eindeutig durch die aktiven Outer Nodes dieser Fläche determiniert** ist, passen benachbarte Super Elements bei übereinstimmender Knotenfärbung **automatisch ohne Hanging Nodes und ohne Hanging Edges** zusammen!
     - Über 767.020 logische ASP-Klauseln (Mannigfaltigkeit, Abgeschlossenheit, Oberflächen-Konvexität, Schnittfreiheit, Erreichbarkeit) wurde für alle 23 Hex-Fälle das **globale Optimum bewiesen**: Über alle 23 Fälle gilt **$\min SJ \ge 0.302$** (in 14 Fällen $\ge 0.500$, Tab. 5.4, S. 74). Alle **Knoten-Inzidenzmatrizen sind vollständig in Anhang C (Tab. C.2–C.7, S. 130–159) tabelliert** und können als statische `constexpr`-Lookup-Tabelle ohne Laufzeit-Solver genutzt werden!
  4. **Asymmetrisches Residual Volume (RV) & $\mathcal{O}(n_{\text{elem}})$ Greedy-Voting-Heuristik (Kap. 6, S. 77–86, Gl. 6.1–6.3):**
     - Um hochfrequente *Zigzag*-Sprünge an fast gitterparallelen Wänden zu unterbinden (Abb. 6.1–6.2, S. 78), wird fehlendes Bauteilvolumen ($RV_{\text{missing}}$) **quadratisch** und überstehendes Volumen ($RV_{\text{excess}}$) **linear** über $6\times 6\times 6 = 216$ Subdomains pro Zelle bestraft:
       $$R(e, D) = \left(RV_{\text{missing}}(e, D)\right)^2 + RV_{\text{excess}}(e, D), \qquad D^*(e) = \arg\min_{D \in \{0,\dots,255\}} R(e, D)$$
     - Jedes Precursor-Element $e$ gibt für seine Eckknoten $N \in e$ eine Stimme $v(e, N) \in \{+1, -1\}$ ab; Knoten mit Mehrheits-Score $S(N) = \sum_{e \ni N} v(e, N) > 0$ bleiben erhalten, woraus in strikt linearer Zeit $C_t(n_{\text{elem}}) = n_{\text{elem}}k_1 + n_{\text{nodes}}k_2 = \mathcal{O}(n_{\text{elem}})$ (Gl. 6.2–6.3; **38 ms für 1.000 Zellen in Python**) die global konsistente Super-Element-Zerlegung folgt.
  5. **Top-Down Entity Mapping & Automatisches Defeaturing in $\mathcal{O}(n \log n)$ (Kap. 7, S. 87–91, Gl. 7.1–7.3):**
     - Vorab-Triangulierung aller CAD-NURBS-Flächen mit Zielkantenlänge $l_{\text{tri}} = \frac{1}{2} l_{\text{min,mesh}}$ (eliminiert Newton-Raphson-Divergenz auf gekrümmten NURBS und filtert Mikro-Features $< \frac{1}{2} l_{\text{min,mesh}}$ automatisch aus!).
     - **3-Stufen-Hierarchie:**
       1. *Mesh-Faces $\to$ CAD-Surfaces* per Octree/k-d-Tree Raycasting in $\mathcal{O}(n \log n)$ (Gl. 7.3).
       2. *Mesh-Edges $\to$ CAD-Curves* genau dann, wenn die zwei adjazenten Randflächen auf verschiedene CAD-Surfaces $(S_1 \neq S_2)$ abgebildet wurden und einen zusammenhängenden Pfad bilden.
       3. *Mesh-Nodes $\to$ CAD-Corners* am Treffpunkt von $\ge 3$ CAD-Kurven.
     - **DOF-gebundene Projektion & Glättung (Tab. 7.2, S. 90):** Corner-Nodes fixiert (0 DOF), Curve-Nodes gleiten auf der 1D-Kurve (1 DOF), Surface-Nodes gleiten auf der 2D-Fläche (2 DOF), Innenknoten frei im 3D-Volumen (3 DOF).


---

### 9.10 [`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Continuous_Mesh_Framework_Part_II_Validations_and_.pdf) — Optimale $L^p$-Metrik, $P_2$-Kantenrekonstruktion & Multiskalen-Adaption
* **Autoren & Quelle:** A. Loseille, F. Alauzet (INRIA, 2011), *„Continuous Mesh Framework Part II: Validations and Applications“*, *SIAM J. Numer. Anal. (SINUM)* 49(1), 61–86, DOI: `10.1137/10078654X`.
* **Mathematische & Algorithmische Kern-Bausteine:**
  1. **Exakte analytische Lösung des globalen Variationsproblems in $L^p$ (Theorem 2.1, Gl. 2.1–2.12):**
     Die Minimierung von $\mathcal{E}_p(\mathbf{M}) = \left(\int_\Omega e_{\mathcal{M}}^p\,\mathrm{d}\mathbf{x}\right)^{1/p}$ unter der Komplexitätsnebenbedingung $\mathcal{C}(\mathbf{M}) = \int_\Omega d(\mathbf{x})\,\mathrm{d}\mathbf{x} = N$ entkoppelt über die Transformation $h_i = d^{-1/3}r_i^{1/3}$ in Richtung/Anisotropie ($r_i^* = (\gamma_1\gamma_2\gamma_3)^{1/2}\gamma_i^{-3/2}$, Gl. 2.5) und Dichte ($d^*$, Gl. 2.6) und besitzt die eindeutige globale Lösung:
     $$\mathcal{M}_{L^p}(\mathbf{x}) = D_{L^p}\,\det(|H_u(\mathbf{x})|)^{-\frac{1}{2p+3}}\,|H_u(\mathbf{x})|, \qquad D_{L^p} = N^{\frac{2}{3}}\left(\int_\Omega \det(|H_u|)^{\frac{p}{2p+3}}\,\mathrm{d}\mathbf{x}\right)^{-\frac{2}{3}} \quad \text{(Gl. 2.10)}$$
     mit der scharfen a-priori-Fehlerschranke 2. Ordnung auf Einheitsgittern ($\ell_{\mathcal{M}}(e) \in [\frac{\sqrt{2}}{2}, \sqrt{2}]$, $N_v \approx 2\,\mathcal{C}(\mathbf{M})$):
     $$\|u - \pi_{\mathbf{M}_{L^p}} u\|_{L^p(\Omega)} = 3\,N^{-\frac{2}{3}}\left(\int_\Omega \det(|H_u|)^{\frac{p}{2p+3}}\,\mathrm{d}\mathbf{x}\right)^{\frac{2p+3}{3p}} \le \frac{\text{Cst}}{N^{2/3}} \quad \text{(Gl. 2.11–2.12)}$$
     *(Wichtig: $p \to \infty$ liefert die klassische $L^\infty$-Metrik, die alle Zellen im stärksten Stoß konzentriert; $p=1,2$ dämpft Singularitäten über $\det(|H_u|)^{-1/(2p+3)}$ und löst feine Nachlaufwirbel und Akustikwellen gleichzeitig auf!).*
  2. **Rauschfreie $P_2$-Kantenmittelpunkts- & $L^2$-Clément-Hesse-Rekonstruktion (§3.1–3.2, Gl. 3.1–3.2):**
     Volumengewichtete nodale Gradientenprojektion $\nabla_R u_h(\mathbf{p}_i) = \frac{\sum_{K_j \in S_i} |K_j| \nabla(u_h|_{K_j})}{\sum_{K_j \in S_i} |K_j|}$, gefolgt von der kubischen Rekonstruktion auf Kantenmittelpunkten $\mathbf{e} = \mathbf{p}\mathbf{q}$:
     $$u_h(\mathbf{e}) = \frac{u_h(\mathbf{p}) + u_h(\mathbf{q})}{2} + \frac{\nabla_R u_h(\mathbf{p}) - \nabla_R u_h(\mathbf{q})}{8} \cdot \mathbf{p}\mathbf{q}$$
     und anschließender $L^2$-Projektion der zweiten Ableitungen der $P_2$-Lagrange-Funktion $R_h u_h$.

---

### 9.11 [`VoronoiDiagrams_2404.18629v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoronoiDiagrams_2404.18629v1.pdf) — Voll differenzierbare 3D Restricted-Power-/Voronoi-Diagramme über Topologie-Flips
* **Autoren & Quelle:** L. Numerow, Y. Li, S. Coros, B. Thomaszewski (ETH Zürich, SIGGRAPH 2024), *„Differentiable Voronoi Diagrams for Simulation of Cell-Based Mechanical Systems“*, *ACM TOG* 43(4), Art. 69, `arXiv:2404.18629v1`, DOI: `10.1145/3658152`.
* **Mathematische & Algorithmische Kern-Bausteine:**
  1. **Power-Diagramm-Metrik & die 4 Klassen von 3D-Restricted-Voronoi-Eckpunkten (§3.1–3.2, Gl. 3–4):**
     Mit $d_{\text{Power}}(\mathbf{p}, C_i)^2 = \|\mathbf{p} - \mathbf{x}_i\|^2 - w_i$ (Gl. 3) bleiben alle Zellfacetten exakt planar, während die Gewichte $w_i$ beliebige Zellgrößen bis zum glatten Zellkollaps ($V_i \to 0$) steuern. Jeder 3D-Eckpunkt $\mathbf{x}_c$ ist der Schnittpunkt von 3 Ebenen (Klasse a: 3 Bisektoren zu 4 Saatpunkten; Klasse b: 2 Bisektoren + 1 Randdreieck; Klasse c: 1 Bisektor + 2 Randdreiecke; Klasse d: 3 Randdreiecke):
     $$\mathbf{A}(\mathbf{c}, \mathbf{v})\,\mathbf{x}_c = \mathbf{b}(\mathbf{c}, \mathbf{v}), \qquad (\mathbf{x}_j - \mathbf{x}_i)\cdot \mathbf{x}_c = \frac{1}{2}\left(\|\mathbf{x}_j\|^2 - \|\mathbf{x}_i\|^2 + w_i - w_j\right) \quad \text{(Gl. 4)}$$
  2. **Exakte analytische 1. und 2. Ableitungen der Voronoi-Eckpunkte & $C^1$-Stetigkeit über Flips (§3.2–3.6, Gl. 5–9, 29):**
     Implizite Differentiation des lokalen $3\times 3$-Systems $\mathbf{A}\mathbf{x}_c = \mathbf{b}$ nach jeder Saatpunkt-/Gewichts-/Randknotenvariable $q, r \in (\mathbf{c}, \mathbf{v})$ liefert:
     $$\frac{\partial \mathbf{x}_c}{\partial q} = \mathbf{A}^{-1}\!\left(\frac{\partial \mathbf{b}}{\partial q} - \frac{\partial \mathbf{A}}{\partial q}\mathbf{x}_c\right), \qquad \frac{\partial^2 \mathbf{x}_c}{\partial q\,\partial r} = \mathbf{A}^{-1}\!\left(\frac{\partial^2 \mathbf{b}}{\partial q\partial r} - \frac{\partial^2 \mathbf{A}}{\partial q\partial r}\mathbf{x}_c - \frac{\partial \mathbf{A}}{\partial q}\frac{\partial \mathbf{x}_c}{\partial r} - \frac{\partial \mathbf{A}}{\partial r}\frac{\partial \mathbf{x}_c}{\partial q}\right)$$
     Zellvolumina $V_i(\mathbf{c})$ und glatte Volumenintegrale (ausgewertet ohne innere Tetraedrisierung über orientierte Rand-Simplices zum Ursprung, Gl. 25–28) sind **über topologische Nachbarschafts-Flips hinweg $C^1$-stetig**!
  3. **Newton-Optimierung, BDF2-Dynamik & Impliziter Adjoint (Gl. 11–24):**
     Kombiniert Volumenterm $a_0(V/\bar{V}-1)^2$, zweites Trägheitsmoment $a_1 \bar{V}^{-4/3}\int_{\mathcal{R}}\|\mathbf{x}-\bar{\mathbf{x}}\|^2\mathrm{d}V$ und Centroidal-Voronoi-Regularisierung $a_2 \bar{V}^{-2/3}\|\mathbf{c}-\bar{\mathbf{x}}\|^2$ (Gl. 22) mit BDF2-Zeitintegration (Gl. 14–15) und implizitem Adjoint $\frac{\mathrm{d}\mathbf{y}}{\mathrm{d}\mathbf{u}} = -\left(\frac{\partial^2 E}{\partial \mathbf{y}^2}\right)^{-1}\frac{\partial^2 E}{\partial \mathbf{y}\partial \mathbf{u}}$ (Gl. 18–19).

---

### 9.12 [`MeshlessVoronoiOnGPU_3272127.3275092.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshlessVoronoiOnGPU_3272127.3275092.pdf) — Synchronisationsfreies 3D-Voronoi-Meshing im GPU Shared Memory
* **Autoren & Quelle:** N. Ray, D. Sokolov, S. Lefebvre, B. Lévy (Inria / LORIA, SIGGRAPH Asia 2018), *„Meshless Voronoi on the GPU“*, *ACM TOG* 37(6), Art. 265, DOI: `10.1145/3272127.3275092`.
* **Mathematische & Algorithmische Kern-Bausteine:**
  1. **Duale Dreiecks-Repräsentation konvexer Polyeder im Shared Memory (§3.2, Abb. 8 & 10):**
     Jeder GPU-Thread berechnet eine 3D-Voronoi-Zelle komplett unabhängig (ohne globalen Delaunay-Graphen und ohne Locks: **10 Mio. Zellen in 800 ms**!). Da jede Ecke eines generischen konvexen 3D-Polyeders exakt die Valenz 3 (Schnittpunkt von 3 Ebenen) besitzt, wird die Zelle rein dual gespeichert:
     - `float4 P[max_P]` (`max_P = 64`): Halbraum-Ebenen $(a_j, b_j, c_j, d_j)$, initialisiert mit den 6 Bounding-Box-/Octree-Seitenflächen.
     - `int3 T[max_T]` (`max_T = 96`): Duale Dreiecke $T_m = (u, v, w) \iff$ primaler Eckpunkt $\mathbf{v}_{uvw} = \mathcal{P}_u \cap \mathcal{P}_v \cap \mathcal{P}_w$ (initialisiert mit 8 Ecken).
  2. **Radius of Security & `clipByPlane` (Alg. 1–4, §3.1–3.3):**
     - Nachbarn $\mathbf{x}_j$ werden aufsteigend nach Abstand per gitterbasiertem Max-Heap (`Alg. 1`) durchlaufen; Abbruch sobald $\|\mathbf{x}_p - \mathbf{x}_i\| > 2 R_{\max} = 2 \max_{\mathbf{v} \in T} \|\mathbf{v} - \mathbf{x}_i\|$ (*Radius of Security*, `Alg. 2`).
     - `clipByPlane` (`Alg. 3`): Entfernt alle Ecken $(u,v,w) \in \mathcal{T}$ mit $(\mathbf{v}_{uvw}, 1)\cdot \mathbf{p} > 0$ in das Rückseiten-Array $\mathcal{R}$, bestimmt den einfachen Randzyklus $\partial\mathcal{R}$ als zirkuläre Liste `char next_in_cycle[64]` (`Alg. 4`) und schließt das duale Loch durch neue Dreiecke $(s, t, p)$ für jede Randkante $(s,t) \in \partial\mathcal{R}$.
  3. **On-the-fly Volumenintegration über 6 orientierte Projektions-Tetraeder pro Ecke (§3.5, Abb. 13–14) & FP-Filter (App. A):**
     Summiert direkt über $(u,v,w) \in \mathcal{T}$ je 6 orientierte Tetraeder aus $\mathbf{x}_i$, den 3 Ebenenprojektionen $\mathbf{p}_u, \mathbf{p}_v, \mathbf{p}_w$, den 3 Kantenprojektionen $\mathbf{e}_{uv}, \mathbf{e}_{vw}, \mathbf{e}_{wu}$ und $\mathbf{v}_{uvw}$ (überstehende Außenteile heben sich nach dem Satz von Stokes exakt weg). Numerisch kritische $3\times 3$-Schnitte ($< 10$ von $10^7$ Zellen) werden per Melquiond-Pion Gleitkomma-Filter erkannt und exakt aufgelöst.

---

### 9.13 [`DiscreteGCL_1709.04788v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/DiscreteGCL_1709.04788v4.pdf) — Exakte Diskrete Erhaltungssätze (Skopenkov 2023) & ALE Discrete Geometric Conservation Law (DGCL)
* **Autoren & Quellen:**
  1. **M. Skopenkov (2023):** *„Discrete field theory: symmetries and conservation laws“*, `arXiv:1709.04788v4 [math-ph]`.
  2. **H. Guillard, C. Farhat (2000):** *„On the significance of the geometric conservation law for flow computations on moving meshes“*, *Comput. Methods Appl. Mech. Engrg.* 190, 1467–1482.
* **Mathematische & Algorithmische Kern-Bausteine:**
  1. **Topologische Exaktheit auf Zellkomplexen (Skopenkov Def. 1.4.1–1.4.8, Theorem 1.4.5, 1.4.7, 1.4.9, Prop. 4.2.4, Gl. 4–21):**
     Mit Randoperator $\partial$, Korandoperator $\delta$ ($\delta\delta = 0, \partial\partial = 0$), Cup-/Cap-Produkten $\smile, \frown$ (diskrete Leibniz-Regel $\delta(\phi\smile\psi) = \delta\phi\smile\psi + (-1)^{\dim\phi}\phi\smile\delta\psi$, Gl. 21) und dem Chain-Cochain-Kreuzprodukt $[\psi\times\phi](e\times f) = \psi(e)\phi(f)$ ist für **jede** Lösung der diskreten Euler-Lagrange-Gleichung $\partial\frac{\partial\mathcal{L}}{\partial(\delta\phi)} + \frac{\partial\mathcal{L}}{\partial\phi} = 0$ (Gl. 7) der diskrete Energie-Impuls-Tensor
     $$T[\phi] = \frac{\partial\mathcal{L}[\phi]}{\partial(\delta\phi)} \times \delta\phi + \frac{\partial\mathcal{L}[\phi]}{\partial\phi} \times \phi \quad \Longrightarrow \quad \partial T[\phi] \equiv 0 \quad \text{(Gl. 10)}$$
     **ohne Rückgriff auf kontinuierliche Translationssymmetrie exakt erhalten**, und über die diskrete Stokes-Formel $\langle T, \partial g\rangle_k = \langle \partial T, g\rangle_k = 0$ (Prop. 4.2.4) verschwindet die Bilanz über jede geschlossene Kontrollzelle exakt.
  2. **3D Swept-Face-Volumes via Simpson-Regel & RK-/BDF2-DGCL auf bewegten FVM-Gittern:**
     Für geradlinige Knotenbewegung $\mathbf{x}_k(\tau) = (1-\tau)\mathbf{x}_k^n + \tau\mathbf{x}_k^{n+1}$ ist der Flächenvektor $\mathbf{S}_f(\tau)$ in 3D quadratisch in $\tau \in [0, 1]$, sodass das überstrichene Facettenvolumen $\Delta V_f^{n,n+1}$ exakt durch die Simpson-Regel gegeben ist:
     $$\Delta V_f^{n,n+1} = \frac{1}{6}(\mathbf{x}_f^{n+1} - \mathbf{x}_f^n) \cdot \left(\mathbf{S}_f(t^n) + 4\,\mathbf{S}_f(t^{n+1/2}) + \mathbf{S}_f(t^{n+1})\right), \qquad V_i^{n+1} - V_i^n = \sum_{f \in \partial\Omega_i} \Delta V_f^{n,n+1}$$
     Damit ein konstanter Freiströmungszustand $u_\infty$ exakt erhalten bleibt, muss der diskrete Gittervolumenfluss $\dot{V}_f = (\mathbf{w}_f\cdot\mathbf{S}_f)_{\text{DGCL}}$ exakt dieselben Zeitintegrationsgewichte nutzen wie die Erhaltungsgleichung:
     - **BDF2-DGCL:** $\frac{3 V_i^{n+1} - 4 V_i^n + V_i^{n-1}}{2\Delta t} = \sum_{f \in \partial\Omega_i} \dot{V}_f^{n+1} \implies \dot{V}_f^{n+1} = \frac{3\,\Delta V_f^{n,n+1} - \Delta V_f^{n-1,n}}{2\,\Delta t}$.
     - **Runge-Kutta-DGCL:** $V_i^{(j)} = V_i^n + \Delta t\sum_{k=1}^s a_{jk}\sum_{f\in\partial\Omega_i}(\mathbf{w}_f\cdot\mathbf{S}_f)^{(k)}$.

---

### 9.14 [`Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf) — Einheitliche CC-FVM Kontinuumsmechanik & Householder-Symmetrie-Randbedingungen
* **Autoren & Quelle:** I. Demirdžić, P. Cardiff (2022), *„Symmetry plane boundary conditions for cell-centered finite-volume continuum mechanics“*, *Numerical Heat Transfer, Part B*, 82(1–2), 1–12, DOI: `10.1080/10407790.2022.2105073`.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–40):**
  1. **Exaktes Verschwinden der Cross-Diffusion an Symmetrieebenen (Gl. 1–7):**
     Da der Verbindungsvektor zur Spiegelzelle $M$, $\boldsymbol{\Delta} = \overrightarrow{PM} = 2\frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$ (Gl. 3), immer exakt kollinear zum äußeren Flächenvektor $\mathbf{s}$ steht, ist der nicht-orthogonale Cross-Diffusion-Term an einer Symmetrieebene selbst auf beliebig schiefen Polyedergittern **identisch Null** ($D_\phi = \Gamma_\phi \frac{\mathbf{s}\cdot\mathbf{s}}{2\,\mathbf{d}\cdot\mathbf{s}}(\phi_M - \phi_P)$, Gl. 4)! Gitter-Schiefheit ($\mathbf{d} \nparallel \mathbf{s}$) tritt ausschließlich bei der Flächenmittelpunkts-Rekonstruktion $\phi_B = \phi_N + \mathbf{t}\cdot(\operatorname{grad}\phi)_N$ über den Tangentialversatz $\mathbf{t} = \mathbf{d} - \frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$ (Gl. 6–7) auf.
  2. **Householder-Spiegelung $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ & Patankar-Split für Vektoren $\mathbf{v}$ (Geschwindigkeit / Strukturverschiebung, Gl. 8, 11–16, 34–35):**
     Aus $\mathbf{v}_M = \mathbf{R}\cdot\mathbf{v}_P$ folgt $\mathbf{D}_v = -\Gamma_v \frac{\mathbf{s}\cdot\mathbf{v}_P}{\mathbf{d}\cdot\mathbf{s}}\mathbf{s}$ (Gl. 12) und der unbedingt diagonaldominante Split $D_{vi} = D_{vi,\text{ex}} - D_{vi,\text{im}} v_{Pi}$:
     $$D_{vi,\text{im}} = \Gamma_v \frac{s_i s_i}{\mathbf{d}\cdot\mathbf{s}} > 0 \;\text{(auf Hauptdiagonale } a_P\text{)}, \qquad D_{vi,\text{ex}} = -\Gamma_v \frac{\mathbf{s}\cdot\mathbf{v}_P - s_i v_{Pi}}{\mathbf{d}\cdot\mathbf{s}} s_i \;\text{(auf RHS } b_P\text{)} \quad \text{(Gl. 15–16)}$$
     sowie der schiefe-korrigierte Randwert $\mathbf{v}_B = \mathbf{v}_P - \frac{\mathbf{s}\cdot\mathbf{v}_P}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s} + \mathbf{t}\cdot\mathbf{G}_P - \mathbf{t}\cdot\frac{\mathbf{G}_P\cdot\mathbf{s}\otimes\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}$ (Gl. 34).
  3. **Symmetrische & unsymmetrische Tensoren 2. Stufe $\mathbf{T}$ (Spannungs- / Cauchy-Green-Tensor, Gl. 17–22, 36–37):**
     Aus $\mathbf{T}_M = \mathbf{R}\cdot\mathbf{T}_P\cdot\mathbf{R}^T$ folgt für jede Komponente $T_{ij}$ ($i,j \in \{1,2,3\}$, keine Summation über $i,j$):
     $$D_{Tij,\text{im}} = \Gamma_T \frac{s_i s_i + s_j s_j}{\mathbf{d}\cdot\mathbf{s}}, \qquad D_{Tij,\text{ex}} = -\Gamma_T \frac{s_i(s_k T_{Pkj}) + s_j(s_k T_{Pik}) - \frac{2(s_p s_q T_{Ppq})s_i s_j}{\mathbf{s}\cdot\mathbf{s}} - (s_i^2 + s_j^2)T_{Pij}}{\mathbf{d}\cdot\mathbf{s}} \quad \text{(Gl. 21–22)}$$

---

### 9.15 [`FSI_Monolithic_2505.22920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FSI_Monolithic_2505.22920v1.pdf) — Monolithische 3D Euler'sche FSI mit Geometrischem VOF/PLIC
* **Autoren & Quelle:** S. Prajapati, A. Fakhreddine, K. Mahesh (Univ. of Minnesota & Univ. of Michigan, Mai 2025), *„Monolithic framework to simulate fluid-structure interaction problems using geometric volume-of-fluid method“*, `arXiv:2505.22920v1 [physics.flu-dyn]`.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–39):**
  1. **One-Continuum Monolithische Impuls- & Spannungsformulierung auf festem Gitter (Gl. 6–15):**
     Mit dem Festkörper-Volumenbruch $\phi \in [0, 1]$ (Gl. 6) erfüllen die gemischten Größen $u_i = \phi u_i^s + (1-\phi)u_i^f$, $\rho = \phi\rho^s + (1-\phi)\rho^f$ und $\sigma_{ij} = \phi\sigma_{ij}^s + (1-\phi)\sigma_{ij}^f$ (Gl. 7–10) auf dem gesamten Gebiet exakt die kinematische und dynamische Kopplungsbedingung:
     $$\frac{\partial u_i}{\partial t} + \frac{\partial (u_i u_j)}{\partial x_j} = -\frac{1}{\rho}\frac{\partial p}{\partial x_i} + \frac{1}{\rho}\frac{\partial \sigma_{ij}}{\partial x_j}, \qquad \sigma_{ij}^s = 2\mu^s D_{ij} + \begin{cases} G^s(B_{ij} - \delta_{ij}) & \text{(Neo-Hooke, Gl. 14)} \\ G^s(B_{ik}B_{kj} - B_{ij}) & \text{(St. Venant-Kirchhoff, Gl. 15)} \end{cases}$$
  2. **Oldroyd-Transport des linken Cauchy-Green-Tensors $\mathbf{B} = \mathbf{F}\mathbf{F}^T$ mit WENO-Z & Fluid-Reset (Gl. 16–18, 30–39):**
     $$\frac{\partial B_{ij}}{\partial t} + u_k \frac{\partial B_{ij}}{\partial x_k} = B_{ik}\frac{\partial u_j}{\partial x_k} + B_{kj}\frac{\partial u_i}{\partial x_k} \quad \text{(Gl. 16)}$$
     Zeitintegration via Adams-Bashforth 2 ($\text{CFL} < 0.5$, Gl. 18) und 5.-Ordnung WENO-Z für $u_k \partial_{x_k} B_{ij}$ (Gl. 31–39).
     **Entscheidender Stabilitäts-Mechanismus:** Nach der direktional gesplitteten Lagrangeschen PLIC-Advektion ($m_x x + m_y y + m_z z = \beta$, Gl. 29) wird für alle reinen Fluidzellen ($\phi < \epsilon_c = 10^{-8}$) $\phi = 0$ geclippt und **hart $\mathbf{B} = \mathbf{I}$ zurückgesetzt** — das verhindert das exponentielle Anwachsen von $\mathbf{B}$ durch Fluidscherung und erreicht auf einem $128^2$-Gitter dieselbe Schärfe wie diffusive WENO-FSI-Löser auf $1024^2$ ($512\times$ weniger 3D-Zellen!).

---

### 9.16 [`CADWatertightFineMeshing_2604.02141v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CADWatertightFineMeshing_2604.02141v1.pdf) — Topology-First B-Rep Meshing (100 % Wasserdichter STEP-Import ohne 2D-Trimmkurven)
* **Autoren & Quelle:** Y. Zhou, D. Zint, N. Izadyar, M. Tao, D. Panozzo, T. Schneider (NYU, TU Berlin, Univ. Victoria, April 2026), *„Topology-First B-Rep Meshing“*, `arXiv:2604.02141v1 [cs.GR]`.
* **Kernproblem & Durchbruch:**
  In realen STEP-Dateien (Fusion-360-/ABC-Datensatz) schneiden sich **14 % aller 2D-$(u,v)$-Trimmkurven (`PCURVE`) selbst** oder klaffen auseinander, weshalb klassische $(u,v)$-Delaunay-Mesher (Gmsh, OCCT) Risse oder Abstürze produzieren. *Topology-First B-Rep Meshing* **verwirft alle 2D-Trimmkurven komplett** und behandelt die kombinatorische B-Rep-Topologie $\mathcal{P} = (\{v_i^b\}, \{e_i^b\}, \{\ell_i^b\}, \{f_i^b\})$ als unverletzliche Invariante:
  1. **Ungetrimmtes Patch-Sampling (Stage 1–2):** Jedes NURBS-Patch $p_i^2(u,v): [0,1]^2 \to \mathbb{R}^3$ wird auf dem vollen ungetrimmten Einheitsquadrat $[0,1]^2$ vernetzt und per RGB-Subdivision auf Abstand $< \epsilon$ zur Fläche verfeinert; 3D-Randkurven $p_i^1(t)$ werden auf die exakten 3D-Eckpunkte $p_i^0$ gesnappt (1D-Laplace-Glättung).
  2. **Topologie-garantiertes 3D Loop-Embedding via Dijkstra (`Algorithm 1 & 2`, Stage 3):**
     Nicht-mannigfaltige b-vertices/Seam-Edges werden temporär dupliziert (sodass jeder Loop eine topologische Disk begrenzt), die 3D-Kurvenpunkte per Closest-Point auf das 3D-Flächennetz $M_k$ projiziert und durch **kürzeste Pfade im Kantengraphen von $M_k$ per Dijkstra** (unter Umgehung bereits belegter Randkanten $\mathcal{F}$) verbunden. Der geschlossene Zyklus $\Gamma$ zerlegt $M_k$ nach dem Jordan-Kurvensatz rein kombinatorisch in Disk (`KeepDisk` für Außenloop) und Annulus (`KeepComplement` + `RestoreDisk` für Innenlöcher) — **komplett ohne geometrische In/Out-Tests**!
  3. **Harmonisches Snapping $\Delta u = 0$ & 1D-$t$-Parameter-Stitching (Stage 4–5):**
     Randknoten werden auf $p_j^1(t_k)$ gezogen, das Innere über die diskrete harmonische Gleichung $\Delta u = 0$ mit $u|_{\partial M_k} = u_c$ entspannt und die 1D-Kurvenparameter $\{t_k\} \subset [0,1]$ der beiden Nachbarflächen auf jeder `EDGE_CURVE` vereinigt und gesplittet $\implies$ **garantiert 100 % wasserdichte, 2-mannigfaltige Oberflächengitter**.

---

### 9.17 [`Differtialble3DCAD_2110.01182v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Differtialble3DCAD_2110.01182v1.pdf) — Differenzierbare 3D-CAD-Programme & Automatische Gültigkeitsschranken
* **Autoren & Quelle:** D. Cascaval, M. Shalah, P. Quinn, R. Bodik, M. Agrawala, A. Schulz (Univ. Washington & Stanford, SIGGRAPH Asia 2021), *„Differentiable 3D CAD Programs for Bidirectional Editing“*, *ACM TOG*, `arXiv:2110.01182v1`.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–13 & Appendix A):**
  1. **Kompilierter Berechnungsgraph (DAG) $G$ statt AD über den CAD-Kernel (§3.1–3.2, Abb. 4):**
     Bei fester Topologie $(V, E, F)$ zeichnet der CAD-Interpreter während des einmaligen Geometrieaufbaus für jede Operation (`Box`, `Chamfer`, `Extrude`, `Revolve`) den symbolischen Berechnungsgraphen $V = \mathcal{F}(P)$ der 3D-Oberflächenknoten in Abhängigkeit der Konstruktionsparameter $P \equiv \boldsymbol{\theta}_{\text{CAD}}$ auf (z. B. Fase $V_5 = V_3 + d\frac{V_1 - V_3}{\|V_1 - V_3\|_2}$) und erzeugt **automatisch die topologischen Sicherheits-Ungleichungen** $g_m(P) \ge 0$ (z. B. $\|V_4 - V_3\|_2 - d \ge 0$, damit eine Fase niemals die Kantenlänge überschreitet!).
  2. **Direkte Adjoint-Kettenregel & Biharmonische/ARAP-Regularisierung im Parameterraum (§3.3, Gl. 1–13, App. A):**
     Kompiliert $\mathcal{F}(P)$, $\nabla\mathcal{F}(P) = \frac{\partial \mathbf{x}_{\text{surf}}}{\partial \boldsymbol{\theta}_{\text{CAD}}}$ und $\nabla g_m(P)$ in nativen Maschinencode (0 CAD-Kernel-Aufrufe in der Optimierungsschleife!). Koppelt unsere FVM-Adjoint-Oberflächensensitivitäten direkt über
     $$\frac{\mathrm{d}J}{\mathrm{d}\boldsymbol{\theta}_{\text{CAD}}} = \left(\frac{\partial J}{\partial \mathbf{x}_{\text{surf}}}\right)^T \frac{\partial \mathbf{x}_{\text{surf}}}{\partial \boldsymbol{\theta}_{\text{CAD}}} \qquad \text{u. d. N.} \quad g_m(\boldsymbol{\theta}_{\text{CAD}}) \ge 0$$
     sowie optionaler biharmonischer Deformationsenergie $E_{\text{bh}}(P) = \operatorname{tr}(\mathbf{D}(P)^T \mathbf{Q}\mathbf{D}(P))$ (Gl. 7) und Volumenerhaltung $E_{\text{vol}}(P) = (\operatorname{Vol}(P) - \operatorname{Vol}(P_0))^2$ (Gl. 11) via SLSQP.

---

### 9.18 [`coupledMFRSteadyIncFoam-Final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/coupledMFRSteadyIncFoam-Final.pdf) — Voll-Impliziter $4\times 4$ Block-Coupled $(\mathbf{u}, p)$ FVM-Löser für Rotierende Bezugssysteme (MRF), Moving Walls & Direct-AMI
* **Autoren & Quelle:** L. Mangani, M. Buchmayr, M. Darwish (Hochschule Luzern, Andritz AG / TU Graz, American Univ. of Beirut, 2014), *„Development of a Novel Fully Coupled Solver in OpenFOAM®: Steady State Incompressible Turbulent Flows in Rotational Reference Frames“*, *Numerical Heat Transfer, Part B: Fundamentals* 66(1), 1–20.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–20):**
  1. **Zentrifugalkraft-freie MRF-Formulierung (Absolutgeschwindigkeit $\mathbf{u}$ mit Relativfluss $\dot{V}_r$, Gl. 3–6):**
     Mit $\mathbf{u} = \mathbf{u}_r + \mathbf{\Omega}\times\mathbf{r}$ (Gl. 3) und dem relativen Flächenvolumenstrom $\dot{V}_r = \dot{V} - (\mathbf{\Omega}\times\mathbf{r}_f)\cdot\mathbf{S}_f$ (Gl. 6) entfällt der ortsabhängige Zentrifugal-Quellterm $\mathbf{\Omega}\times(\mathbf{\Omega}\times\mathbf{r})$ komplett:
     $$\nabla\cdot\mathbf{u} = 0 \quad \text{(Gl. 4)}, \qquad \nabla\cdot(\mathbf{u}_r\mathbf{u}) + \mathbf{\Omega}\times\mathbf{u} = -\frac{1}{\rho}\nabla p + \nabla\cdot(\nu_{\text{eff}}\nabla\mathbf{u}) \quad \text{(Gl. 5)}$$
  2. **Impliziter $4\times 4$ Block-Matrix-Operator & Schiefsymmetrischer Coriolis-Block ($\mathbf{\Omega}\times\mathbf{u}$, Gl. 9 & §3.1):**
     Für jede Zelle $C$ und ihre Nachbarn $NB$ wird ein gekoppeltes $4\times 4$-System $\mathbf{A}_C [u_C, v_C, w_C, p_C]^T + \sum_{NB} \mathbf{A}_{NB} [u_{NB}, v_{NB}, w_{NB}, p_{NB}]^T = \mathbf{b}_C$ assembliert (Gl. 9). Der Term $\int_{\Delta V_C}(\mathbf{\Omega}\times\mathbf{u})\,\mathrm{d}V$ erzeugt einen **exakten schiefsymmetrischen $3\times 3$-Nebendiagonalblock direkt auf dem Hauptdiagonalblock $\mathbf{A}_C^{uu}$**:
     $$a_{C,\text{rot}}^{uv} = -\Omega_z \Delta V_C, \quad a_{C,\text{rot}}^{uw} = +\Omega_y \Delta V_C, \quad a_{C,\text{rot}}^{vu} = +\Omega_z \Delta V_C, \quad a_{C,\text{rot}}^{vw} = -\Omega_x \Delta V_C, \quad a_{C,\text{rot}}^{wu} = -\Omega_y \Delta V_C, \quad a_{C,\text{rot}}^{wv} = +\Omega_x \Delta V_C$$
  3. **Implizite Tangential-Projektion an bewegten/rotierenden Wänden ($\mathbf{I} - \mathbf{n}_b\otimes\mathbf{n}_b$, Gl. 10–15):**
     An einer No-Slip-Wand $b$ mit Einheitsnormale $\mathbf{n}_b = \mathbf{S}_b/\|\mathbf{S}_b\|$ wirkt die Wandschubspannung nur auf die wandparallele Komponente $\mathbf{u}_\parallel = (\mathbf{I} - \mathbf{n}_b\otimes\mathbf{n}_b)\mathbf{u}$ (Gl. 12–13) und koppelt alle 3 Geschwindigkeitskomponenten implizit auf $\mathbf{A}_{C,b}^{uu}$ (Gl. 14–15):
     $$\mathbf{A}_{C,b}^{uu} \mathrel{+}= \frac{\nu_{\text{eff}}\|\mathbf{S}_b\|}{d_\perp}\begin{bmatrix} 1-n_{b,x}^2 & -n_{b,x}n_{b,y} & -n_{b,x}n_{b,z} \\ -n_{b,y}n_{b,x} & 1-n_{b,y}^2 & -n_{b,y}n_{b,z} \\ -n_{b,z}n_{b,x} & -n_{b,z}n_{b,y} & 1-n_{b,z}^2 \end{bmatrix}, \qquad \mathbf{b}_{C,b}^{\mathbf{u}} \mathrel{+}= \frac{\nu_{\text{eff}}\|\mathbf{S}_b\|}{d_\perp}\mathbf{u}_{\parallel,\text{wall}}$$
  4. **$4\times 4$ Rodrigues-Rotationsmatrix $\mathbf{T}_{NB}$ für `cyclic` (Gl. 16–17) & Direkte Sub-Face-AMI-Kopplung (Gl. 18–19):**
     * Bei rotationsperiodischen `cyclic`-Patches (Drehwinkel $\alpha$ um Achse $\boldsymbol{\omega}$) wird der geometrische Nachbarblock von rechts mit der $4\times 4$-Rodrigues-Matrix multipliziert: $\mathbf{A}_{NB} = \mathbf{A}_{NB,\text{geom}}\cdot\mathbf{T}_{NB}(\boldsymbol{\omega},\alpha)$ (Gl. 16–17).
     * Bei nicht-konformen `AMI`/`cyclicAMI`-Interfaces (z. B. Kaplan-Schaufel-Spaltblöcke) wird statt einer virtuellen Schatten-Zelle jede Master-Fläche $\mathbf{S}_f$ gemäß den Überlappungsgewichten $w_{f_k}$ ($\sum_k w_{f_k}=1$) direkt mit allen überlappenden Slave-Zellen $NB_k$ als virtuelle interne Sub-Faces in der globalen $4\times 4$-Blockmatrix verknüpft (z. B. Rhie-Chow-Druckblock $a_{NB_k}^{pp} = -w_{f_k}\frac{\mathbf{S}_f\cdot\overline{\mathbf{D}}_f\cdot\mathbf{S}_f}{\mathbf{d}_k\cdot\mathbf{S}_f}$, Gl. 18–19).
  5. **Ergebnis (Tab. 1–4):** Nahezu lineare $\mathcal{O}(N)$-Gitterskalierung und **$6.1\times\text{ bis }35.1\times$ kürzere Rechenzeit** als `simpleFoam` (SIMPLE) auf Pumpen-, Francis- und Kaplan-Turbinenläufern.

---

### 9.19 [`Coupled_Solver_1-s2.0-S0045793023002207-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Coupled_Solver_1-s2.0-S0045793023002207-main.pdf) — Massiv-Paralleler Monolithischer Zweiphasen-Löser mit Block-Gauss-Seidel & PCD-Schur-Vorkonditionierer
* **Autoren & Quelle:** S. El Ouafa, S. Vincent, V. Le Chenadec, B. Trouette (Université Gustave Eiffel, 2023), *„Fully-coupled parallel solver for the simulation of two-phase incompressible flows“*, *Computers and Fluids* 265, 105995, DOI: `10.1016/j.compfluid.2023.105995`.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–24 & App. A–C):**
  1. **One-Fluid-Modell (OFM) mit Darcy-Brinkman-Strafoperator & Caltagirone-Vincent-Spannungszerlegung (Gl. 2–6, A.1–A.8, B.1–B.5):**
     Löst das monolithische Sattelpunktsystem $\begin{bmatrix} F_{\mathbf{u}} & B_p^T \\ B_u & 0 \end{bmatrix}\begin{pmatrix}\mathbf{u}^{n+1}\\p^{n+1}\end{pmatrix} = \begin{pmatrix}\mathbf{f}\\0\end{pmatrix}$ ($F_{\mathbf{u}} = M_u^{(\rho)} + N_u^{(\rho)} + L_u^{(\mu)}$, Gl. 4–6) mit dem tensoriellen Darcy-Brinkman-Strafoperator $\bar{\bar{\mathbf{B}}}\cdot(\mathbf{f}(\mathbf{u}^{n+1})-\mathbf{u}_\infty)$ (Gl. 2a, B.1–B.5). Für extreme Viskositätssprünge ($\mu_2/\mu_1 = 10^6$) wird der Spannungstensor über $T_{lm} = \kappa\Lambda_{lm} + \zeta\Xi_{lm} - \eta\Gamma_{lm}$ ($\kappa=\zeta=2\mu, \eta=\mu$, Gl. A.3) exakt in Dehnungs- ($\Lambda$, Gl. A.4), Scher- ($\Xi$, Gl. A.5) und implizite Rotations-Kreuzableitungen ($-\eta\Gamma$, Gl. A.6 $\implies$ Nebendiagonalblöcke $F_{uv}, F_{uw}, F_{vw}$) zerlegt.
  2. **Block-LU-Faktorisierung & Neumann-Reihen-Geschwindigkeits-Vorkonditionierer (Gl. 7–19, Alg. 1):**
     Im oberen Block-Dreiecks-Vorkonditionierer $\mathcal{P} = \begin{bmatrix} F_{\mathbf{u}} & B_p^T \\ 0 & S_p \end{bmatrix}$ ($S_p = -B_u F_{\mathbf{u}}^{-1}B_p^T$, Gl. 7–8) wird der $3\times 3$-Geschwindigkeitsblock über die Neumann-Reihe $(I - G)^{-1} = \sum_{k=0}^\infty (F_{ww}^{-1}A_{wvu}A_{uv}^{-1}A_{uvw})^k$ (Gl. 12–14) approximiert. Die **Ordnung-0-Trunkierung (Block-Gauss-Seidel $\tilde{F}_{\mathbf{u}}^{(0)} = \begin{bmatrix} F_{uu} & F_{uv} & F_{uw} \\ 0 & F_{vv} & F_{vw} \\ 0 & 0 & F_{ww} \end{bmatrix}$, Gl. 19a)** schlägt sowohl Block-Jacobi als auch Ordnung-1 ($\tilde{F}_{\mathbf{u}}^{(1)}$, Gl. 19b) in der Gesamtlaufzeit.
  3. **Variabel-Koeffizienten Two-Phase PCD Schur-Komplement & $\text{BiCGStab}(2)$ (Gl. 20–21, Alg. 2–3):**
     Das inverse Druck-Schur-Komplement wird über den zweiphasigen *Pressure Convection-Diffusion (PCD)*-Operator approximiert:
     $$\hat{S}_{PCD}^{-1} = \left(M_p^{(1/\mu)}\right)^{-1} + \left(A_p^{(1/\rho)}\right)^{-1}\left(N_p^{(1)} + \Delta t^{-1}M_p^{(1)}\right)\left(M_p^{(1)}\right)^{-1} \quad \text{(Gl. 20–21)}$$
     mit dem variabel-dichten Druck-Laplacian $A_p^{(1/\rho)}\phi = \int_{\Omega_p}\nabla\cdot(\rho^{-1}\nabla\phi)\,\mathrm{d}V$, der viskositätsgewichteten Druck-Massenmatrix $M_p^{(1/\mu)}\phi = \int_{\Omega_p}\mu^{-1}\phi\,\mathrm{d}V$ und dem Druck-Konvektionsoperator $N_p^{(1)}\phi = \int_{\Omega_p}\nabla\cdot(\mathbf{u}\phi)\,\mathrm{d}V$. Pro $\mathcal{P}^{-1}$-Aufruf (Alg. 2) im links-vorkonditionierten $\text{BiCGStab}(2)$ (Alg. 3) sind nur **4 skalare AMG/PFMG-Lösungen** ($A_p^{(1/\rho)}, F_{ww}, F_{vv}, F_{uu}$) nötig $\implies$ **96 % starke Skalierung auf 6.400 Cores**, **1,1 Mrd. Zellen** im Weak-Scaling und stabile Konvergenz bei **$\rho_2/\rho_1 = \mu_2/\mu_1 = 10^6$** sowie $4\text{–}10\times$ größeren Zeitschritten als OpenFOAM `interFoam`.

---

### 9.20 [`CoupledSolver_RWTH_1010342.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CoupledSolver_RWTH_1010342.pdf) — Zero-Copy SPMD-Kopplung Reaktiver CFD-Solver mit GPU-Deep-Learning-Inferenz (`AIxeleratorService`)
* **Autoren & Quelle:** F. Orland, L. Nista, N. Kocher, J. Vanvinckenroye, H. Pitsch, C. Terboven (RWTH Aachen University, Feb. 2025), *„Efficient and Scalable AIxeleration of Reactive CFD Solvers Coupled with Deep Learning Inference on Heterogeneous Architectures“*, *HPC Asia 2025 Workshops*, S. 45–57, DOI: `10.1145/3703001.3724386`.
* **Mathematische & Architektonische Kern-Bausteine (Gl. 1 & §3–5):**
  1. **In-Situ Gekoppelte LES-Turbulenz- ($\tau_{ij}$) & Reaktive Wasserstoff-Flammen-Schließung ($\tilde{\dot{\omega}}_C$) in jedem RK-Subschritt (§2.1–2.2):**
     * *SR-GAN Dekonvolution für den Subfilter-Reynolds-Spannungstensor $\tau_{ij} = \overline{u_i u_j} - \bar{u}_i\bar{u}_j$:* Rekonstruiert das ungefilterte Geschwindigkeitsfeld $u_i^\bullet = \mathcal{G}_l^{-1} * \bar{u}_i = \mathcal{G}_l^{-1} * \mathcal{G} * u_i$ (Gl. 1) auf einem $4\times$ feineren Hilfsgitter ($64^3 \to 256^3$) und filtert $u_i^\bullet u_j^\bullet$ zurück auf das LES-Gitter.
     * *UNet++ CNN für magere $\text{H}_2$-Verbrennung:* Sagt aus der gefilterten Reaktionsfortschrittsvariable $\tilde{C}$ direkt die thermodiffusiv instabile Quellrate $\tilde{\dot{\omega}}_C$ vorher.
  2. **SPMD `AIxeleratorService`-Architektur vs. MPMD (`PhyDLL`, §4.2, Abb. 4–5):**
     * Statt separate Python-Prozesse im MPMD-Modus zu starten (was 50 % der CPU-Cores blockiert und bei 4 H100-GPUs **$69.6\,\text{ms}$ Python-Host-Overhead** pro Schritt erzeugt, Abb. 10), läuft `AIxeleratorService<T>` als **reine C++-SPMD-Bibliothek** innerhalb der MPI-Ränge des CFD-Solvers.
     * Teilt `MPI_COMM_WORLD` per `RoundRobinDistribution` in 1 `work_group_comm` pro GPU, bündelt die lokalen Teilgebiete über **Zero-Copy-Pointer-Attachment (`T* input_data_`) + `FP32` `MPI_Gatherv`/`MPI_Scatterv`** (50 % weniger InfiniBand-Traffic) auf 1 GPU-Server-Rank pro Workgroup und ruft direkt die nativen **C/C++-Inferenz-APIs (`LibTorch`, `TF_SessionRun`, `ONNX Runtime Ort::Session`)** auf (**nur $2\,\text{ms}$ Host-Overhead $\implies 3.5\times\text{ bis }9.6\times$ schnellere GPU-Inferenz** auf 32 H100-GPUs!).
  3. **Direkte Blaupause für unser Rust-Node-Manager $\leftrightarrow$ C++20-Mesher/Solver FFI:**
     Die Messdaten des RWTH IT Centers & ITV belegen, warum lose gekoppelte MPMD-Prozesse ($69.6\,\text{ms}$ Overhead) scheitern und **Zero-Copy-Pointer-Übergabe im selben Prozessraum ($2\,\text{ms}$ Overhead)** gewinnt. Exakt dieses **SPMD-In-Memory-Prinzip** setzen wir über unseren `CAbiSpan` Data-Lake (`(void* ptr, size_t len, size_t stride, uint32_t dtype, uint32_t device_id)`) zwischen dem Rust-Orchestrator/Node-Manager und dem C++20-Mesher, FVM-Solver und C-API-Surrogat-Evaluator um.

---

### 9.21 [`CAD_Optimization_1-s2.0-S0045793011000508-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CAD_Optimization_1-s2.0-S0045793011000508-main.pdf) — Direkte STEP B-Rep NURBS-Formoptimierung mit Hybrider Reverse/Forward-AD-Adjoint-Kettenregel
* **Autoren & Quelle:** G. Yu, J.-D. Müller, D. Jones, F. Christakopoulos (Queen Mary Univ. of London, 2011), *„CAD-based shape optimisation using adjoint sensitivities“*, *Computers & Fluids* 46, 512–516, DOI: `10.1016/j.compfluid.2011.01.043`.
* **Mathematische & Algorithmische Kern-Bausteine (Gl. 1–12):**
  1. **4-Stufige Adjoint-Kettenregel & Hybride Reverse/Forward-AD-Aufteilung (Gl. 1–6, Abb. 1):**
     Für das Zielfunktional $L = L(U(X(X_B(\alpha))))$ (Gl. 3) mit den STEP-NURBS-Kontrollpunkten $\alpha \equiv \{\mathbf{P}_{i,j}\}$ als Designvariablen und der adjungierten FVM-Gleichung $A^T v = g = (\frac{\partial L}{\partial U})^T$ ($A = \frac{\partial R}{\partial U}$, Gl. 2, 6) lautet die exakte Gesamtableitung:
     $$\frac{\mathrm{d}L}{\mathrm{d}\alpha} = \underbrace{\left[\left(\frac{\partial L}{\partial X} - v^T\frac{\partial R}{\partial X}\right)\frac{\partial X}{\partial X_B}\right]}_{\substack{\text{Oberflächenknoten-Sensitivität } \mathrm{d}L/\mathrm{d}X_B \\ \text{(Reverse-Mode AD auf FVM + Netzdeformation)}}} \cdot \underbrace{\frac{\partial X_B}{\partial \alpha}}_{\substack{\text{B-Rep NURBS-Jacobi-Matrix} \\ \text{(Forward-Mode AD auf STEP-Evaluator)}}}$$
  2. **Lokale Knotenintervall-Sparsity von NURBS-Flächen & $G^1/C^2$-Kollinearitätsbedingung (Gl. 7–11, Abb. 3):**
     Für eine aus einer ISO-10303-STEP-Datei gelesene rationale NURBS-Fläche $\mathbf{X}_s(u,v) = \sum_{i=0}^n\sum_{j=0}^m R_{i,j}(u,v)\mathbf{P}_{i,j}$ mit $R_{i,j}(u,v) = \frac{N_{i,p}(u)N_{j,q}(v)w_{i,j}}{\sum_{k,l}N_{k,p}(u)N_{l,q}(v)w_{k,l}}$ (Gl. 7–8) sind im lokalen Knotenintervall $(\text{span}_u, \text{span}_v)$ höchstens $(p+1)(q+1)$ Basisfunktionen von Null verschieden (Gl. 10), und die exakte Ableitung nach jedem Kontrollpunkt lautet geschlossen:
     $$\frac{\partial \mathbf{X}_s(u,v)}{\partial \mathbf{P}_{i,j}} = R_{i,j}(u,v)\,\mathbf{I}_3 \quad \text{(Gl. 11)}, \qquad \frac{\partial \mathbf{X}_s(u,v)}{\partial w_{i,j}} = \frac{R_{i,j}(u,v)}{w_{i,j}}\big(\mathbf{P}_{i,j} - \mathbf{X}_s(u,v)\big)$$
     An Patch-Übergängen (z. B. Profil-Vorderkante $\mathbf{P}_0$) erzwingt die **Kollinearität von $(\mathbf{P}_{1,\text{bot}}, \mathbf{P}_0, \mathbf{P}_{1,\text{top}})$** (Abb. 3, nach Farin) automatisch $G^1/C^2$-Krümmungsstetigkeit und Wasserdichtheit.
  3. **Mehrwert gegenüber Knoten-basierter Optimierung & CAD-Finite-Differenzen (§3.2–4, Tab. 1, Abb. 6–7):**
     * Berechnet $\frac{\partial X_B}{\partial\alpha}$ auf **10+ Dezimalstellen exakt** in **0,24 s** (**$75.000\times$ schneller** als 3 Stunden CATIA-V5-Finite-Differenzen).
     * Die kubische B-Spline-Basis wirkt als **intrinsischer $C^2$-Tiefpassfilter** (keine künstliche Gradientenglättung nötig!) und schreibt die optimierten Kontrollpunkte direkt als fertige ISO-STEP-B-Rep-Datei zurück.

---

### 9.22 Master-Synthese: Wie alle 21 Teilkapitel als 2-Modi-Mesher-Pipeline auf demselben Polyeder-Graphen $\mathcal{G} = (\mathcal{V}, \mathcal{E})$ zusammenspielen

Statt die vier im Kompendium vertretenen Vernetzungs-Philosophien (*VoroCrust* in 9.1, *Mosaic Poly-Hexcore/PUMA* in 9.2, *fTetWild/WSRD Cut-Cells* in 9.3/9.4 und *Super Elements* in 9.9) in einen einzigen überladenen Codepfad zu zwingen, operiert unser C++20-Mesher als **saubere 2-Modi-Pipeline**, die in beiden Modi exakt denselben **Hilbert-sortierten, 4-Quadranten-gerichteten Flächen-Zellen-Graphen $\mathcal{G} = (\mathcal{V}, \mathcal{E})$** (`FaceBaseSoA` + 6 % `CorrectionSoA` + atomikfreies `CellFaceCSR`) ausgibt:

| Einsatzfall | Optimaler Pipeline-Pfad aus Kapitel 9 | Warum genau diese Kombination? |
| :--- | :--- | :--- |
| **Modus A: Externe Aerodynamik, Turbomaschinen, CHT & Verbrennung ($y^+ \le 1$)** | **Topology-First B-Rep (9.16)** $\longrightarrow$ **Loseille/Marcon Kollisionsfreie Prismen (9.7)** $\longrightarrow$ **Mosaic Poly-Hexcore + PUMA / Stromberg Super-Elements (9.2 / 9.9)** $\longrightarrow$ **$L^p$-Metrik- & Adjoint-Adaption (9.6 / 9.10)** | Liefert maximale Genauigkeit in Wandgrenzschichten ($y^+ \le 1$), verschwindende numerische Diffusion im orthogonalen Hexcore-Fernfeld ($\theta_f = 0^\circ$) und 100 % flächenkonforme Polyeder-/Super-Element-Übergänge ($\min SJ \ge 0.302$) ohne Hanging Nodes. |
| **Modus B: Dirty-CAD, Topologieoptimierung (SIMP), Ventile & große FSI-Bewegung** | **fTetWild $\epsilon$-Envelope / VoroCrust / GPU-Voronoi (9.1 / 9.3 / 9.12)** *oder* **WSRD Cut-Cells (9.4)** $+$ **Jaiswal 3-Ring WSS (9.5)** $+$ **JamTet / TINE / Simpson-BDF2-DGCL / Monolithic PLIC-FSI (9.8 / 9.11 / 9.13 / 9.15)** | 100 % robust bei kaputten STL/CAD-Geometrien, SIMP-Isoflächen ($\gamma = 0.5$) und Topologiewechseln; kein Kollabieren von Grenzschicht-Prismen bei Ventilkontakt oder extremer Strukturverformung, während WSRD ($\text{CFL}=1$) und Jaiswals $24\times 24$-Patch-Rekonstruktion exakte Wandkräfte sichern. |

**Einheitlicher Ausgang beider Modi zum Solver & Optimizer:**
Beide Pipelines münden in denselben **C-ABI SoA Data-Lake (`.mphys`)**, auf dem sowohl unser segregierter/gekoppelter **DiFVM-Graph-Solver (Kap. 6)**, der **$4\times 4$ Block-Coupled MRF-/Zweiphasen-Löser mit Block-Gauss-Seidel & PCD-Schur-Vorkonditionierer (Kap. 9.18 & 9.19)**, die **Householder-Symmetrie- & Festkörper-FVM (Kap. 9.14)** als auch der **hybride Reverse-FVM / Forward-CAD/NURBS-Adjoint-Optimizer (Kap. 7, 9.17 & 9.21)** ohne jede Weiche im Solver-Code arbeiten.

---

### 9.23 Coupled vs. Segregated (SIMPLE / SIMPLEC / PISO): Der Paradigmenwechsel zum Matrix-Free / Block-Coupled Newton-Krylov-Solver mit SIMPLE/AL/CPR/PCD als Block-Vorkonditionierer
* **Kern-Erkenntnis der modernen CFD-Algebra (Klaij & Vuik 2013; Moukalled, Mangani & Darwish 2016; Denner & van Wachem 2018; El Ouafa et al. 2023):**
  Klassische segregierte Verfahren (**SIMPLE**, **SIMPLEC**, **PISO**, **PIMPLE**) sind **keine** konkurrierenden Gleichungslöser zum Coupled Solver, sondern schlicht **unvollständige Block-LU-Zerlegungen derselben gekoppelten Sattelpunktmatrix**
  $$\mathcal{J}\,\begin{bmatrix} \Delta \mathbf{u} \\ \Delta p \end{bmatrix} = \begin{bmatrix} \mathbf{A}_{\mathbf{u}\mathbf{u}} & \mathbf{A}_{\mathbf{u}p} \\ \mathbf{A}_{p\mathbf{u}} & \mathbf{A}_{pp} \end{bmatrix} \begin{bmatrix} \Delta \mathbf{u} \\ \Delta p \end{bmatrix} = -\begin{bmatrix} \mathbf{R}_{\mathbf{u}} \\ \mathbf{R}_p \end{bmatrix}$$
  Approximiert man im exakten Block-LU-Faktor $\mathcal{J} = \begin{bmatrix} \mathbf{A}_{\mathbf{u}\mathbf{u}} & \mathbf{0} \\ \mathbf{A}_{p\mathbf{u}} & \mathbf{S} \end{bmatrix} \begin{bmatrix} \mathbf{I} & \mathbf{A}_{\mathbf{u}\mathbf{u}}^{-1}\mathbf{A}_{\mathbf{u}p} \\ \mathbf{0} & \mathbf{I} \end{bmatrix}$ die teure Inverse $\mathbf{A}_{\mathbf{u}\mathbf{u}}^{-1}$ im Schur-Komplement $\mathbf{S} = \mathbf{A}_{pp} - \mathbf{A}_{p\mathbf{u}}\mathbf{A}_{\mathbf{u}\mathbf{u}}^{-1}\mathbf{A}_{\mathbf{u}p}$ und im oberen Dreiecksfaktor durch eine Diagonalmatrix $\mathbf{D}_{\mathbf{u}\mathbf{u}}^{-1}$, erhält man exakt einen **SIMPLE- bzw. SIMPLEC-Schritt als Rechts-Vorkonditionierer $\mathcal{P}_{\text{SIMPLE}}$**:
  $$\mathcal{P}_{\text{SIMPLE}} = \begin{bmatrix} \mathbf{A}_{\mathbf{u}\mathbf{u}} & \mathbf{0} \\ \mathbf{A}_{p\mathbf{u}} & \tilde{\mathbf{S}}_{\text{SIMPLE}} \end{bmatrix} \begin{bmatrix} \mathbf{I} & \mathbf{D}_{\mathbf{u}\mathbf{u}}^{-1}\mathbf{A}_{\mathbf{u}p} \\ \mathbf{0} & \mathbf{I} \end{bmatrix}, \qquad \tilde{\mathbf{S}}_{\text{SIMPLE}} = \mathbf{A}_{pp} - \mathbf{A}_{p\mathbf{u}}\mathbf{D}_{\mathbf{u}\mathbf{u}}^{-1}\mathbf{A}_{\mathbf{u}p}$$
  wobei $\mathbf{D}_{\mathbf{u}\mathbf{u}}^{\text{SIMPLE}} = \operatorname{diag}(a_{PP})$ und $\mathbf{D}_{\mathbf{u}\mathbf{u}}^{\text{SIMPLEC}} = \operatorname{diag}\!\big(a_{PP} + \sum_{N} a_{PN}\big)$ gilt (und **PISO** einer 2-stufigen Neumann-Reihenentwicklung von $\mathbf{A}_{\mathbf{u}\mathbf{u}}^{-1}$ entspricht).
* **Warum äußere segregierte SIMPLE/SIMPLEC/PISO-Schleifen auf unserem Ziel-Stack an harte Grenzen stoßen:**
  1. **Gitter-Stagnation bei hohen Aspektverhältnissen ($1000:1$ Grenzschicht-Prismen, $y^+ \le 1$):** Da $\mathbf{D}_{\mathbf{u}\mathbf{u}}^{-1}$ auf anisotropen Prismen-/Mosaic-Gittern die Nebendiagonalen $\mathbf{A}_{\mathbf{u}\mathbf{u}} - \mathbf{D}_{\mathbf{u}\mathbf{u}}$ unterschlägt, erfordert äußeres SIMPLE starke Unterrelaxation ($\alpha_p \approx 0.2\text{–}0.3, \alpha_u \approx 0.7$) und stagniert bei $3\,000\text{–}10\,000$ Iterationen, während der gekoppelte Löser **linear $\mathcal{O}(N)$ in $50\text{–}150$ Iterationen** konvergiert (vgl. Kap. 9.18, Tab. 1–2).
  2. **Steife Volumenkräfte & Tensor-Kopplungen:** Coriolis-Kräfte ($\mathbf{\Omega}\times\mathbf{u}$ in MRF, Kap. 9.18), Lorentz-Kräfte ($\mathbf{J}\times\mathbf{B}$ in MHD), Auftrieb ($\rho\mathbf{g}$ in CHT), Oberflächenspannung ($\sigma\kappa\mathbf{n}\delta_s$, Kap. 9.19) und steife Darcy-Forchheimer-/SIMP-Brinkman-Strafen ($\bar{\bar{\mathbf{B}}}\mathbf{u}$) koppeln $u, v, w$ und $p$ lokal in derselben Zelle.
  3. **All-Mach-Kompressibilität & Kavitation ($\text{Ma} = 10^{-4} \to 3.0$):** Bei hoher Mach-Zahl oder Schnerr-Sauer-Kavitation ($\partial\dot{m}/\partial p$) wechselt die Druckgleichung von elliptisch zu hyperbolisch (Konvektions-Diffusions-Gleichung).
  4. **Adjoint-Optimierung (TopOpt & STEP-NURBS-Shape-Opt):** Ein einziger gekoppelter Block-Operator $\mathcal{J} = \frac{\partial\mathbf{R}}{\partial\mathbf{U}}$ erlaubt die direkte adjungierte Lösung $\mathcal{J}^T\boldsymbol{\psi} = -\left(\frac{\partial L}{\partial\mathbf{U}}\right)^T$ ohne instabile segregierte Adjoint-SIMPLE-Fixpunktschleifen.
* **Die 5 komplementären Coupled-Solver-Architekturen für unseren C++20/CUDA-Stack (Ergänzung zu Kap. 9.18 & 9.19):**
  1. **$5\times 5$ Pressure-Based All-Mach Coupled Solver (Darwish, Sraj & Moukalled 2009–2020; Denner, Xiao & van Wachem 2018/2020):**
     Erweitert das inkompressible $4\times 4$-System $(u,v,w,p)$ aus Kap. 9.18 um die Energie-/Enthalpiegleichung ($T$ bzw. $h$) auf **$5\times 5$-Blöcke** und linearisiert den kompressiblen Massenfluss $\dot{m}_f = \rho_f (\mathbf{u}_f \cdot \mathbf{S}_f)$ über die Newton-Zerlegung:
     $$\rho_f^{(k)}\mathbf{u}_f^{(k)} \approx \rho_f^{(k-1)}\mathbf{u}_f^{(k)} + \mathbf{u}_f^{(k-1)}\left(\frac{\partial\rho}{\partial p}\right)_{T,f}\!\Delta p_f + \mathbf{u}_f^{(k-1)}\left(\frac{\partial\rho}{\partial T}\right)_{p,f}\!\Delta T_f, \qquad \left(\frac{\partial\rho}{\partial p}\right)_T = \frac{1}{c_T^2}$$
     Im Grenzfall $\text{Ma} \to 0$ ($c_T \to \infty$) verschwindet $\partial\rho/\partial p \to 0$ automatisch und das System geht **nahtlos ohne Low-Mach-Singularität** in das inkompressible $4\times 4$ Rhie-Chow-Sattelpunktsystem über!
  2. **Krylov-Accelerated SIMPLE (`FGMRES-SIMPLE`) & Augmented-Lagrangian (`AL`) Vorkonditionierer (Klaij & Vuik 2013, *J. Comput. Phys.* 242):**
     - *Stufe 1 (`Krylov-SIMPLE`):* Unser bestehender segregierter SIMPLE/SIMPLEC-Code wird ohne Neuprogrammierung der Einzelgleichungen als **Rechts-Vorkonditionierer $\mathcal{P}_{\text{SIMPLE}}^{-1}$ innerhalb einer äußeren FGMRES- oder BiCGStab(2)-Schleife** aufgerufen.
     - *Stufe 2 (`Augmented-Lagrangian`):* Für extreme Grenzschicht-Aspektverhältnisse ($1000:1$) wird der Impulsblock mit $\mathbf{A}_{\mathbf{u}\mathbf{u}}^{\gamma} = \mathbf{A}_{\mathbf{u}\mathbf{u}} + \gamma_{\text{AL}}\,\mathbf{A}_{\mathbf{u}p}\mathbf{W}_p^{-1}\mathbf{A}_{p\mathbf{u}}$ ($\gamma_{\text{AL}} \sim \mathcal{O}(1)$, $\mathbf{W}_p = \operatorname{diag}(V_P/\rho_P)$) angereichert; nach dem Sherman-Morrison-Woodbury-Lemma dominiert dann im Schur-Komplement $\mathbf{S}_\gamma^{-1} = \mathbf{S}^{-1} + \gamma_{\text{AL}}\mathbf{W}_p^{-1}$ der exakt bekannte Massenterm $\gamma_{\text{AL}}\mathbf{W}_p^{-1}$, wodurch die Iterationszahl **unabhängig von Gitterfeinheit und Aspektverhältnis** wird!
  3. **CPR (Constrained Pressure Residual) Zweistufen-Vorkonditionierer für Reaktive & Multiphysik-Systeme (Wallis 1983; Lacroix et al. 2003; Roy, Janna et al. 2019–2024):**
     Sobald neben $(\mathbf{u}, p, T)$ noch $N_s = 10\text{–}50$ chemische Spezies $Y_k$, Turbulenzgrößen $(k,\omega)$ oder Batterie-Zustände gekoppelt werden ($M \times M$-Blöcke mit $M \ge 10$), wäre eine volle Block-Matrix viel zu teuer. **CPR** kombiniert zwei extrem billige Stufen:
     $$\mathbf{M}_{\text{CPR}}^{-1} = \mathbf{M}_{\text{BILU}(0)/\text{BJacobi}}^{-1}\Big(\mathbf{I} - \mathcal{J}\,\mathbf{P}_p\big(\mathbf{A}_{pp}^{\text{CPR}}\big)^{-1}\mathbf{R}_p^T\Big) + \mathbf{P}_p\big(\mathbf{A}_{pp}^{\text{CPR}}\big)^{-1}\mathbf{R}_p^T, \qquad \mathbf{A}_{pp}^{\text{CPR}} = \mathbf{R}_p^T\,\mathcal{J}\,\mathbf{P}_p$$
     - *Stufe 1 (Globale elliptische Druck-Entkopplung):* Der Quasi-IMPES-Restriktionsoperator $\mathbf{R}_p^T$ extrahiert die skalare Druckgleichung $\mathbf{A}_{pp}^{\text{CPR}}$ ($1\times 1$ pro Zelle), die mit **einem einzigen skalaren AMG-V-Zyklus** gelöst wird.
     - *Stufe 2 (Lokale Hyperbolik-/Reaktions-Glättung):* Ein einziger hochparalleler GPU-**Block-Jacobi- oder Block-ILU(0)-Sweep** glättet alle Transport-, Turbulenz- und Arrhenius-Chemie-Gleichungen lokal pro Zelle.
  4. **Matrix-Free Block-Coupled JFNK / FAS + Zell-Lokaler Vanka-Smoother via Enzyme Forward-AD (Vanka 1986; Sabawi, Farrell & Wathen 2020–2025):**
     Ein assembliertes $4\times 4$-`Block-CSR`-System benötigt $16\times$ so viele Off-Diagonal-Floats wie ein Skalar-Laplacian. In unserem Stack berechnet stattdessen **Enzyme Forward-Mode AD (`__enzyme_fwddiff`)** das exakte Matrix-Vektor-Produkt $\mathcal{J}\mathbf{v} = \frac{\partial\mathbf{R}(\mathbf{U})}{\partial\mathbf{U}}\mathbf{v}$ innerhalb von FGMRES **komplett matrixfrei in $\mathcal{O}(N)$ Speicher**, während im VRAM lediglich die zelllokalen $4\times 4$- bzw. $5\times 5$-Diagonalblöcke (oder kleine zellzentrierte Vanka-Patches + skalare Druck-AMG-Matrix $\tilde{\mathbf{S}}$) für den Vorkonditionierer liegen (**50–65 % weniger VRAM** bei voller quadratischer Newton-Konvergenz!).
  5. **Unterrelaxations- und Zeitschritt-Konsistente Rhie-Chow-Interpolation (Moukalled, Mangani & Darwish 2016, Kap. 15.6 & 18; Pascau 2011):**
     In einfachem OpenFOAM-/Ansys-Rhie-Chow hängt die auskonvergierte stationäre Lösung leicht vom Unterrelaxationsfaktor $\alpha_u$ und vom Zeitschritt $\Delta t$ ab! Die **exakt konsistente Rhie-Chow-Flächengeschwindigkeit** für sowohl segregierte als auch Block-Coupled Solver lautet:
     $$\mathbf{u}_f^{(k)} = \overline{\mathbf{u}}_f^{(k)} - \overline{\mathbf{D}}_f\Big(\nabla p_f^{(k)} - \overline{\nabla p}_f^{(k)}\Big) + (1 - \alpha_u)\Big(\mathbf{u}_f^{(k-1)} - \overline{\mathbf{u}}_f^{(k-1)}\Big) + \frac{\rho_f V_f}{\Delta t\,\overline{a}_{P,f}}\Big(\mathbf{u}_f^{n} - \overline{\mathbf{u}}_f^{n}\Big)$$
     Sie stellt mathematisch sicher, dass der stationäre Fixpunkt ($\mathbf{u}_f^{(k)} = \mathbf{u}_f^{(k-1)} = \mathbf{u}_f^n$) **auf Maschinenpräzision unabhängig von $\alpha_u$ und $\Delta t$** ist!
* **Verbindliche Dual-Mode-Solver-Strategie für unseren Stack:**
  | Physik-Regime & Anwendungsfall | Primärer Solver-Modus im C++20/CUDA-Stack | Warum genau dieser Modus? |
  | :--- | :--- | :--- |
  | **Stationäre & Langsam-Transiente RANS, Turbomaschinen (MRF/AMI), CHT, SIMP-TopOpt, All-Mach & Adjoint** | **Matrix-Free / Block-Coupled JFNK** (`FGMRES` / `BiCGStab(2)`) vorkonditioniert mit **Krylov-AL-SIMPLE**, **El-Ouafa Two-Phase PCD (9.19)** oder **CPR (Reaktive Chemie)** | Lineare $\mathcal{O}(N)$-Skalierung auf $1000:1$-Prismengittern, $6\times\text{–}35\times$ schneller als äußeres SIMPLE, $\text{CFL} = 10\text{–}100$, und direkt transponierbar ($\mathcal{J}^T\boldsymbol{\psi} = -\nabla_{\mathbf{U}} L$) für den Adjoint-Solver! |
  | **Hochtransiente Wirbelauflösung ($\text{CFL} \le 1$): WALE-LES, DES, FWH-Aeroakustik, Freie VOF-Oberflächen & OPMIS SPH-FVM** | **Leichtgewichtiger Segregierter PISO / Fractional-Step-Modus** mit Multi-Pass Rhie-Chow Re-Evaluation + **Krylov-Deflation (10.5.4)** & **KEP-Konvektion (10.5.3)** | Da die Physik ohnehin $\Delta t$ auf $\text{CFL} < 1$ begrenzt, ist der Impulsblock $\frac{\rho V}{\Delta t}\mathbf{I} + \mathbf{A}_{\mathbf{u}\mathbf{u}}$ stark diagonaldominant; 2–3 PISO-Korrekturen ohne äußere Nichtlinearitäts-Schleife liefern hier den maximalen Durchsatz pro Sekunde. |

---

## 10. Angewandte Mathematische Beschleuniger & Struktur-Erhaltende Operatoren (Ohne zusätzlichen PDF-Overhead direkt in Code übersetzbar)
*Fokus: Geschlossene mathematische Operatoren aus der Mimetischen Diskretisierung (MFD/DEC), Port-Hamiltonschen Systemtheorie (PHS), Spektralen Graph-Sparsifikation (LAMG), Tropischen Geometrie, Anderson-Beschleunigung, Algebraischen Flusskorrektur (AFC/MPRK) und Krylov-Deflation*

---

### 10.1 Mimetischer Diskreter Hodge-Stern auf verzerrten Polyedern (MFD / VEM: Brezzi, Buffa & Lipnikov 2009; Lipnikov, Manzini & Shashkov 2014)
* **Problem:** Auf extrem schiefen Cut-Cells oder Mosaic-Übergangspolyedern ($\theta_f > 70^\circ$) kann die explizite Verzögerung von $\nabla\phi_f\cdot\mathbf{k}_f^{\text{corr}}$ die Symmetrie und Koerzivität der Diffusionsmatrix schwächen.
* **Geschlossene lokale $m\times m$-Stabilisierungsmatrix $\mathbf{M}_P$ pro Polyederzelle $P$ ($m$ Flächen):**
  Mit der Flächenvektor-Matrix $\mathbf{N}_P = [\mathbf{S}_{P,1}, \dots, \mathbf{S}_{P,m}]^T \in \mathbb{R}^{m\times 3}$ und der Abstands-Matrix $\mathbf{R}_P = [\mathbf{x}_{f_1} - \mathbf{x}_P, \dots, \mathbf{x}_{f_m} - \mathbf{x}_P]^T \in \mathbb{R}^{m\times 3}$ (die nach dem Satz von Gauß exakt $\mathbf{N}_P^T \mathbf{R}_P = V_P \mathbf{I}_3$ erfüllen!) lautet das mimetische Skalarprodukt (diskreter Hodge-Stern $\star_1$):
  $$\mathbf{M}_P(\mathbf{K}_P) = \underbrace{\frac{1}{V_P}\mathbf{N}_P \mathbf{K}_P \mathbf{N}_P^T}_{\text{Konsistenzterm (Rang 3)}} + \underbrace{\gamma_P \left(\mathbf{I}_m - \mathbf{R}_P(\mathbf{R}_P^T \mathbf{R}_P)^{-1}\mathbf{R}_P^T\right)}_{\text{Orthogonaler Projektor-Stabilisierungsterm (Rang } m-3\text{)}}, \qquad \gamma_P = \frac{\operatorname{tr}(\mathbf{K}_P)}{V_P^{1/3}}$$
* **Nutzen:** Benötigt **null Volumen-Quadratur** (nur $\mathbf{S}_f, \mathbf{x}_f, \mathbf{x}_P, V_P$ aus `FaceBaseSoA`), erfüllt exakt die lineare Konsistenzbedingung $\mathbf{M}_P \mathbf{R}_P = \mathbf{N}_P \mathbf{K}_P$ und ist auf **jedem beliebig gekrümmten oder nicht-konvexen Polyeder garantiert symmetrisch positiv definit ($\mathbf{M}_P \succ 0$)**.

---

### 10.2 Port-Hamiltonsche Systeme (PHS) & Dirac-Leistungs-Ports für garantiert stabile Multiphysik-Kopplung (van der Schaft & Jeltsema 2014; Kotyczka 2019)
* **Problem:** Beim modularen Zusammenstecken von Fluid, Festkörper (FSI), Wärmeleitung (CHT), Batterie-Elektrochemie und MHD im C-ABI Data-Lake können zwei einzeln stabile Löser am Kopplungs-Interface künstliche Energie erzeugen und divergieren.
* **Mathematischer Dirac-Kopplungsvertrag:**
  Jedes Interface zwischen zwei Physik-Modulen $A$ und $B$ tauscht ausschließlich **leistungskonjugierte Effort-Flow-Paare $(e_\alpha, f_\alpha)$** aus, deren Produkt die strikte SI-Dimension einer **Leistung $[\text{W}] = [1, 2, -3, 0, 0, 0, 0]$** besitzt:
  - *Mechanik / FSI:* Effort $e_{\text{mech}} = \mathbf{v}_{\text{int}}\;[\text{m/s}]$, Flow $f_{\text{mech}} = \mathbf{F}_f = \boldsymbol{\sigma}\cdot\mathbf{S}_f\;[\text{N}] \implies \mathbf{v}_{\text{int}}\cdot\mathbf{F}_f\;[\text{W}]$.
  - *Thermodynamik / CHT:* Effort $e_{\text{th}} = T\;[\text{K}]$ (bzw. $1 - T_0/T$), Flow $f_{\text{th}} = \dot{S}_f = \dot{Q}_f/T\;[\text{W/K}] \implies e_{\text{th}} f_{\text{th}} = \dot{Q}_f\;[\text{W}]$.
  - *Elektrochemie / MHD:* Effort $e_{\text{el}} = \phi_E\;[\text{V}]$, Flow $f_{\text{el}} = I_f = \mathbf{J}\cdot\mathbf{S}_f\;[\text{A}] \implies \phi_E I_f\;[\text{W}]$.
  Durch Erzwingen der **verlustfreien Dirac-Bedingung** $e_A = e_B$ und $f_A + f_B = 0$ ($\implies \langle e_A, f_A\rangle + \langle e_B, f_B\rangle \equiv 0$) zusammen mit einer schiefadjungierten Kopplungsmatrix $\mathbf{J}_{\text{int}}^T = -\mathbf{J}_{\text{int}}$ und positiv semidefiniter Dämpfung $\mathbf{R}_{\text{diss}} \succeq 0$ gilt für die Gesamt-Hamilton-Funktion (Gesamtenergie/Exergie) $\mathcal{H}_{\text{tot}} = \sum_m \mathcal{H}_m$:
  $$\frac{\mathrm{d}\mathcal{H}_{\text{tot}}}{\mathrm{d}t} = -\mathbf{e}^T \mathbf{R}_{\text{diss}} \mathbf{e} + \mathbf{e}_{\partial\Omega}^T \mathbf{f}_{\partial\Omega} \le \mathbf{e}_{\partial\Omega}^T \mathbf{f}_{\partial\Omega}$$
  $\implies$ **Unbedingte Passivität (Lyapunov-Stabilität) über alle gekoppelten Physikmodule hinweg!**

---

### 10.3 Spektrale Graph-Sparsifikation (Spielman & Teng 2011; Livne & Brandt LAMG 2012) gegen Operator-Complexity-Explosion in Polyeder-AMG
* **Problem:** Auf Poly-Hexcore- oder VoroCrust-Gittern mit 14–25 Flächen pro Zelle füllt sich der Grobgitter-Laplacian $\mathbf{A}_c = \mathbf{P}^T \mathbf{A}\mathbf{P}$ ab AMG-Level 2 stark auf (*Operator Complexity* $\sigma_{\text{op}} = \sum_\ell \text{nnz}(\mathbf{A}_\ell)/\text{nnz}(\mathbf{A}_0) > 3.5$), was die GPU-Speicherbandbreite bremst.
* **Lösung:** Auf jedem groben AMG-Level $\ell \ge 1$ werden schwache Off-Diagonal-Kanten $a_{ij}^{(\ell)}$ mit $|a_{ij}^{(\ell)}| < \theta_{\text{sparsify}} \sqrt{|a_{ii}^{(\ell)} a_{jj}^{(\ell)}|}$ unter **lumpender Erhaltung der Zeilensumme (Konstanten-Kern)** und ggf. effektiver Widerstandsstichprobe $p_{ij} \propto |a_{ij}| (\mathbf{e}_i - \mathbf{e}_j)^T \mathbf{A}_c^\dagger (\mathbf{e}_i - \mathbf{e}_j)$ gestrichen und umskaliert. Das begrenzt $\sigma_{\text{op}} \le 1.35$ bei spektraler Äquivalenz $(1-\epsilon)\mathbf{A}_c \preceq \tilde{\mathbf{A}}_c \preceq (1+\epsilon)\mathbf{A}_c$ (**bis zu $2.5\times$ schnellere AMG-V-Zyklen auf GPUs**).

---

### 10.4 Tropische Geometrie / Log-Sum-Exp Glättung für $C^\infty$-Differenzierbare Wandabstände (Varadhan-Formel) & Wirbelstreckungs-Sensor
1. **Tropische Dequantisierung (Log-Sum-Exp) für glatte SDFs & KS-Aggregation:**
   In der tropischen $(\min, +)$-Algebra ist das Minimum $\min_k d_k$ der Grenzwert der glatten Varadhan-/Kreisselmeier-Steinhauser-Formel:
   $$d_{\text{smooth}}(\mathbf{x}) = -\frac{1}{\rho_{\text{trop}}}\ln \left(\sum_{f \in \Gamma_{\text{wall}}} \frac{A_f}{A_{\text{tot}}}\,\exp\!\big(-\rho_{\text{trop}}\,\|\mathbf{x} - \mathbf{x}_f\|_2\big)\right) \xrightarrow{\rho_{\text{trop}}\to\infty} \min_{f\in\Gamma_{\text{wall}}} \|\mathbf{x} - \mathbf{x}_f\|_2$$
   Liefert für Adjoint-Formoptimierung und Turbulenzmodelle ($y_{\text{wall}}$) einen **überall $C^\infty$-differenzierbaren Wandabstand ohne Medial-Axis-Knicke**!
2. **3D-Wirbelstreckungs- / Blowup-Frühwarn-Indikator (nach Buckmaster/Alpöge 2026 & Enstrophie-Bilanz):**
   Da in 3D Navier-Stokes der Wirbelstreckungsterm $\boldsymbol{\omega}\cdot(\boldsymbol{\omega}\cdot\nabla)\mathbf{u} = \boldsymbol{\omega}^T \mathbf{S}\boldsymbol{\omega}$ die einzige Quelle der Enstrophie-Verstärkung $\frac{1}{2}\frac{\mathrm{D}\|\boldsymbol{\omega}\|^2}{\mathrm{D}t} = \boldsymbol{\omega}^T \mathbf{S}\boldsymbol{\omega} - \nu\|\nabla\boldsymbol{\omega}\|_F^2$ ist, ergänzt der dimensionslose **Vortex-Stretching-Sensor**
   $$\mathcal{I}_{\text{stretch},i} = V_i^{2/3}\,\frac{\max(\boldsymbol{\omega}_i^T \mathbf{S}_i \boldsymbol{\omega}_i, 0)}{\|\mathbf{u}_i\|_2^2\,\|\boldsymbol{\omega}_i\|_2 + \varepsilon}$$
   unseren `SolutionIndicator` in der `MeshErrorMap` und verfeinert aufrollende Schaufelspitzen- und Kavitationswirbel **bevor** Druck- und Geschwindigkeitsgradienten steil werden.

---

### 10.5 Vier weitere Hochleistungs-Angles für unseren Solver: Anderson Acceleration `AA(m)`, Kuzmin AFC / Modified Patankar, Symmetrie-erhaltendes KEP-FVM & Krylov-Deflation
1. **Anderson Acceleration `AA(m)` für segregiertes SIMPLE, CHT & FSI (Walker & Ni 2011; Pollock, Rebholz & Xiao 2019):**
   Beschleunigt jede Fixpunktiteration $\mathbf{x}^{(k+1)} = \mathcal{G}(\mathbf{x}^{(k)})$ (Residuum $\mathbf{f}_k = \mathcal{G}(\mathbf{x}^{(k)}) - \mathbf{x}^{(k)}$) über die letzten $m \approx 5$ Stände durch Lösen eines winzigen **$m\times m$ Least-Squares-Problems** ($\mathcal{O}(m^2 N)$ Aufwand, keine Matrix-Neuassemblierung!):
   $$\boldsymbol{\theta}^* = \arg\min_{\sum_{j=0}^m \theta_j = 1} \left\|\sum_{j=0}^m \theta_j \mathbf{f}_{k-j}\right\|_2^2, \qquad \mathbf{x}^{(k+1)} = (1-\beta_{AA})\sum_{j=0}^m \theta_j^*\,\mathbf{x}^{(k-j)} + \beta_{AA}\sum_{j=0}^m \theta_j^*\,\mathcal{G}(\mathbf{x}^{(k-j)})$$
   $\implies$ **Reduziert die Iterationszahl von segregiertem SIMPLE und partitioniertem CHT um Faktor $2.5\times\text{ bis }4\times$!**
2. **Garantierte Positivität & Diskretes Maximum-Prinzip via Kuzmin AFC & Modified Patankar-Runge-Kutta (MPRK):**
   * *Räumlich (Kuzmin Algebraic Flux Correction, 2002–2024):* Begrenzt den antidiffusiven Korrekturfluss $f_{PN}^{\text{anti}} = F_{PN}^{\text{HO}} - F_{PN}^{\text{upw}}$ symmetrisch über Zalesak-Knotenlimiter $\alpha_{PN} \in [0, 1]$, sodass das diskrete Maximum-Prinzip $\min_N \phi_N^n \le \phi_P^{n+1} \le \max_N \phi_N^n$ strikt gilt.
   * *Zeitlich für steife Produktions-Destruktions-Systeme (Burchard, Deleersnijder & Meister MPRK, 2003–2022):* Für Spezies-Massenanteile $\frac{\mathrm{d}Y_k}{\mathrm{d}t} = \sum_j p_{kj}(\mathbf{Y}) - \sum_j d_{kj}(\mathbf{Y})$ ($p_{kj} = d_{jk} \ge 0$) gewichtet das Patankar-Schema Produktion und Destruktion implizit mit den Quotienten $\frac{Y_j^{n+1}}{Y_j^n}$ bzw. $\frac{Y_k^{n+1}}{Y_k^n}$:
     $$Y_k^{n+1} - Y_k^n = \Delta t \sum_{j=1}^{N_s} p_{kj}(\mathbf{Y}^n)\frac{Y_j^{n+1}}{Y_j^n} - \Delta t \sum_{j=1}^{N_s} d_{kj}(\mathbf{Y}^n)\frac{Y_k^{n+1}}{Y_k^n}$$
     $\implies$ **Beweisbar unbedingt positiv ($Y_k^{n+1} > 0$) und exakt massenerhaltend ($\sum_k Y_k^{n+1} \equiv \sum_k Y_k^n = 1$) für jeden beliebigen Zeitschritt $\Delta t > 0$!**
3. **Kinetische-Energie-erhaltende (KEP) Schiefsymmetrische Konvektion für LES & Akustik (Trias, Lehmkuhl & Verstappen 2014–2025):**
   Mit schiefsymmetrischem Konvektionsoperator $\mathbf{C}(\dot{\mathbf{m}}) + \mathbf{C}(\dot{\mathbf{m}})^T = \mathbf{0}$ (Mittelpunktsgewicht $\beta_f = 1/2$) bleibt die diskrete kinetische Energie $\sum_P V_P \rho_P \frac{\|\mathbf{u}_P\|^2}{2}$ im reibungsfreien Grenzfall exakt erhalten; die Rhie-Chow-Druckdissipation wird nur lokal über einen Druck-Oszillationssensor $\psi_{p,f} \in [0, 1]$ zugeschaltet, sodass feine LES-Wirbel und FWH-Schallwellen nicht weggedämpft werden.
4. **Krylov-Subraum-Deflation & Recycling (`GCRO-DR` / Nicolaides-Deflation: Parks et al. 2006; Vuik & Sheikh 2009):**
   Recycelt in transienten PISO/PIMPLE-Läufen und Topologieoptimierungs-Schleifen die $k \approx 8\text{–}16$ langsamsten harmonischen Ritz-Vektoren $\mathbf{Z} \in \mathbb{R}^{N\times k}$ des Druck-Laplacians vom vorherigen Schritt über den Deflationsprojektor:
   $$\mathbf{P}_{\text{def}} = \mathbf{I} - \mathbf{A}\mathbf{Z}\mathbf{E}^{-1}\mathbf{Z}^T, \qquad \mathbf{E} = \mathbf{Z}^T \mathbf{A}\mathbf{Z} \in \mathbb{R}^{k\times k}$$
   $\implies$ **Senkt die Anzahl der AMG-V-Zyklen pro PISO-Zeitschritt von 12–15 auf 2–4 ($3\times\text{–}4\times$ Speedup)!**



