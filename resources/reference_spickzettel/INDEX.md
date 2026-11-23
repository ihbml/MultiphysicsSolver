# Master-Bookmarks & Spickzettel-Index (`reference_spickzettel`)
> **Zweck:** Zentrales Lesezeichen-Verzeichnis (Bookmarks) für dich und alle KI-Instanzen (*Antigravity / Gemini, Carbon, Claude Opus 5.5*).  
> **Grundgesetz:** Wir bauen **keinen** abhängigen Bibliotheks-Flickenteppich, sondern schreiben **100 % eigenständigen C++ / Modern Fortran / Rust Code** und nutzen diese **23 geklonten Referenz-Repos + `OpenFOAM_base` + 7 Fachpaper** als direkten Spickzettel für die besten Algorithmen.

---

## Schnell-Navigation (Kategorien)
1. [Kategorie 1: CAD, B-Rep, Implizite Geometrie & Wasserdichte Boolesche Schnitte (`CAD/`)](#1-kategorie-cad--b-rep-implizite-geometrie--boolesche-schnitte)
2. [Kategorie 2: 3D-Meshing, Cut-Cell, Octree & Level-Set-Vernetzungen (`Meshing/`)](#2-kategorie-meshing--3d-vernetzung-cut-cell-octree--level-set)
3. [Kategorie 3: CFD, Graph-FVM, Adjoint & SPH-FVM Kopplung (`CFD_Multiphysics/` & `OpenFOAM_base`)](#3-kategorie-cfd_multiphysics--graph-fvm-adjoint--sph-fvm-kopplung)
4. [Kategorie 4: Gleichungslöser, Algebraic Multigrid (AMG) & Mixed-Precision HPC (`LinearSolvers_HPC/`)](#4-kategorie-linearsolvers_hpc--amg-drucklöser--mixed-precision-fp16fp64)
5. [Kategorie 5: Topologieoptimierung (SIMP, MMA, FOSM), Schwarm (PSO/MIGA) & Auto-Diff (`Optimization_AD/`)](#5-kategorie-optimization_ad--simp-mma-fosm-psomiga--auto-diff)
6. [Kategorie 6: Reaktionskinetik, Realgas-Thermodynamik & Steife ODE-Löser (`Thermo_Chemistry_Physics/`)](#6-kategorie-thermo_chemistry_physics--kinetik-realgas--steife-odes)
7. [Kategorie 7: Wissenschaftliche Paper-Bookmarks (`resources/papers/`)](#7-kategorie-papers--die-schlüsselpublikationen--dissertationen-im-direktzugriff)
8. [Kategorie 7b: Die besten LMArena Mesher- & Solver-Spickzettel (`LMArena/Mesher/` & `LMArena/Solver/`)](#7b-kategorie-lmarena--die-besten-mesher---solver-spickzettel-lmarenamesher--lmarenasolver)
9. [Kategorie 7c: Die besten LMArena Frontend-, Post-Processing- & CAD-Format-Spickzettel (`LMArena/Frontend/` & `LMArena/CAD/`)](#7c-kategorie-lmarenafrontend--cad--native-wgpuegui-cockpits-wgsl-shader-cfdfld03-in-situ-post-processing--axcad)
10. [Gold-Snippets & Direkt-Lesezeichen (Snippets 1–20)](#8-gold-snippets--direkt-lesezeichen-wichtigste-codeblöcke-mit-zeilennummern)

---

## 1. Kategorie `CAD` — B-Rep, Implizite Geometrie & Boolesche Schnitte
Ordner: [`resources/reference_spickzettel/CAD/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`truck`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck)** *(Reines Rust CAD)* | • [`truck-stepio/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-stepio) *(STEP-Parser & Exporter in Rust)*<br/>• [`truck-shapeops/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-shapeops) *(NURBS Boolesche Schnitte)*<br/>• [`truck-meshalgo/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-meshalgo) *(Oberflächen-Triangulierung)*<br/>• [`truck-topology/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/truck/truck-topology) *(Vertices, Edges, Faces, Shells)* | Moderne, speichersichere Rust-Vorlage für unseren STEP-Reader und B-Rep-Topologiekern (100x kompakter als OpenCASCADE). |
| **[`manifold`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold)** *(Garantiert wasserdichtes C++)* | • [`boolean3.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/boolean3.cpp) *(3D Boolesche Verschneidung)*<br/>• [`csg_tree.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/csg_tree.cpp) *(CSG Baum-Auswertung)*<br/>• [`sdf.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/sdf.cpp) *(Level-Set / Marching Tetrahedra)*<br/>• [`smoothing.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/manifold/src/smoothing.cpp) *(Netz-Glättung)* | Garantiert **2-Manifold (100 % wasserdichte)** Boolesche Operationen, um aus einem CAD-Bauteil das negative Fluid-Strömungsvolumen ohne Mikrolöcher auszuschneiden. |
| **[`libigl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl)** *(ETH Zürich / NYU Header-Only C++)* | • [`signed_distance.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/signed_distance.cpp) *(Exakter Wandabstand SDF)*<br/>• [`fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp) *(Innen/Außen-Test für $\gamma_i$)*<br/>• [`marching_cubes.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/marching_cubes.cpp) *(SIMP $\gamma_i \to$ glattes CAD-Netz)* | **Fast Winding Numbers** entscheiden für Millionen CFD-Zellen in Millisekunden, ob eine Zelle im Festkörper ($\gamma_i=0$) oder Fluid ($\gamma_i=1$) liegt; `marching_cubes` gewinnt aus dem optimierten SIMP-Feld das fertige Bauteil zurück. |
| **[`openvdb`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb)** *(OpenVDB & NanoVDB)* | • [`nanovdb/NanoVDB.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb/nanovdb/nanovdb/NanoVDB.h) *(Single-Header Octree/Level-Set)*<br/>• [`openvdb/tools/LevelSetAdvect.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/openvdb/openvdb/openvdb/tools/LevelSetAdvect.h) | Datenstruktur-Blaupause für flache, GPU-/SIMD-freundliche hierarchische Octree-Gitter und Level-Set-Transport ($\psi$). |
| **[`solvespace`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace)** *(Parametrischer 3D-Kernel)* | • [`src/constrainteq.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace/src/constrainteq.cpp) *(Geometrischer Gleichungslöser)*<br/>• [`src/srf/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/solvespace/src/srf) *(Kompakter NURBS-Flächenkern)* | Zeigt, wie ein kompletter parametrischer CAD-Constraint-Solver (Abstände, Radien, Winkel für Formoptimierung) in wenigen tausend Zeilen C++ funktioniert. |
| **[`OCCT`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT)** *(OpenCASCADE)* | • [`src/STEPControl/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT/src/STEPControl) *(Vollständiger ISO-STEP-Standard)*<br/>• [`src/BRepMesh/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/OCCT/src/BRepMesh) *(B-Rep Tessellierung)* | Nachschlagewerk für seltene ISO-10303-STEP-Spezialfälle (AP203/214/242). |

---

## 2. Kategorie `Meshing` — 3D-Vernetzung, Cut-Cell, Octree & Level-Set
Ordner: [`resources/reference_spickzettel/Meshing/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`fTetWild`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild)** *(Kugelsicherer 3D-Mesher)* | • [`CutMesh.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/CutMesh.cpp) *(Zellen entlang CAD-Dreiecken schneiden)*<br/>• [`FloatTetDelaunay.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/FloatTetDelaunay.cpp)<br/>• [`MeshImprovement.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/fTetWild/src/MeshImprovement.cpp) *(Qualitäts-Optimierung)* | `CutMesh.cpp` und `MeshImprovement.cpp` sind die perfekte Vorlage für unseren eigenen Cut-Cell-/Tetraeder-Vernetzungsalgorithmus, der selbst bei kaputten STL-Dateien nie abstürzt. |
| **[`mmg`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg)** *(Level-Set & Anisotropes Remeshing)* | • [`src/mmg3d/mmg3d1.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg/src/mmg3d/mmg3d1.c)<br/>• [`src/mmg3d/mmg3d2.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/mmg/src/mmg3d/mmg3d2.c) *(Implizites Level-Set $\psi = 0$ Meshing!)* | `mmg3d2.c` zeigt, wie man eine **Level-Set-Nullkontur ($\psi = 0$)** aus unserer Topologieoptimierung direkt als messerscharfe Kante in ein bestehendes 3D-Gitter schneidet! |
| **[`gmsh`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh)** *(3D-FVM & Grenzschichten)* | • [`src/mesh/meshGRegion.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh/src/mesh/meshGRegion.cpp) *(3D-Volumenvernetzung)*<br/>• [`src/mesh/meshGRegionBoundaryLayer.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/gmsh/src/mesh/meshGRegionBoundaryLayer.cpp) | Referenz für das Aufwachsen von **Grenzschicht-Prismen (Boundary Layer Extrusion für $y^+ < 1$)** an gekrümmten Wänden. |
| **[`tetgen`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen)** *(Kompakter Delaunay-Kern)* | • [`tetgen.cxx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen/tetgen.cxx) & [`predicates.cxx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Meshing/tetgen/predicates.cxx) | Exakte geometrische Prädikate (`orient3d`, `insphere` von Shewchuk) gegen Rundungsfehler bei spitzen Winkeln. |

---

## 3. Kategorie `CFD_Multiphysics` — Graph-FVM, Adjoint & SPH-FVM Kopplung
Ordner: [`resources/reference_spickzettel/CFD_Multiphysics/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics) & [`OpenFOAM_base/`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`SU2`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2)** *(Stanford Unstructured FVM + Adjoint)* | • [`SU2_CFD/src/numerics/flow/convection/hllc.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/hllc.cpp)<br/>• [`SU2_CFD/src/numerics/flow/convection/roe.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/roe.cpp)<br/>• [`SU2_CFD/src/solvers/CFVMFlowSolverBase.inl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/include/solvers/CFVMFlowSolverBase.inl)<br/>• [`SU2_CFD/src/drivers/CDiscAdjSinglezoneDriver.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/drivers/CDiscAdjSinglezoneDriver.cpp) | Sauberer, makroloser C++-Spickzettel für unstrukturierte FVM-Flüsse (HLLC, Roe, AUSM), Grenzschicht-Turbulenz ($k-\omega$ SST, SA) und diskrete Adjoint-Schleifen. |
| **[`SPHinXsys`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys)** *(TU München Eulerian SPH + FVM)* | • [`eulerian_riemann_solver.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_riemann_solver.cpp) *(HLLC mit Limiter $\beta_{HLLC}$)*<br/>• [`eulerian_ghost_boundary.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/general_dynamics/boundary_condition/eulerian_ghost_boundary.cpp) *(Nullte-Ordnung-Randkonsistenz!)*<br/>• [`eulerian_compressible_fluid_integration.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_compressible_fluid_integration.hpp) | **Der exakte Original-Code zu deinem Paper [`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf)!** Enthält die direkte Kopplung von FVM-Randbedingungen und Eulerian-SPH. |
| **[`ECOGEN`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN)** *(Mehrphasen, Kavitation & Stöße)* | • [`src/Models/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Models) *(Kapila Mehrphasen- & Kavitationsmodelle)*<br/>• [`src/Eos/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Eos) *(Stiffened Gas EOS für Wasser/Gas-Blasen)*<br/>• [`src/Relaxations/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/ECOGEN/src/Relaxations) *(Druck-/Temperatur-Relaxation & Phasenwechsel)* | Perfekte Vorlage für die kompressiblen Mehrphasen-Gleichungen (Stiffened Gas EOS Gl. 17 & 33 aus deinem Blasen-Paper [`MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf)). |
| **[`OpenFOAM_base/src/fused`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume)** *(Fused FVM Kernels)* | • [`fusedGaussGrad.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussGrad.C#L86-L109)<br/>• [`fusedGaussConvectionScheme.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussConvectionScheme.C)<br/>• [`fusedGaussLaplacianScheme.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussLaplacianScheme.C)<br/>• [`CuthillMcKeeRenumber.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/renumber/renumberMethods/CuthillMcKeeRenumber/CuthillMcKeeRenumber.C) | Single-Pass Gather/Scatter FVM-Schleifen und Cache-Renumbering für unser `DynamicRedistributor`-Modul. |

---

## 4. Kategorie `LinearSolvers_HPC` — AMG Drucklöser & Mixed-Precision (`FP16`/`FP64`)
Ordner: [`resources/reference_spickzettel/LinearSolvers_HPC/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`amgcl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl)** *(Header-Only C++ AMG)* | • [`amgcl/amg.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/amg.hpp) *(Kompletter AMG V-Cycle Aufbau)*<br/>• [`amgcl/coarsening/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/coarsening) *(Smoothed Aggregation & Ruge-Stüben)*<br/>• [`amgcl/relaxation/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/relaxation) *(SPAI0, Gauss-Seidel, ILU0, Chebyshev Glätter)*<br/>• [`amgcl/solver/bicgstab.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/solver/bicgstab.hpp) & [`cg.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/amgcl/amgcl/solver/cg.hpp) | Der mit Abstand klarste C++-Spickzettel weltweit für unseren eigenen **Algebraic Multigrid (AMG)** Druck-Poisson-Löser und unsere Fortran/C++ Krylov-Löser (CG, BiCGStab, GMRES). |
| **[`ginkgo`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo)** *(KIT Mixed-Precision HPC)* | • [`core/solver/ir.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/ir.cpp) *(Mixed-Precision Iterative Refinement!)*<br/>• [`core/solver/cb_gmres.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/cb_gmres.cpp) *(Compressed-Basis GMRES in FP16/FP32)* | Exakte Vorlage für unser **`PrecisionAddon::LowPrecisionBooster`** (äußeres Residuum in `FP64`, innerer Vorkonditionierer in `FP16`/`FP32`). |
| **[`eigen`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/eigen)** *(SIMD Matrix/Vektor-Kern)* | • [`Eigen/src/Core/arch/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/eigen/Eigen/src/Core/arch) *(AVX2, AVX-512, ARM NEON, SVE Paket-Mathematik)* | Spickzettel für plattformübergreifende SIMD-Vektorisierung (x86 AVX2/AVX-512 & ARM64 NEON/SVE) und kleine $3\times 3$ / $6\times 6$ Block-Invertierungen. |

---

## 5. Kategorie `Optimization_AD` — SIMP, MMA, FOSM, PSO/MIGA & Auto-Diff
Ordner: [`resources/reference_spickzettel/Optimization_AD/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD) & [`OpenFOAM_base/src/optimisation`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`TopOpt_in_PETSc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc)** *(DTU TopOpt Gruppe: Sigmund / Aage / Lazarov)* | • [`MMA.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc) & [`MMA.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.h) *(700 Zeilen reines C++ MMA!)*<br/>• [`PDEFilter.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/PDEFilter.cc) *(Helmholtz-Dichtefilter)*<br/>• [`Filter.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/Filter.cc) *(Nachbarschafts- & Heaviside-Filter)*<br/>• [`TopOpt.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/TopOpt.cc) | **Juwel des Ordners:** [`MMA.cc`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc) ist die offizielle, extrem saubere C++-Implementierung von Svanbergs *Method of Moving Asymptotes* für Millionen 3D-Zellen! |
| **[`nlopt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt)** *(MIT Optimierungs-Algorithmen)* | • [`src/algs/mma/mma.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/mma/mma.c) & [`ccsa_quadratic.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/mma/ccsa_quadratic.c)<br/>• [`src/algs/neldermead/nldrmd.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/neldermead/nldrmd.c) *(Downhill Simplex!)*<br/>• [`src/algs/slsqp/slsqp.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/nlopt/src/algs/slsqp/slsqp.c) | Kompakte C-Implementierung des **Downhill Simplex (Nelder-Mead)** aus deinem Turbinen-Paper ([`optimization-curved-fir-tree-turbine-15.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/optimization-curved-fir-tree-turbine-15.pdf)) sowie CCSA/MMA. |
| **[`pagmo2`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2)** *(ESA Schwarm- & Insel-Optimierung)* | • [`src/algorithms/pso.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/pso.cpp) & [`pso_gen.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/pso_gen.cpp) *(Particle Swarm)*<br/>• [`src/archipelago.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/archipelago.cpp) & [`island.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/island.cpp) *(Multi-Island Migration MIGA!)*<br/>• [`src/algorithms/nsga2.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/nsga2.cpp) & [`cmaes.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/pagmo2/src/algorithms/cmaes.cpp) | Referenz für **Particle Swarm Optimization (PSO)** und das **Insel-Migrations-Modell (Multi-Island Genetic Algorithm MIGA)** mit parallelen Sub-Populationen im RAM. |
| **[`Enzyme`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme)** *(MIT LLVM Auto-Diff)* | • [`enzyme/Enzyme/Enzyme.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme/enzyme/Enzyme/Enzyme.cpp)<br/>• [`enzyme/test/Integration/ReverseMode/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/Enzyme/enzyme/test/Integration/ReverseMode) | Zeigt die exakten Funktions-Signaturen (`__enzyme_autodiff`), um unsere C++- und Fortran-Kerne vollautomatisch rückwärts zu differenzieren. |
| **[`OpenFOAM_base/adjointOptimisation`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation)** | • [`BorrvallPeterssonInterpolation.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/BorrvallPetersson/BorrvallPeterssonInterpolation.C)<br/>• [`invBP.C` (RAMP)](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/invBP/invBP.C)<br/>• [`Helmholtz.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/regularisation/regularisationPDE/Helmoltz/Helmholtz.C)<br/>• [`LBFGS.C`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/updateMethod/LBFGS/LBFGS.C) | Brinkman-Darcy-Interpolation für Strömungs-SIMP, RAMP und L-BFGS Zwei-Schleifen-Rekursion. |

---

## 6. Kategorie `Thermo_Chemistry_Physics` — Kinetik, Realgas & Steife ODEs
Ordner: [`resources/reference_spickzettel/Thermo_Chemistry_Physics/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics)

| Repository | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :--- | :--- |
| **[`cantera`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera)** *(Reaktionskinetik & Katalyse)* | • [`src/kinetics/Arrhenius.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/Arrhenius.cpp) & [`Falloff.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/Falloff.cpp) *(Troe/SRI Druckabhängigkeit)*<br/>• [`src/kinetics/InterfaceKinetics.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/kinetics/InterfaceKinetics.cpp) *(Heterogene Wandkatalyse / Washcoat)*<br/>• [`src/thermo/NasaPoly2.h`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/include/cantera/thermo/NasaPoly2.h) *(7-Koeffizienten NASA-Polynome)*<br/>• [`src/transport/MixTransport.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/cantera/src/transport/MixTransport.cpp) *(Maxwell-Stefan / Hirschfelder-Curtiss)* | Blaupause für unser `species_chem`- und `energy_heat`-Modul (in Modern Fortran hochvektorisiert nachgebaut!). |
| **[`CoolProp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp)** *(Realgas- & Flüssigkeits-EOS)* | • [`src/Backends/Helmholtz/HelmholtzEOSMixtureBackend.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp/src/Backends/Helmholtz/HelmholtzEOSMixtureBackend.cpp)<br/>• [`src/Backends/Cubics/GeneralizedCubic.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/CoolProp/src/Backends/Cubics/GeneralizedCubic.cpp) *(Peng-Robinson / SRK)* | Exakte Realgas-Zustandsgleichungen (Peng-Robinson, Soave-Redlich-Kwong, Helmholtz-Energie) und Dampfdruckkurven für Kavitation & Phasenwechsel. |
| **[`sundials`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials)** *(LLNL Steife ODE-Löser)* | • [`src/cvode/cvode.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials/src/cvode/cvode.c) *(BDF Backward Differentiation Formulas für steife ODEs)*<br/>• [`src/cvode/cvode_nls.c`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Thermo_Chemistry_Physics/sundials/src/cvode/cvode_nls.c) *(Nichtlineare Newton-Iteration mit Jacobi-Matrix)* | Vorlage für unseren zelllokalen Fortran-Chemie- und Batterie-Thermal-Runaway-Integrator mit variabler Ordnung und Schrittweitensteuerung. |

---

## 7. Kategorie `papers` — Die Schlüsselpublikationen & Dissertationen im Direktzugriff
Ordner: [`resources/papers/`](file:///G:/Datein/Projects/CFDSolver/resources/papers) (Vollständige mathematische Auswertung in [`literature_survey_2026.md`](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md))

1. **Graph-FVM & Impliziter Druck-Adjoint:** [`Meshing_FVM_DiFVM_2603.15920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_FVM_DiFVM_2603.15920v1.pdf) *(Du et al., 2026)*
2. **Eulerian SPH-FVM Randkonsistenz & HLLC:** [`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf) *(Wang, Haidn, Hu – TU München)*
3. **Adaptive SPH-FVM Kopplung (`OPMIS`) für Mehrphasen-Blasen:** [`MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingPaperBubble1-s2.0-S030193222500134X-main.pdf) *(Zhang et al., 2025)*
4. **Thermo-Fluid SIMP, Brinkman & AD:** [`thesis_Elisabetta_Chimisso.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/thesis_Elisabetta_Chimisso.pdf) *(Chimisso, Politecnico di Milano)*
5. **Generalisierte FOSM Robuste Optimierung & KS-Aggregation:** [`ArbitraryFunctionOptimization_s00158-023-03540-w.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ArbitraryFunctionOptimization_s00158-023-03540-w.pdf) *(Kranz, Lüdeker, Kriegesmann – TU Hamburg, 2023)*
6. **Level-Set Topologieoptimierung via Jacobi-Transformation:** [`Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Optimization_LevelSetMethod_AIAA-2010-2842-327.pdf) *(James & Martins, 2010)*
7. **Zweistufige Globale (MIGA) + Lokale (Downhill Simplex) Optimierung:** [`optimization-curved-fir-tree-turbine-15.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/optimization-curved-fir-tree-turbine-15.pdf) *(Tan et al.)*
8. **Combinatorial Meshing, ASP-optimierte Super Elements ($\min SJ \ge 0.302$), Rohteil-Precursor-Mesh & Top-Down Entity Mapping:** [`SuperElements_Db_115554.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/SuperElements_Db_115554.pdf) *(H. Stromberg, Diss. D 104, TU Clausthal, 2023 — **Inzidenztabellen aller 23 Hex-, 5 Tet-, 13 Prism- & 12 Pyr-Super-Elemente in Anhang B & C, S. 121–159!**)*
9. **Next-Gen 3D-Meshing-Bibliothek (Abschnitt 9 in [`literature_survey_2026.md`](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L175)):**
   * [`VoroCrust_1902.08767v3.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroCrust_1902.08767v3.pdf) *(Ungeclipptes Voronoi-Meshing mit $\mathbf{k}_f \equiv \mathbf{0}$)*
   * [`FastTetrahedralMeshing_1908.03581v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FastTetrahedralMeshing_1908.03581v2.pdf) *(fTetWild $\epsilon$-Envelope & Conformal AMIPS)*
   * [`CutCell_Meshing2308.16332v2.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CutCell_Meshing2308.16332v2.pdf) *(Berger & Giuliani 2024: Provably Stable WSRD Cut-Cells)*
   * [`WallShearStress_pointCloud_Meshing_2609.24069v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/WallShearStress_pointCloud_Meshing_2609.24069v1.pdf) *(Jaiswal & Hsu 2026: Unsymmetrische Nitsche-WSS + 3-Ring-Rekonstruktion)*
   * [`Meshing_ErrorCorrection_contmesh.part1.final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Meshing_ErrorCorrection_contmesh.part1.final.pdf), [`Error_Estimators1-s2.0-S0045793021003601-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Error_Estimators1-s2.0-S0045793021003601-main.pdf) & [`ErrorCorrection_Meshing1712.00693v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ErrorCorrection_Meshing1712.00693v1.pdf) *(Continuous Mesh Framework, Fidkowski Entropie-Adjoint & DWR)*
   * [`Boundarylayer_Collision_1901.00992v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Boundarylayer_Collision_1901.00992v1.pdf) *(Marcon et al. 2019 & Loseille/Löhner 2013: Kollisionsfreie Grenzschicht-Extrusion)*
   * [`Moving_Mesh_2006.14051v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2006.14051v1.pdf), [`MovingMesh_Morphing_2610.08880v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MovingMesh_Morphing_2610.08880v1.pdf), [`Moving_Mesh_2407.00382v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Moving_Mesh_2407.00382v4.pdf) & [`VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoroMesh_WatertightSurfaceMesh_2308.14616v1.pdf) *(TINE, JamTet, UM2N & VoroMesh)*
10. **Erweiterungs-Paket (Stand 2026-11-11: GPU-Voronoi, Diff-Voronoi, B-Rep-Invarianten, Diff-CAD, Monolithic FSI & Solid-FVM):**
   * [`Continuous_Mesh_Framework_Part_II_Validations_and_.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Continuous_Mesh_Framework_Part_II_Validations_and_.pdf) *(Loseille & Alauzet 2011, SINUM: Optimale $L^p$-Metrik $\mathcal{M}_{L^p}$ Gl. 2.10, $P_2$-Kantenrekonstruktion & Multiskalen-Adaption)*
   * [`VoronoiDiagrams_2404.18629v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoronoiDiagrams_2404.18629v1.pdf) *(Numerow et al., SIGGRAPH 2024: Analytische 1. & 2. Ableitungen $\frac{\partial \mathbf{x}_c}{\partial \mathbf{c}}, \frac{\partial^2 \mathbf{x}_c}{\partial \mathbf{c}^2}$ von 3D Restricted-Power-Diagrammen über Topologie-Flips)*
   * [`MeshlessVoronoiOnGPU_3272127.3275092.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshlessVoronoiOnGPU_3272127.3275092.pdf) *(Ray, Sokolov, Lefebvre, Lévy, SIGGRAPH Asia 2018: Duales 3D-Voronoi-Clipping `int3 T[96]` + `float4 P[64]` im GPU Shared Memory, 10 Mio. Zellen in 800 ms)*
   * [`DiscreteGCL_1709.04788v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/DiscreteGCL_1709.04788v4.pdf) *(Skopenkov 2023: Exakte diskrete Erhaltungssätze $\partial T = 0$, $\delta\delta = 0$ & RK-/BDF2-DGCL Swept-Volume-Konsistenz $\dot{V}_i = \sum_f \dot{V}_f$)*
   * [`Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf) *(Demirdžić & Cardiff 2022: Householder-Symmetrie-Randbedingungen $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ für Vektoren & Tensoren 2. Stufe in CC-FVM mit exakt verschwindender Cross-Diffusion)*
   * [`FSI_Monolithic_2505.22920v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/FSI_Monolithic_2505.22920v1.pdf) *(Prajapati, Fakhreddine, Mahesh 2025: Monolithische Euler'sche 3D-FSI mit geometrischem VOF/PLIC, Oldroyd-Transport von $\mathbf{B} = \mathbf{F}\mathbf{F}^T$, WENO-Z & Fluid-Reset $\mathbf{B}=\mathbf{I}$)*
   * [`CADWatertightFineMeshing_2604.02141v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CADWatertightFineMeshing_2604.02141v1.pdf) *(Zhou, Zint, Panozzo, Schneider, April 2026: Topology-First B-Rep Meshing — verwirft kaputte 2D-$(u,v)$-Trimmkurven, bettet 3D-Loops per Dijkstra ein und vernäht Nachbarflächen über 1D-$t$-Parameter-Vereinigung 100 % wasserdicht)*
   * [`Differtialble3DCAD_2110.01182v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Differtialble3DCAD_2110.01182v1.pdf) *(Cascaval, Schulz et al., SIGGRAPH Asia 2021: Kompilierter differenzierbarer CAD-DAG $\nabla\mathcal{F}(P) = \frac{\partial \mathbf{x}_{\text{surf}}}{\partial \boldsymbol{\theta}_{\text{CAD}}}$ mit automatischen Gültigkeitsschranken $g_m(P) \ge 0$)*
11. **Coupled-Solver- & STEP-NURBS-Adjoint-Paket (Stand 2026-11-21 — [`literature_survey_2026.md` §9.18–§9.21](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L536)):**
   * [`coupledMFRSteadyIncFoam-Final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/coupledMFRSteadyIncFoam-Final.pdf) *(Mangani, Buchmayr & Darwish 2014: Voll-impliziter $4\times 4$ Block-Coupled $(\mathbf{u}, p)$ FVM-Löser mit zentrifugalkraftfreier MRF-Formulierung, schiefsymmetrischem Coriolis-Block, Moving-Wall Tangentialprojektion $\mathbf{I}-\mathbf{n}_b\otimes\mathbf{n}_b$, $4\times 4$-Rodrigues-Rotation $\mathbf{T}_{NB}$ und direkter Sub-Face-AMI-Kopplung $\implies 6.1\times\text{–}35.1\times$ schneller als SIMPLE)*
   * [`Coupled_Solver_1-s2.0-S0045793023002207-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Coupled_Solver_1-s2.0-S0045793023002207-main.pdf) *(El Ouafa, Vincent, Le Chenadec & Trouette 2023, Computers & Fluids 265: Massiv-paralleler monolithischer Zweiphasen-Löser bis 1,1 Mrd. Zellen mit Ordnung-0 Block-Gauss-Seidel, variabel-dichtem/viskosem Two-Phase PCD Schur-Vorkonditionierer $\hat{S}_{PCD}^{-1}$ und $\text{BiCGStab}(2)$ für $\rho_2/\rho_1 = \mu_2/\mu_1 = 10^6$)*
   * [`CoupledSolver_RWTH_1010342.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CoupledSolver_RWTH_1010342.pdf) *(Orland, Nista, Kocher, Vanvinckenroye, Pitsch & Terboven, RWTH Aachen, HPC Asia 2025: `AIxeleratorService` für Zero-Copy C++ SPMD-Kopplung reaktiver CFD-Solver mit GPU-C-API-Inferenz `LibTorch`/`TF_SessionRun`/`ONNX` $\implies 3.5\times\text{–}9.6\times$ schneller als MPMD-Python)*
   * [`CAD_Optimization_1-s2.0-S0045793011000508-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CAD_Optimization_1-s2.0-S0045793011000508-main.pdf) *(Yu, Müller, Jones & Christakopoulos 2011, Computers & Fluids 46: Direkte STEP B-Rep NURBS-Kontrollpunkt-Formoptimierung mit hybrider Reverse-FVM/Forward-NURBS-AD-Kettenregel $\frac{\partial \mathbf{X}_s}{\partial \mathbf{P}_{i,j}} = R_{i,j}(u,v)\mathbf{I}_3$ in 0,24 s)*

---

## 7b. Kategorie `LMArena` — Die besten Mesher- & Solver-Spickzettel (`LMArena/Mesher/` & `LMArena/Solver/`)
Ordner: [`resources/reference_spickzettel/LMArena/Mesher/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher) & [`resources/reference_spickzettel/LMArena/Solver/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver)

| Spickzettel / Modell | Bewertung | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :---: | :--- | :--- |
| **[`MesherAndSolverSol61Max`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max)** *(GPT-6 Sol / Sol 6.1 Max: „continuum“)* | ⭐⭐⭐⭐⭐ **(5.0/5)** | • [`face_geometry.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/face_geometry.hpp) *(Linear-exakter Ebenen-Schnitt $\beta_f$ + `FaceClass::invalid`)*<br/>• [`face_layout.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/face_layout.hpp) *(6-Range `FaceRanges`, `CorrectionSoA`, `CellFaceCSR`)*<br/>• [`base_coefficients.cu`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/base_coefficients.cu), [`correction_flux.cu`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/correction_flux.cu) & [`gather_residual.cu`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/gather_residual.cu)<br/>• [`halo_schedule.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/halo_schedule.cpp) *(2-Stufen MPI/CUDA Halo-Schedule `halo_phi` + `halo_grad`)*<br/>• [`reference_test.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/reference_test.cpp) & [`notebook.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/data/notebook.ts) *(10-Punkte HPC-Vertrag)* | **Goldstandard für unseren GPU-FVM-Kern:** Kompilierte und getestete C++20/CUDA-Referenz, die 3 kritische Bugs in naiven 2-Klassen-Solvern behebt (`base-all` auf allen 4 Segmenten, 2-Stufen-Halo für Nachbar-Gradienten $(\nabla\phi)_N$ und atomikfreies `CellFaceCSR`-Gather ohne Ghost-Row-Korruption). |
| **[`SolverGemini4ArgonHighCUDA.md`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md)** *(Gemini 4 Argon High)* | ⭐⭐⭐⭐⭐ **(5.0/5)** | • [`SolverGemini4ArgonHighCUDA.md#L17-L200`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L17-L200) *(4-Quadranten CUDA-Kerne + 6%-`MeshCorrectionSoA`)*<br/>• [`SolverGemini4ArgonHighCUDA.md#L204-L244`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L204-L244) *(6 kritische Numerik- & GPU/Enzyme-AD-Optimierungen)* | **Direkte CUDA-Vorlage für unseren 4-Quadranten-Dispatcher:** Zeigt den kompakten 6%-Offset-Trick (`seg3_corr_offset`), gradientenfreie Upwind-Deferred-Correction in Klasse 0, Continuity-Diagonalkorrektur, Vermeidung der 24-Byte-`double3`-Falle sowie Schutz gegen Warp-Atomic-Stau in LLVM Enzyme Reverse-Mode AD. |
| **[`Fable52HighChemSolver`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver)** *(Fable 5.2 High)* | ⭐⭐⭐⭐½ **(4.5/5)** | • [`src/data/solvers.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/data/solvers.ts) *(13 Solver, Kernels & Coupler von $10^{-10}\,\text{m}$ bis $10^{3}\,\text{m}$)*<br/>• [`src/data/stacks.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/data/stacks.ts) *(3 Multiskalen-Kopplungs-Stacks)*<br/>• [`src/components/Speed.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/components/Speed.tsx) *(6 Performance-Hebel für reaktive Strömungen)*<br/>• [`src/components/Language.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/components/Language.tsx) *(C++ Solver + Rust `cxx`/`diffsol`/`faer` Bridge)*<br/>• [`src/lib/scale.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Fable52HighChemSolver/src/lib/scale.ts) *(13-Dekaden Log-Skala & `gradientWindow`)* | **Architektur- & UI-Blaupause für Multiskalen-Chemie (`Å → m`):** Trennt sauber Solver, Kinetik-Kerne (`Cantera`, `TChem`, `PelePhysics` AoT-Codegen) und Coupler (`preCICE`), definiert die 6 Speed-Hebel (Operator-Splitting, AoT-Jacobians, AMR) und die C++/Rust-`cxx`-Brücke. |
| **[`MesherAndSolverStep5Preview`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview)** *(Step 5 Preview: „MESHFORGE“)* | ⭐⭐⭐⭐☆ **(4.1/5)** | • [`src/data/content.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/data/content.ts#L225-L435) *(Direct CSR Indexing, `pilot_metric_kernel`, Enzyme Seed-Morphing + CAD-Tangentialprojektion)*<br/>• [`src/components/MeshErrorMapSection.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/components/MeshErrorMapSection.tsx) *(Interaktiver 4-Segment-Explorer & VRAM-Modell)*<br/>• [`src/components/ScalingSection.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/components/ScalingSection.tsx) *(Hilbert-SFC Partitionierer & $s$-step Krylov Skalierungsmodell)* | **Starkes Ideen- & Cockpit-UI-Vorbild:** Liefert den $128^3$ Pilot-LBM Sizing-Kernel (`pilot_metric_kernel`), die tangentiale CAD-Wandprojektion $(\mathbf{I}-\mathbf{n}\mathbf{n}^T)\nabla J$ für differenzierbares Voronoi-Seed-Morphing und interaktive Visualisierungen für unser Frontend (immer kombiniert mit den 3 Bugfixes aus `Sol61Max`!). |
| **[`BlackglasIChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/BlackglasIChemSugg.txt)**, **[`ChatGPTSol61SearchMaxChemSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/ChatGPTSol61SearchMaxChemSugg.txt)** & **[`Mistral4LargeChemSolverSugg.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeChemSolverSugg.txt)** | ⭐⭐⭐⭐ **(4.0/5)** / ⭐⭐⭐ | • [`BlackglasIChemSugg.txt#L61-L150`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/BlackglasIChemSugg.txt#L61-L150) *(DLBFoam 10× Speedup, Analytische Sparse-Jacobians 100× Speedup, DeepFlame, EBIdnsFoam, MUI)*<br/>• [`ChatGPTSol61SearchMaxChemSugg.txt#L23-L28`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/ChatGPTSol61SearchMaxChemSugg.txt#L23-L28) *(Sparse Preconditioned Cantera/SUNDIALS & Rust `diffsol`)* | Liefert die entscheidenden Literatur-Benchmarks für unser `species_chem`-Modul (**DLBFoam** dynamisches Lastbalancing steifer Chemie-Zellen, **100× Speedup** durch analytische dünnbesetzte Jacobi-Matrizen sowie Validierungsfälle **Sandia Flame D** & **ECN Spray A**). |
| **[`SolverSIMPLEPISOChatGPT6SolMax`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax)** *(ChatGPT 6 Sol Max: „continuum“)* | ⭐⭐⭐⭐⭐ **(5.0/5)** | • [`continuum.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/continuum.hpp) *(Rhie-Chow, SIMPLE/PISO Re-Projektion & echte 6%-`CorrectionSoA`)*<br/>• [`linear.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/linear.hpp) *(Knuth-Hash PMIS, Norm-skaliertes FP32-AMG & $\eta_{\text{round}}$-Floor)*<br/>• [`adjoint.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/adjoint.hpp) & [`enzyme_bridge.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/enzyme_bridge.cpp) *(Gekoppelter 3D Rhie-Chow/Brinkman Dual-IFT-Adjoint + CAD-Kettenregel)*<br/>• [`chemistry.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/chemistry.hpp), [`continuum_cuda.cu`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/cuda/continuum_cuda.cu), [`generate_mechanism.py`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/tools/generate_mechanism.py) & [`verify.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/tests/verify.cpp) | **Goldstandard für den nativen C++20/CUDA-Solver & Adjoint-Kern:** Mit `g++ -std=c++20 -O2` kompiliert und verifiziert (`massError = 2.35e-14`, `pisoMassError = 0`, `fdError = 1.27e-9`, `cadError = 6.66e-12`). Enthält echten Python-AoT-Code-Generator (`chain.json` $\to$ `chain_abc.hpp`), CUDA-12 `cub::DeviceSelect::Flagged` + `cudaMallocAsync` Compaction und den gekoppelten Rhie-Chow-Adjoint. |
| **[`KimiK31ProMaxSolverFVM.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt)** *(Kimi K3.1 Pro Max: „CONTINUUM_CABI“)* | ⭐⭐⭐⭐⭐ **(4.9/5)** | • [`KimiK31ProMaxSolverFVM.txt#L670-L1031`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L670-L1031) & [`#L4005-L4084`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L4005-L4084) *(2D/3D SIMPLE/PISO/PIMPLE + Monolithischer $4N\times 4N$ `(u,v,p,h)` Block-Löser)*<br/>• [`KimiK31ProMaxSolverFVM.txt#L1264-L1300`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L1264-L1300) & [`#L4605-L4644`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L4605-L4644) *(Exakte variabel-$c_p(Y)$ Temperatur-Jacobi-Zeile + C++20 AoT Sparse-Emitter)*<br/>• [`KimiK31ProMaxSolverFVM.txt#L1375-L1402`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L1375-L1402) *(Rhie-Chow-Mobilitäts-Adjoint & 3D CAD `boundary_geometry_seed`)*<br/>• [`KimiK31ProMaxSolverFVM.txt#L1587-L1600`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L1587-L1600) *(32-Zellen Shared-Memory GPU Work-Stealing)* | **Co-Goldstandard für monolithische Kopplung, Thermo-Chemie-Jacobians & 3D-CAD-Rand-Seeds:** Liefert die geschlossene Formel `boundary_geometry_seed` ($\partial Q/\partial\mathbf{x}_f, \partial Q/\partial\mathbf{S}_f$), die Quotientenregel für temperaturabhängige Mischungs-$c_p(\mathbf{Y})$ in der Energie-Jacobi-Zeile und 32-Zellen-Shared-Memory-Tile-Stealing auf der GPU. |
| **[`Mistral4LargeAdjointSolver`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeAdjointSolver)** *(Mistral 4 Large)* | ⭐⭐⭐⭐☆ **(4.2/5)** | • [`cpp/tests/verification.cpp#L404-L520`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeAdjointSolver/cpp/tests/verification.cpp#L404-L520) *(Complex-Step $h=10^{-30}$ Adjoint-Verifikation auf $7.28\times 10^{-12}$!)*<br/>• [`cpp/include/chem/chemistry.hpp#L173-L323`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeAdjointSolver/cpp/include/chem/chemistry.hpp#L173-L323) *(2-Stage L-stabile Rosenbrock-W `ROS2` + Matrix-Exponential `expm_dense`)*<br/>• [`cpp/include/linalg/krylov.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeAdjointSolver/cpp/include/linalg/krylov.hpp) *(PCG, BiCGStab, FGMRES(30) & FP32 Iterative Refinement)* | **Referenz für Complex-Step-Taylor-Tests & Matrix-Exponential-Benchmarks:** Beweist mit $h_{\text{cs}} = 10^{-30}$ ($\operatorname{Im}(J(\gamma + i h_{\text{cs}}))/h_{\text{cs}}$) komplett auslöschungsfreie Adjoint-Verifikation (22/22 C++20-Tests grün in `run_output.txt`). |
| **[`SolverSIMPLEPISOGemini38FlashHigh`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOGemini38FlashHigh)** *(Gemini 3.8 Flash High: „Aero-Continuum“)* | ⭐⭐⭐⭐☆ **(4.0/5)** | • [`cpp_engine/cabi_span.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOGemini38FlashHigh/cpp_engine/cabi_span.hpp) *(Strided `CAbiSpan<T>` für pitched GPU-Speicher)*<br/>• [`cpp_engine/discrete_adjoint.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOGemini38FlashHigh/cpp_engine/discrete_adjoint.hpp) *(RAMP Brinkman-Modell $\kappa(\gamma)$)*<br/>• [`src/components/RhieChowFVMView.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOGemini38FlashHigh/src/components/RhieChowFVMView.tsx) *(Interaktiver Rhie-Chow Checkerboard-Visualizer)* | Saubere C++20-Header für `CAbiSpan<T>` mit Stride-Unterstützung und sehr anschauliche interaktive UI-Darstellung der Rhie-Chow-Schachbrett-Dämpfung (*Hinweis: für $\eta_{\text{round}}$ und Stöchiometrie die Formeln aus `ChatGPT6SolMax`/`KimiK31` nutzen!*). |

---

## 7c. Kategorie `LMArena/Frontend & CAD` — Native `wgpu`/`egui`-Cockpits, WGSL-Shader, `CFDFLD03` In-Situ Post-Processing & `.axcad`
Ordner: [`resources/reference_spickzettel/LMArena/Frontend/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend) & [`resources/reference_spickzettel/LMArena/CAD/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/CAD)

| Spickzettel / Modell | Bewertung | Wichtigste Bookmark-Dateien (Direkt-Links) | Wofür wir diesen Spickzettel nutzen |
| :--- | :---: | :--- | :--- |
| **[`ChatGPT6LunaMaxCAENoSync`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync)** *(ChatGPT 6 Luna Max: „FLUX / CFD“)* | ⭐⭐⭐⭐⭐ **(5.0/5)** | • [`reference/rust/src/buffers.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/buffers.rs) *(Zero-Copy `bytemuck` Parser für `CFDGEO01`, `CFDMSH02`, `CFDFLD03`)*<br/>• [`reference/rust/src/viewport.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/viewport.rs) *(Camera-Relative `f64->(f32_hi, f32_lo)` Split, Reverse-Z & Async 1×1 `R32Uint` Pick)*<br/>• [`reference/rust/src/ipc.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/ipc.rs), [`physics.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/physics.rs) & [`mesh_quality.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/mesh_quality.rs) *(Tokio NDJSON Bridge, `RevisionClock`, `AbsoluteP` vs `ReducedPRgh`, Flat Quality)*<br/>• [`reference/shaders/mesh_inspect.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/mesh_inspect.wgsl), [`field_unlit.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/field_unlit.wgsl), [`surface_lic.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/surface_lic.wgsl) & [`streamline.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/streamline.wgsl) | **Goldstandard für unser Rust `wgpu` + `egui` Cockpit (Option C):** Idiomatisches, getestetes Rust-Crate für Zero-Copy-Binärpuffer, Reverse-Z, 8-Tap Depth-EDL, `@compute`-Shader Surface-LIC, GPU-instanzierte Streamline-Ribbons und die lockfreie `RevisionClock`. |
| **[`CAEVisualisation1Unknown.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation1Unknown.txt)** & **[`CAEVisualisation2Unknown.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt)** *(Paket 3 `CFDFLD03` Deep-Dive)* | ⭐⭐⭐⭐⭐ **(4.9/5)** | • [`CAEVisualisation1Unknown.txt#L78-L280`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation1Unknown.txt#L78-L280) & [`#L462-L480`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation1Unknown.txt#L462-L480) *(64-Byte `CFDFLD03` Header, Hardware SSE4.2 `_mm_crc32_u64` & 21-Bit 3D-Hilbert-Index)*<br/>• [`CAEVisualisation2Unknown.txt#L279-L667`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt#L279-L667) *(Windows `CreateFileMappingA` & POSIX `shm_open` Lock-Free SPMC Triple-Buffer-Ring in C++20 & Rust)*<br/>• [`CAEVisualisation2Unknown.txt#L675-L963`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt#L675-L963) *(IEEE-754 XOR-Delta + LZ4/ZFP + Lag-Dropping WebSocket Streamer)*<br/>• [`CAEVisualisation2Unknown.txt#L1059-L1694`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt#L1059-L1694) *(Polyeder-Slicer, Cardano-$\lambda_2$ & $Q$-Kriterium, Face-Exit RK4/Cash-Karp Streamlines, 3-Ring WLSQ $\boldsymbol{\tau}_w$, Paket-4-Integrale)* | **Komplette C++20- & Rust-Implementierung für `Paket 3 (CFDFLD03)` & In-Situ Post-Processing:** Liefert alle 5 Post-Processing-Kerne, den plattformübergreifenden Shared-Memory-Ringbuffer und das komprimierte Remote-Streaming. |
| **[`Qwen27bCockpitCAENoSync`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync)** *(Qwen 27B: „HydroCore“)* | ⭐⭐⭐⭐½ **(4.6/5)** | • [`src/fields/colormaps.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/fields/colormaps.ts) *(Exakter $256\times 1$ Oklab-LUT-Generator mit Ansys-Band-Quantisierung)*<br/>• [`rust/src/render/shaders.wgsl`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/rust/src/render/shaders.wgsl) & [`src/gl/shaders.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/gl/shaders.ts) *(2-Phasen MRT-Compositor, MatCap + 4-Tap Normal-EDL, Isolinien & LIC)*<br/>• [`src/gl/engine.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/gl/engine.ts) & [`src/units/parser.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/units/parser.ts) *(3D Slice-Plane-Gizmo, 4-Quadranten-Splitscreen & Unicode-SI-Parser)* | **Beste Vorlage für Oklab-LUT-Texturen, MRT-Compositing & 3D-Slice-Gizmo:** Kombiniert einen zweiphasigen MRT-Renderer (`[albedo, normal, id]`) mit wahrnehmungsgleichen Oklab-Farbtabellen und einem 4-Quadranten-Qualitäts-Splitscreen. |
| **[`Fable52xhighCockpit`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Fable52xhighCockpit)** *(Fable 5.2 xHigh)* & **[`Qwen38maxCADRender1`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen38maxCADRender1)** *(Qwen 3.8 Max)* | ⭐⭐⭐⭐½ **(4.6/5)** | • [`Fable52xhighCockpit/src/components/BlockStack.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Fable52xhighCockpit/src/components/BlockStack.tsx), [`Viewport3D.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Fable52xhighCockpit/src/components/Viewport3D.tsx), [`colormaps.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Fable52xhighCockpit/src/lib/colormaps.ts) & [`units.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Fable52xhighCockpit/src/lib/units.ts)<br/>• [`Qwen38maxCADRender1/crates/cad_renderer/src/pipeline.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen38maxCADRender1/crates/cad_renderer/src/pipeline.rs) & [`crates/cad_addons/src/rhai_engine.rs`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen38maxCADRender1/crates/cad_addons/src/rhai_engine.rs) | **Design-Referenz & Rhai-Addon-Architektur:** `Fable52xhighCockpit` definiert das Obsidian/Carbon-UI, den nTop-Block-Stack und die Colormaps; `Qwen38maxCADRender1` liefert ein komplettes 5-Crate Rust-Workspace (`wgpu 30` + `egui 0.37` + `rhai` Scripting-Addons). |
| **[`CADFiletypeGPT6Sol`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/CAD/CADFiletypeGPT6Sol)** *(GPT-6 Sol: „AXIOM `.axcad`“)* & **[`MuseEmberMax`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/MuseEmberMax)** | ⭐⭐⭐⭐☆ **(4.3/5)** | • [`AXIOM_FORMAT_SPEC.md`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/CAD/CADFiletypeGPT6Sol/public/AXIOM_FORMAT_SPEC.md) *(Trennung von Design-Intent & Binär-Cache, 6-Stufen-DAG-Invalidierung, PBR vs. Physik-Material)*<br/>• [`MuseEmberMax/src/App.tsx`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/MuseEmberMax/src/App.tsx) *(Interaktiver 64-Byte `CFDFLD03` Hex-Inspector & SPMC-Slot-Monitor)* | **Vorlage für Git-freundliche Projektdateien & `CFDFLD03`-Hex-Debugging:** Zeigt exakt, wie man deklarativen Intent (`manifest.json` / `project.toml`) von ableitbaren Binär-Caches trennt und Topologie-Konflikte bei B-Rep-Edits abfängt. |

---

## 7d. Vierte LMArena-Charge: Grenzschicht-Mesher, 3D-MMS/Adjoint-Verifikation, All-Mach/SPH-FVM, Cluster C & Cluster A (Neu vom 21.–22.11.2026)

| Ordner / Datei in `LMArena/` | Bewertung | Wichtigste C++20/CUDA-Dateien & Zeilen-Lesezeichen | Was wir konkret daraus übernehmen |
| :--- | :---: | :--- | :--- |
| **[`Mesher/BoundaryLayerMeshing1GPTAstra62max/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/octree_hexcore.cpp)** *(GPT Astra 6.2 Max)* | ⭐⭐⭐⭐⭐ **(9.9/10)** | • [`engine/superelements.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/superelements.hpp) & [`engine/templates.hpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/templates.hpp) *(23 kanonische Stromberg-Templates, 48 Symmetrien, 125-Bit Sub-Voxel-Voting)*<br/>• [`engine/geometry.hpp#L185-L204`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/geometry.hpp#L185-L204) *(Quadratische Prismen-Jacobi-Schranke $\min_{t\in[0,1]}(At^2+Bt+C)$)*<br/>• [`engine/fa_mesh.hpp#L54-L70`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/fa_mesh.hpp#L54-L70) *(Dreiecks-Abwicklungs-Geodäte & Dihedral-Krümmung)*<br/>• [`tests/certify_dyadic.py`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/tests/certify_dyadic.py) *(Exakte dyadische Rationalzahl-Zertifizierung)* | **Gold-Standard für Phase 2.5 Boundary-Layer, 23 Super-Elements & `faMesh` (`Snippet 25`):** Zertifiziert 0 Hanging Nodes, exakt 0 Gauss-Residuum und stetige Prismen-Positivität $\det\mathbf{J}(t) > 0$ für alle $t\in[0,1]$. |
| **[`Fable52high.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Fable52high.txt)** *(Fable 5.2 High: `mphys` Rust + C++20)* | ⭐⭐⭐⭐⭐ **(9.8/10)** | • `quantity.hpp` *(Zeilen 9560–9845: 7-Exponenten-SI, `AbsolutePressurePa` vs. `GaugePressurePa` vs. `ReducedPressurePRgh`)*<br/>• `mesh_arena.hpp` *(Zeilen 9848–10060: 2-MiB-Page Bump-Arena + Epoch-Tagging)*<br/>• `deterministic.hpp` *(Zeilen 10063–10309: Skilling 3D-Hilbert, 4096-Blatt-Baumsumme, LSD-Radix)*<br/>• `selector.hpp` *(Zeilen 10312–11014: Zero-Alloc Bytecode-VM)* & `flight_recorder.hpp` *(Zeilen 11017–11318: `amips_tet`)* | **Bit-identischer Rust/C++20 Systems-Core (`Snippet 26`):** Garantiert threadzahl-unabhängige Reduktionen und 1:1 Parität zwischen Rust-Node-Manager und C++20-Kerneln. |
| **[`Solver/MultiphaseCompressible1PGT62SolMax/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/physics.hpp)** *(GPT 6.2 Sol Max)* | ⭐⭐⭐⭐⭐ **(9.7/10)** | • [`native/physics.hpp#L44-L93`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/physics.hpp#L44-L93) *(Geschlossene quadratische Peng-Robinson-EOS in $w=\sqrt{T}$)* & [`#L200-L265`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/physics.hpp#L200-L265) *(All-Mach HLLC & AUSM+-up)*<br/>• [`native/hybrid.hpp#L50-L92`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/hybrid.hpp#L50-L92) *(OPMIS Wendland-$C^2$ MLS-Transfer)*<br/>• [`native/film.hpp#L41-L186`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/film.hpp#L41-L186) *(2D-Wandfilm & Bai-Gosman 4-Regime-Splash)* | **Referenz für Cluster B (`Snippet 27`):** 51/51 bestandene C++20-Tests + CPU/CUDA-Parität (`< 5e-12`). Löst Peng-Robinson ohne Newton-Iteration und koppelt FVM, OPMIS-SPH und 2D-Wandfilm massenerhaltend ($4.08\times 10^{-15}$). |
| **[`Solver/MultiphysicsModules2GPT61SolMax/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp)** *(GPT 6.1 Sol Max)* | ⭐⭐⭐⭐⭐ **(9.6/10)** | • [`public/solver/cluster_c.cpp#L195-L228`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L195-L228) *(Nicht-orthogonale elliptische Flussprojektion `project_flux`)*<br/>• [`#L248-L346`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L248-L346) *(2-RC-ECM + zukünftige Temperatur-Patankar-Bisektion & Farassat 1A)*<br/>• [`#L366-L410`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L366-L410) *(Ni et al. 4-Schritt-MHD)* & [`#L510-L561`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L510-L561) *(Exakte 3D Swept-Volume Simpson-BDF2-DGCL-Quadratur)* | **Referenz für Cluster C & 3D Polyeder-ALE (`Snippet 28`):** 35/35 bestandene Tests inkl. exakter Simpson-Swept-Volume-Quadratur ($1.21\times 10^{-17}\,\text{m}^3/\text{s}$ DGCL-Fehler auf verformten 3D-Zellen!). |
| **[`Mesher/FVMVerificationEngine1Inkling2Small/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/assembly.h)** *(Inkling 2 Small)* | ⭐⭐⭐⭐⭐ **(9.6/10)** | • [`engine/mesh.h#L430-L505`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/mesh.h#L430-L505) & [`engine/assembly.h#L206-L235`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/assembly.h#L206-L235) *(Implizite WLS-Gradienten-Einbettung $\mathbf{L}_{c,j}$ in $\mathbf{A}$)*<br/>• [`engine/assembly.h#L68-L108`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/assembly.h#L68-L108) *($2\times 2\times 2$ Gauss-Quellquadratur)* & [`#L330-L346`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/assembly.h#L330-L346) (`luSolveT`) | **Implizite Nicht-Orthogonalitäts-Matrix & IFT-Adjoint (`Snippet 26`):** Konvergiert bei $47.7^\circ$ Gitterverzerrung in **1 linearen Solve ohne Picard-Schleife** ($p_{\text{obs}} = 1.9586$) und trifft den Complex-Step-Adjoint auf $1.14\times 10^{-14}$. |
| **[`Mesher/FVMVerificationEngine2Gemini38Flashhigh/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine2Gemini38Flashhigh/cpp/fvm_solver.hpp)** *(Gemini 3.8 Flash High)* | ⭐⭐⭐⭐½ **(9.3/10)** | • [`cpp/geometry.hpp#L313-L424`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine2Gemini38Flashhigh/cpp/geometry.hpp#L313-L424) *(Tet-Pyramiden-Zellvolumen & Ebenen-Schnitt $\mathbf{m}_f^{\text{skew}}, \mathbf{k}_f^{\text{corr}}$)*<br/>• [`cpp/fvm_solver.hpp#L354-L396`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine2Gemini38Flashhigh/cpp/fvm_solver.hpp#L354-L396) *(Picard Deferred-Correction)* & [`cpp/adjoint.hpp#L220-L408`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine2Gemini38Flashhigh/cpp/adjoint.hpp#L220-L408) *(6-Param. IFT-Adjoint, $2.26\times 10^{-15}$ Fehler)* | **Sauberste modulare V&V-Suite:** Beweist den Ordnungsverlust von $p_{\text{obs}} = 1.951$ auf $0.964$ beim Abschalten von $\mathbf{k}_f^{\text{corr}}$ und prüft 6 geometrische Gitter-Pathologien. |
| **[`Solver/Turbulence3GPT6LunaMax/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence3GPT6LunaMax/cpp/include/cluster_a/cluster_a.hpp)** *(GPT-6 Luna Max)* | ⭐⭐⭐⭐½ **(9.2/10)** | • [`cpp/include/cluster_a/cluster_a.hpp#L483-L551`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence3GPT6LunaMax/cpp/include/cluster_a/cluster_a.hpp#L483-L551) *(Exakte Rang-1 Forchheimer-Jacobi-Matrix & P1-Marshak-Rand)*<br/>• [`cpp/cuda/cluster_a_kernels.cu#L52-L125`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence3GPT6LunaMax/cpp/cuda/cluster_a_kernels.cu#L52-L125) *(10 CUDA-Kerne: CSR-Gather & block-gekoppelte Patankar-SST)* | **Referenz für Cluster-A CUDA-Kerne & Forchheimer-Jacobi-Matrix (`Snippet 29`):** Einzige Implementierung mit exakter analytischer Tangentenmatrix $J_{ij} = \mu D_{ij} + \frac{1}{2}\rho(|\mathbf{u}|F_{ij} + \frac{(\mathbf{F}\mathbf{u})_i u_j}{|\mathbf{u}|})$. |
| **[`Solver/Turbulence1ClaudeHaiku55med/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence1ClaudeHaiku55med/cpp/include/mp_turbulence_cht.hpp)** & **[`Turbulence2ClaudeHaiku55med/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2ClaudeHaiku55med/cpp/cluster_a.hpp)** | ⭐⭐⭐⭐½ **(9.0 & 8.8/10)** | • [`Turbulence1.../mp_turbulence_cht.hpp#L187-L241`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence1ClaudeHaiku55med/cpp/include/mp_turbulence_cht.hpp#L187-L241) *(Newton-Patankar SST & SA-noft2)*<br/>• [`Turbulence2.../cluster_a.hpp#L236-L275`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2ClaudeHaiku55med/cpp/cluster_a.hpp#L236-L275) *(Jayatilleke-$P$ Thermowandgesetz)* & [`#L429-L462`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2ClaudeHaiku55med/cpp/cluster_a.hpp#L429-L462) *(Aitken-$\Delta^2$ Robin-Robin CHT)* | **100 % Verifikationsrate (30/30 & 36/36 Checks, `Snippet 29`):** Aitken-$\Delta^2$ Robin-Robin CHT konvergiert in **3 Schritten** auf $8.79\times 10^{-11}\,\text{K}$ an die monolithische Lösung! |
| **[`Solver/MultiphysicsModules1GeminiArgon4High/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules1GeminiArgon4High/cpp/cluster_c_multiphysics.cpp)** & **[`MultiphysicsModuleGLM53.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/MultiphysicsModuleGLM53.txt)** | ⭐⭐⭐⭐½ **(9.0 & 8.6/10)** | • [`cluster_c_multiphysics.cpp#L419-L476`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules1GeminiArgon4High/cpp/cluster_c_multiphysics.cpp#L419-L476) *(Hatchard-Spotnitz mit SEI-Tunneling $t_{\text{SEI}}$ & Rosenbrock-W)* & [`#L1107-L1146`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules1GeminiArgon4High/cpp/cluster_c_multiphysics.cpp#L1107-L1146) *(PICA Blowing-Newton)*<br/>• [`MultiphysicsModuleGLM53.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/MultiphysicsModuleGLM53.txt) *(Zeilen 1264–1565 & 3073–3295: $5\times 5$ `expm_phi1` & `Ros2`)* | **Elektrochemie- & PICA-Ablations-Perlen (`Snippet 28`):** Dynamische SEI-Schichtdicken-Barriere $\exp(-t_{\text{SEI}}/t_{\text{SEI},0})$, Transpirations-Blowing $\psi_{\text{blow}} = \phi/(e^\phi-1)$ und $5\times 5$ Matrix-Exponential-Integrator. |
| **[`Mesher/BoundaryLayerMeshing2MistralLarge4high/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing2MistralLarge4high/mesher/engine.h)** & **[`Solver/Turbulence2GLM53max/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2GLM53max/src/cpp/cluster_a_radiation.hpp)** | ⭐⭐⭐⭐☆ **(8.6 & 8.5/10)** | • [`MistralLarge4high/mesher/engine.h#L145-L162`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing2MistralLarge4high/mesher/engine.h#L145-L162) *(Sichtkegel-Projektion $\cos 65^\circ$)* & [`quality.h#L51-L64`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing2MistralLarge4high/mesher/quality.h#L51-L64) *(Pyramiden-Faltungstest)*<br/>• [`Turbulence2GLM53max/.../cluster_a_wall.hpp#L89-L135`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2GLM53max/src/cpp/cluster_a_wall.hpp#L89-L135) *(Spalding + Newton $u_\tau$)* & [`cluster_a_radiation.hpp#L161-L274`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2GLM53max/src/cpp/cluster_a_radiation.hpp#L161-L274) *($S_8/S_{16}$ fvDOM)* | **Spalding-Newton-$u_\tau$, $S_8/S_{16}$ fvDOM & Pyramiden-Qualität (`Snippets 25 & 29`):** Liefert den 128/512-Richtungs-fvDOM-Sweep mit Spiegel-Reflexion und das exakte $\operatorname{tr}(\mathbf{g}^2)$-WALE. |
| **[`BoundaryLayerMeshing1Inkling2mini`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1Inkling2mini/engine/geometry.hpp)** (6.2), **[`FVMVerificationEngine1Nemotron35SuperVL`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Nemotron35SuperVL/verification_engine.cpp)** / **[`FVMVerificationEngineAUnknown.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/FVMVerificationEngineAUnknown.txt)** (5.5), **[`Turbulence1MM31flash`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence1MM31flash/)** (5.2) & **[`MultiphysicsModules1GLM53`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules1GLM53/)** (0.0) | ⭐⭐☆☆☆ **(0.0–6.2/10)** | • `Inkling2mini/engine/geometry.hpp#L31-L45` *(Kompensierte `KahanVec3`-Summation — Rest wegen Fake-$J_{\text{ENS}}$ nicht nutzen)*<br/>• *Red-Team-Ausschluss:* `Nemotron35SuperVL` (nur 3/6 WLS-Nachbarn, Fake-FD-Adjoint), `Turbulence1MM31flash` (12/16 Tests rot, `NaN` in P1) und `MultiphysicsModules1GLM53` (leeres Vite-Template). | **Ausschließlich `KahanVec3` aus `Inkling2mini` nutzen; alle anderen durch die Top-10-Referenzen oben ersetzt!** |


---

## 8. Gold-Snippets & Direkt-Lesezeichen (Wichtigste Codeblöcke mit Zeilennummern)

Hier sind die wichtigsten „Gold-Blöcke“ aus deinen geklonten Repos direkt mit exakter Zeilennummer (`#L...`) und dem extrahierten Kern-Snippet dokumentiert:

---

### Snippet 1: Fused Single-Pass Gauss-Gradient (aus [`OpenFOAM_base/.../fusedGaussGrad.C#L86-L109`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/fused/finiteVolume/fusedGaussGrad.C#L86-L109))
* **Warum wichtig:** Berechnet den Zellgradienten $\nabla \phi_i = \frac{1}{V_i} \sum_f \phi_f \mathbf{S}_f$ in einer einzigen Schleife über alle internen Flächen (`owner` / `neighbour`), ohne ein temporäres Oberflächenfeld im RAM anzulegen.
```cpp
// Aus fusedGaussGrad.C (Zeilen 86–109):
forAll(owner, facei)
{
    const GradType Sfssf = Sf[facei]*issf[facei];
    igGrad[owner[facei]]     += Sfssf;
    igGrad[neighbour[facei]] -= Sfssf;
}
// Rand-Patches aufaddieren und durch Zellvolumen teilen:
igGrad /= mesh.V();
```

---

### Snippet 2: Nullte-Ordnung-Randkonsistenz für Eulerian SPH-FVM (aus [`SPHinXsys/.../eulerian_ghost_boundary.cpp#L25-L41`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/general_dynamics/boundary_condition/eulerian_ghost_boundary.cpp#L25-L41))
* **Warum wichtig:** Exakte Umsetzung von **Gleichung (23)** aus deinem TU-München-Paper ([`MeshingFVM_SPH_2312.03542v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshingFVM_SPH_2312.03542v1.pdf)): Berechnet für Randpartikel mit fehlenden Nachbarn den exakten FVM-Abschlussvektor $\mathbf{A}_{iS}^{ESPH} = -\sum_j V_j \nabla W_{ij}$.
```cpp
// Aus eulerian_ghost_boundary.cpp (Zeilen 29–41):
Vecd gradient_summation = Vecd::Zero();
Neighborhood &inner_neighborhood = inner_configuration_[index_i];
for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
{
    size_t index_j = inner_neighborhood.j_[n];
    Real dW_ijV_j = inner_neighborhood.dW_ij_[n] * Vol_[index_j];
    gradient_summation += dW_ijV_j * inner_neighborhood.e_ij_[n];
}
Real ghost_particle_dW_ijV_j = -gradient_summation.norm();
Vecd ghost_particle_eij      = -gradient_summation / ghost_particle_dW_ijV_j;
Real distance_to_ghost       = fabs(sph_body_->getInitialShape().findSignedDistance(pos_[index_i]));
```

---

### Snippet 3: HLLC Riemann-Löser mit Dissipations-Limiter (aus [`SPHinXsys/.../eulerian_riemann_solver.cpp#L95-L165`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SPHinXsys/src/shared/particle_dynamics/fluid_dynamics/eulerian_fluid_dynamics/eulerian_riemann_solver.cpp#L95-L165) & [`SU2/.../hllc.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CFD_Multiphysics/SU2/SU2_CFD/src/numerics/flow/convection/hllc.cpp))
* **Warum wichtig:** Löst das eindimensionale Riemann-Problem an jeder Zell-/Partikelgrenze für kompressible Strömungen, Stoßwellen und Kavitationsblasen (Gleichung 7–16 im TUM-Paper).
```cpp
// Aus eulerian_riemann_solver.cpp (Zeilen 29–40):
Real ul = -e_ij.dot(state_i.vel_);
Real ur = -e_ij.dot(state_j.vel_);
Real s_l = ul - compressible_fluid_i_.getSoundSpeed(state_i.p_, state_i.rho_);
Real s_r = ur + compressible_fluid_j_.getSoundSpeed(state_j.p_, state_j.rho_);
Real denominator = state_j.rho_ * (s_r - ur) + state_i.rho_ * (ul - s_l);
Real s_star = (state_j.rho_ * ur * (s_r - ur) + state_i.rho_ * ul * (ul - s_l) + state_i.p_ - state_j.p_) / denominator;
```

---

### Snippet 4: Svanbergs MMA (Method of Moving Asymptotes) für 3D-SIMP (aus [`TopOpt_in_PETSc/MMA.cc#L499-L580`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/Optimization_AD/TopOpt_in_PETSc/MMA.cc#L499-L580))
* **Warum wichtig:** Der Goldstandard-Optimierer für Topologieoptimierung (aus deinem TU-Hamburg FOSM-Paper [`ArbitraryFunctionOptimization_s00158-023-03540-w.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/ArbitraryFunctionOptimization_s00158-023-03540-w.pdf)). Baut in `GenSub()` die beweglichen Asymptoten $L_i, U_i$ auf (`asyminit = 0.5, asymdec = 0.7, asyminc = 1.2`) und löst das konvexe Subproblem über einen schnellen Dual-Interior-Point-Löser (`SolveDIP`).
```cpp
// Aus TopOpt_in_PETSc/MMA.cc (Zeilen 499–518):
PetscErrorCode MMA::Update(Vec xval, Vec dfdx, PetscScalar* gx, Vec* dgdx, Vec xmin, Vec xmax) {
    // 1. Konvexes Approximations-Subproblem & Asymptoten L, U aufbauen:
    GenSub(xval, dfdx, gx, dgdx, xmin, xmax);
    // 2. Letzte zwei Design-Stände für Asymptoten-Trend sichern:
    VecCopy(xo1, xo2);
    VecCopy(xval, xo1);
    // 3. Duales Problem per Interior-Point-Methode lösen:
    SolveDIP(xval);
    return 0;
}
```

---

### Snippet 5: Borrvall-Petersson & RAMP Brinkman-Interpolation (aus [`OpenFOAM_base/.../BorrvallPeterssonInterpolation.C#L61-L91`](file:///G:/Datein/Projects/CFDSolver/OpenFOAM_base/openfoam/src/optimisation/adjointOptimisation/adjoint/optimisation/designVariables/topODesignVariables/interpolationFunctions/BorrvallPetersson/BorrvallPeterssonInterpolation.C#L61-L91))
* **Warum wichtig:** Schaltet in jeder Zelle stufenlos und differenzierbar zwischen Fluid und Festkörper um (inklusive exakter Ableitung für den Adjoint-Gradienten).
```cpp
// Aus BorrvallPeterssonInterpolation.C (Zeilen 73 & 88):
res   = arg / (scalar(1) + b * (scalar(1) - arg));
deriv = (b + scalar(1)) / sqr(scalar(1) + b * (scalar(1) - arg));
```

---

### Snippet 6: Mixed-Precision Iterative Refinement (`FP16`/`FP32` innen, `FP64` außen) (aus [`ginkgo/core/solver/ir.cpp#L216-L266`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LinearSolvers_HPC/ginkgo/core/solver/ir.cpp#L216-L266))
* **Warum wichtig:** Die Vorlage für unser **`PrecisionAddon::LowPrecisionBooster`**: Das Residuum $\mathbf{r} = \mathbf{b} - \mathbf{A}\mathbf{x}$ wird in voller `FP64`-Präzision berechnet, der innere Löser (`solver_->apply(residual_ptr, inner_solution)`) rechnet die Korrektur schnell in niedriger Präzision (`FP16`/`FP32`), und das Ergebnis wird auf die `FP64`-Lösung aufaddiert.
```cpp
// Aus ginkgo/core/solver/ir.cpp (Zeilen 248–262):
// 1. Äußeres Residuum r = b - A*x in FP64 aktualisieren (in update_residual)
// 2. Inneres Gleichungssystem A * inner_solution = residual in niedriger Präzision lösen:
inner_solution->fill(zero<ValueType>());
solver_->apply(residual_ptr, inner_solution);
// 3. Korrektur in FP64 aufaddieren: x = x + relaxation_factor * inner_solution
dense_x->add_scaled(relaxation_factor_, inner_solution);
```

---

### Snippet 7: Innen-/Außen-Test & Signed Distance für Millionen CFD-Zellen (aus [`libigl/include/igl/signed_distance.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/signed_distance.cpp) & [`fast_winding_number.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/CAD/libigl/include/igl/fast_winding_number.cpp))
* **Warum wichtig:** Entscheidet über Hierarchische Winding-Numbers selbst bei nicht-wasserdichten CAD-Netzen in Millisekunden für jede Gitterzelle $\mathbf{x}_i$, wie weit sie von der Wand entfernt ist ($\psi_i$) und ob sie im Metall ($\gamma_i = 0$) oder im Fluid ($\gamma_i = 1$) liegt.


---

### Snippet 8: Kombinatorische Super-Element-Auswahl, Asymmetrisches Residual Volume & $J_{\text{ENS}}$ (aus [`SuperElements_Db_115554.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/SuperElements_Db_115554.pdf), Stromberg 2023, Kap. 3.3, 5, 6 & Anhang B/C)
* **Warum wichtig:** Ersetzt instabiles geometrisches Cut-Cell-Clipping an Bauteilrändern und SIMP-Isoflächen durch **23 offline mittels Answer Set Programming (Clingo) beweisbar optimal gelöste Hexaeder-Super-Elemente** (über 35 lokalen Kandidatenknoten in $[-1,1]^3$ inkl. 8 Oktanten-Stützknoten $(\pm 0.5, \pm 0.5, \pm 0.5)$, Tab. B.1 & Tab. C.2–C.4, S. 121–155). Garantiert **$\min SJ \ge 0.302$**, **0 Hanging Nodes**, **0 Hanging Edges** (wichtig für quadratische Ansätze!) und assembliert das gesamte Gitter über eine asymmetrische Residual-Volume-Strafe + Mehrheits-Voting in strikt linearer Zeit $\mathcal{O}(n_{\text{elem}})$!
```cpp
// 1. Aspektverhältnis-sensitiver Knoten-Scaled-Jacobian (Stromberg Gl. 3.30, S. 37):
//    Bestraft im Gegensatz zum klassischen SJ auch extreme Streckungen über (max l_k)^3:
double nodal_scaled_jacobian_iso(const Vec3& e1, const Vec3& e2, const Vec3& e3, double max_elem_edge) {
    const double detJ = dot(e1, cross(e2, e3));
    return detJ / (max_elem_edge * max_elem_edge * max_elem_edge + 1e-30);
}

// 2. Element Normalized Scaled Jacobian J_ENS für gemischte Netze (Hex8, Prism6, Pyr5, Tet4)
//    (Lobos 2015 / Stromberg Gl. 3.32, S. 38 + fiktives Basis-Hexaeder gegen invertierte Pyr-Basen):
double element_normalized_sj(double js_min, CellType type) {
    // J_{S,opt}: Hex8 = 1.0, Prism6 = sqrt(3)/2 (0.866025), Tet4 & Pyr5 = sqrt(2)/2 (0.707107)
    const double js_opt = (type == CellType::HEXAHEDRON) ? 1.0 :
                          (type == CellType::WEDGE)      ? 0.8660254037844386 :
                                                           0.7071067811865476;
    if (js_min >  js_opt) return (1.0 + js_opt) - js_min;
    if (js_min < -js_opt) return -(1.0 + js_opt) - js_min;
    return js_min / js_opt;
}

// 3. O(n_elem) Greedy Voting mit quadratischer Unterdeckungs-Strafe gegen Zigzag-Artefakte (Kap. 6.2 & 6.4):
//    R(e, D) = (RV_missing)^2 + RV_excess  (D in 0..255 Bitmaske der 8 Outer Nodes)
uint8_t select_best_superelement_mask(const uint32_t missing_subdomains[256],
                                      const uint32_t excess_subdomains[256]) {
    uint8_t best_mask = 0;
    uint64_t best_cost = UINT64_MAX;
    for (int d = 0; d < 256; ++d) {
        const uint64_t m = missing_subdomains[d];
        const uint64_t cost = m * m + excess_subdomains[d];
        if (cost < best_cost) { best_cost = cost; best_mask = static_cast<uint8_t>(d); }
    }
    return best_mask; // Danach Mehrheits-Voting v(e, N) in {+1, -1} auf allen Precursor-Knoten N!
}
```


---

### Snippet 9: Meshless 3D-Voronoi-Clipping im GPU Shared Memory & Analytische Ableitungen (aus [`MeshlessVoronoiOnGPU_3272127.3275092.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/MeshlessVoronoiOnGPU_3272127.3275092.pdf) & [`VoronoiDiagrams_2404.18629v1.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/VoronoiDiagrams_2404.18629v1.pdf))
* **Warum wichtig:** Da jede Ecke eines generischen konvexen 3D-Polyeders exakt die Valenz 3 (Schnittpunkt von 3 Halbraum-Ebenen $\mathcal{P}_u \cap \mathcal{P}_v \cap \mathcal{P}_w$) hat, speichert Ray et al. (2018) eine komplette 3D-Voronoi-/Laguerre-Zelle rein dual als `float4 P[64]` (Ebenen) und `int3 T[96]` (duale Dreiecke = primale Ecken) lokal pro GPU-Thread (10 Mio. Zellen in 800 ms, 0 Locks!). Und exakt dieses $3\times 3$-System liefert nach Numerow et al. (SIGGRAPH 2024, Gl. 4) sofort die analytische Ableitung $\frac{\partial \mathbf{x}_c}{\partial \mathbf{c}} = \mathbf{A}^{-1}(\frac{\partial \mathbf{b}}{\partial \mathbf{c}} - \frac{\partial \mathbf{A}}{\partial \mathbf{c}}\mathbf{x}_c)$!
```cpp
// 1. Schnittpunkt x_c dreier Bisektor-/Randebenen P_u, P_v, P_w (Ray et al. Sec. 3.2 & Numerow Gl. 4):
//    Für Power-Diagramm (Saatpunkte x_0..x_3, Gewichte w_0..w_3):
//    (x_k - x_0) . x_c = 0.5 * (||x_k||^2 - ||x_0||^2 + w_0 - w_k)   für k in {1, 2, 3}
inline Vec3 intersect_three_planes(const Vec3& n1, double b1,
                                   const Vec3& n2, double b2,
                                   const Vec3& n3, double b3) {
    const Vec3 c23 = cross(n2, n3);
    const double detA = dot(n1, c23); // Mit Melquiond-Pion FP-Filter -> exakt bei |detA| < eps
    return (b1 * c23 + b2 * cross(n3, n1) + b3 * cross(n1, n2)) / detA;
}

// 2. Exakte analytische Sensitivität dx_c / dx_k eines Voronoi-Eckpunkts x_c nach Saatpunkt x_k (k in {1,2,3})
//    aus A * x_c = b  ==>  A * (dx_c / dx_k) = db/dx_k - (dA/dx_k) * x_c = (x_k - x_c)^T * e_k:
inline Mat3 voronoi_vertex_jacobian_wrt_seed_k(const Mat3& A_inv, const Vec3& x_k, const Vec3& x_c, int k_row) {
    // Zeile k_row von (db/dx_k - (dA/dx_k)*x_c) ist (x_k - x_c)^T:
    const Vec3 rhs_row = x_k - x_c;
    return outer_product(A_inv.col(k_row), rhs_row); // dx_c / dx_k in R^{3x3}, C^1-glattes Zellvolumen V_i!
}
```

---

### Snippet 10: Einheitliche Householder-Symmetrie-BC für Vektoren & Tensoren 2. Stufe in CC-FVM (aus [`Demirdzic-2022-...pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Demirdzic-2022-Symmetryplaneboundaryconditionsforcell-centeredfinite-volumecontinuummechanics.pdf), Gl. 8–22, 34–37)
* **Warum wichtig:** An jeder beliebig orientierten Symmetrieebene (Flächenvektor $\mathbf{s}$, Einheitsnormale $\mathbf{n} = \mathbf{s}/\|\mathbf{s}\|$) steht der Spiegelzell-Verbindungsvektor $\boldsymbol{\Delta} = \overrightarrow{PM} = 2\frac{\mathbf{d}\cdot\mathbf{s}}{\mathbf{s}\cdot\mathbf{s}}\mathbf{s}$ exakt senkrecht auf der Fläche ($\implies$ **Cross-Diffusion ist identisch Null!**). Über den Householder-Reflektor $\mathbf{R} = \mathbf{I} - 2\mathbf{n}\otimes\mathbf{n}$ folgen der exakt diagonaldominante Patankar-Split für Geschwindigkeit $\mathbf{v}$, Strukturverschiebung $\mathbf{u}_s$ sowie den Cauchy-Green-/Spannungstensor $\mathbf{T}$:
```cpp
// 1. Vektor-Diffusionsfluss D_v auf beliebiger Symmetrieebene s (Demirdzic & Cardiff 2022, Gl. 15–16):
//    D_{vi} = D_{vi,ex} - D_{vi,im} * v_{Pi}   (mit d = x_B - x_P, d_dot_s = dot(d, s) > 0)
inline void symmetry_vector_diffusion_split(double gamma_v, const Vec3& s, const Vec3& d, const Vec3& v_P,
                                            Vec3& D_im_diag, Vec3& D_ex_rhs) {
    const double inv_ds = gamma_v / dot(d, s);
    const double s_dot_v = dot(s, v_P);
    for (int i = 0; i < 3; ++i) {
        D_im_diag[i] = inv_ds * (s[i] * s[i]);                       // Positiv auf Matrix-Hauptdiagonale a_P!
        D_ex_rhs[i]  = -inv_ds * (s_dot_v - s[i] * v_P[i]) * s[i];   // Deferred Correction auf RHS b_P
    }
}

// 2. Symmetrischer Tensor 2. Stufe T (z. B. linker Cauchy-Green-Tensor B = F*F^T in Monolithic FSI, Gl. 21–22):
inline double symmetry_tensor_implicit_diag(double gamma_T, const Vec3& s, const Vec3& d, int i, int j) {
    return gamma_T * (s[i] * s[i] + s[j] * s[j]) / dot(d, s);        // Gl. 21: Impliziter Diagonalbeitrag für T_{ij}
}
```

---

### Snippet 11: Exaktes 3D-Swept-Volume & BDF2-DGCL für bewegte Polyeder-Netze (aus [`DiscreteGCL_1709.04788v4.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/DiscreteGCL_1709.04788v4.pdf) / Guillard & Farhat ALE-DGCL)
* **Warum wichtig:** Bewegt sich ein unstrukturiertes 3D-Polyedergitter von $t^n$ nach $t^{n+1}$, ist der Flächenvektor $\mathbf{S}_f(\tau)$ bei linearer Knotenbewegung quadratisch in $\tau \in [0, 1]$. Damit eine konstante Freiströmung $u_\infty$ ohne künstliche Druck-/Massenquellen exakt erhalten bleibt (Audit-H9!), muss das überstrichene Volumen $\Delta V_f^{n,n+1}$ über die **Simpson-Regel** berechnet und in BDF2 mit denselben Zeitschrittgewichten $(\frac{3}{2}, -\frac{1}{2})$ wie die Zeitableitung kombiniert werden:
```cpp
// Exaktes überstrichenes Facettenvolumen dV_f^{n,n+1} in 3D via Simpson-Regel (exakt für quadratisches S_f(tau))
// und BDF2-konsistenter Gittervolumenfluss Vdot_f^{n+1}:
inline double compute_bdf2_dgcl_face_flux(const Vec3& w_f_disp, // Verschiebung dx_f = x_f^{n+1} - x_f^n
                                          const Vec3& Sf_n, const Vec3& Sf_half, const Vec3& Sf_np1,
                                          double dV_f_prev, double dt) {
    const double dV_f_curr = (1.0 / 6.0) * dot(w_f_disp, Sf_n + 4.0 * Sf_half + Sf_np1);
    // BDF2-DGCL: (3*V^{n+1} - 4*V^n + V^{n-1}) / (2*dt) == sum_f Vdot_f^{n+1}
    return (1.5 * dV_f_curr - 0.5 * dV_f_prev) / dt;
}
```

---

### Snippet 12: Linear-Exakter Ebenen-Schnitt $\beta_f$, 6-Range `FaceRanges` & Atomik-freier `CellFaceCSR`-Gather (aus [`MesherAndSolverSol61Max/src/snippets/`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/face_geometry.hpp#L25-L53))
* **Warum wichtig:** Schneidet die Verbindungsgerade $\mathbf{x}(\beta) = \mathbf{x}_P + \beta(\mathbf{x}_N - \mathbf{x}_P)$ mit der **Flächenebene** $(\mathbf{x} - \mathbf{x}_f)\cdot\mathbf{S}_f = 0$ via $\beta_f = \frac{(\mathbf{x}_f - \mathbf{x}_P)\cdot\mathbf{S}_f}{\mathbf{d}_f\cdot\mathbf{S}_f}$ (statt orthogonaler Linienprojektion). Dadurch liegt $\mathbf{m}_f^{\text{skew}} = \mathbf{x}_f - (\mathbf{x}_P + \beta_f\mathbf{d}_f)$ exakt in der Flächenebene und die Kombination aus 2-Punkt-Basisoperator + expliziter Korrektur reproduziert **jedes beliebige lineare Feld $\phi(\mathbf{x}) = \phi_0 + \mathbf{g}\cdot\mathbf{x}$ auf beliebig schiefen 3D-Flächen bis auf Maschinengenauigkeit** (bewiesen in [`reference_test.cpp#L20-L26`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/reference_test.cpp#L20-L26)). Der anschließende `gather_residual`-Kernel sammelt Flüsse deterministisch ohne `atomicAdd` und ohne Ghost-Cell-Speicherkorruption:
```cpp
// 1. Aus face_geometry.hpp (Zeilen 25–53): Linear-exakte Flächengeometrie & expliziter Invalid-Pfad
inline FaceGeometry geometry(Vec3 xp, Vec3 xn, Vec3 xf, Vec3 area,
                             double h_ref, double skew_tol = 1e-12, double non_ortho_tol = 1e-12) {
    FaceGeometry g;
    const Vec3 d = xn - xp;
    const double ds = dot(d, area), d2 = dot(d, d), s2 = dot(area, area);
    if (!(d2 > 0 && s2 > 0 && h_ref > 0)) return g;
    const double cosine = ds / std::sqrt(d2 * s2);
    if (!std::isfinite(cosine) || cosine <= 1e-8) return g; // FaceClass::invalid (keine stille Klammerung!)

    g.beta  = dot(xf - xp, area) / ds;                      // Schnittpunkt Gerade PN mit Flächenebene!
    g.alpha = s2 / ds;                                      // Over-relaxed Leitwert ||S_f||^2 / (d_f . S_f)
    g.skew  = xf - (xp + d * g.beta);                       // Liegt exakt in der Flächenebene (skew . area == 0)
    g.correction    = area - d * g.alpha;                   // Nicht-orthogonaler Korrekturvektor k_f
    g.relative_skew = norm(g.skew) / h_ref;
    g.non_ortho     = norm(g.correction) / std::sqrt(s2);
    g.kind = (g.relative_skew <= skew_tol && g.non_ortho <= non_ortho_tol)
             ? FaceClass::fast : FaceClass::corrected;
    return g;
}

// 2. Aus gather_residual.cu (Zeilen 7–20): Deterministisches, atomikfreies Cell-Gather (nur für owned_cells!)
__global__ void gather_residual(Index owned_cells, CellFaceCSR adjacency,
                                const double* __restrict__ face_flux, double* __restrict__ residual) {
    const Index cell = blockIdx.x * blockDim.x + threadIdx.x;
    if (cell >= owned_cells) return; // Ghost-Zellen (cell >= owned_cells) besitzen keine eigene CSR-Zeile!
    double sum = 0.0;
    for (std::int64_t k = adjacency.offsets[cell]; k < adjacency.offsets[cell + 1]; ++k) {
        sum += adjacency.orientation[k] * face_flux[adjacency.faces[k]]; // orientation in {-1, +1}
    }
    residual[cell] += sum;
}
```

---

### Snippet 13: 4-Quadranten Branchless CUDA FVM-Assemblierung, Kompakte 6%-`MeshCorrectionSoA` & Gradientenfreie Upwind-Deferred-Correction (aus [`SolverGemini4ArgonHighCUDA.md#L17-L225`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverGemini4ArgonHighCUDA.md#L17-L225))
* **Warum wichtig:** Kombiniert das 4-Quadranten-Face-Layout (`Seg 0: Internal Class 0`, `Seg 1: Internal Class 1`, `Seg 2: Halo Class 0`, `Seg 3: Halo Class 1`) mit einer **nur für Seg 1 & Seg 3 (~6 % der Flächen) allokierten `MeshCorrectionSoA`** (spart 94 % Korrekturspeicher über `seg3_corr_offset = b.seg2_end - (b.seg1_end - b.seg0_end)`), strikt getrennten `_x/_y/_z`-Arrays (verhindert 24-Byte-`double3`-Cache-Line-Splits) und **gradientenfreier konvektiver Deferred Correction in Klasse 0** (unbedingte M-Matrix-Diagonaldominanz für beliebige Zell-Péclet-Zahlen ohne Laden von $\nabla\phi$!):
```cpp
// 1. Kompaktes 6%-Offset-Mapping für Segment 1 (Internal Class 1) & Segment 3 (Halo Class 1):
//    In MeshCorrectionSoA liegen Seg 1 [0 .. seg1_count) und Seg 3 [seg1_count .. seg1_count + seg3_count) direkt hintereinander!
int32_t seg1_corr_offset = b.seg0_end;
int32_t seg3_corr_offset = b.seg2_end - (b.seg1_end - b.seg0_end);

// 2. Klasse-0 Kernel mit Upwind-M-Matrix + Gradientenfreier High-Order Deferred Correction (Sec. 2.A & 2.B):
const double m_dot     = fields.m_dot[f];
const double diff_flux = fields.gamma[f] * mesh.alpha[f];
const double m_pos     = fmax(m_dot, 0.0);
const double m_neg     = fmin(m_dot, 0.0);

// Implizite Nebendiagonalen (strikt <= 0 für alle Péclet-Zahlen -> M-Matrix garantiert!):
const double a_PN = m_neg - diff_flux;  // Off-diag (P, N) <= 0
const double a_NP = -m_pos - diff_flux; // Off-diag (N, P) <= 0
sys.val[mesh.csr_off_PN[f]] = a_PN;     // Lock-freier Direkt-Schreibzugriff (keine Binärsuche!)
sys.val[mesh.csr_off_NP[f]] = a_NP;

// Continuity-korrigierter Diagonalbeitrag (a_PP = sum_N |a_PN|, stabil auch bei div(m_dot) != 0 in SIMPLE):
const double a_P_diag = -a_PN;          // = diff_flux - m_neg >= 0
const double a_N_diag = -a_NP;          // = diff_flux + m_pos >= 0

// Explizite konvektive Deferred Correction (nutzt NUR skalare phi_P, phi_N -> lädt KEINE Zellgradienten grad_phi!):
const double phi_P = fields.phi[mesh.owner[f]], phi_N = fields.phi[mesh.neighbor[f]];
const double flux_ho_minus_upw = m_dot * (mesh.beta[f] * phi_N + (1.0 - mesh.beta[f]) * phi_P)
                               - (m_pos * phi_P + m_neg * phi_N);
```

---

### Snippet 14: 2-Stufen MPI/CUDA Halo-Vertrag (`halo_phi` + `halo_grad`) & Tangential-projiziertes Enzyme Seed-Morphing (aus [`halo_schedule.cpp`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverSol61Max/src/snippets/halo_schedule.cpp#L5-L39) & [`MesherAndSolverStep5Preview/src/data/content.ts`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/MesherAndSolverStep5Preview/src/data/content.ts#L411-L426))
* **Warum wichtig:** Behebt zwei subtile Bugs verteilter GPU-Solver und differenzierbarer Mesher:
  1. Für `halo_corrected`-Flächen (`Segment 3`) reicht der Austausch von $\phi$ allein **nicht** aus, da `correction_flux` auch den Gradienten $(\nabla\phi)_N$ der entfernten Ghost-Zelle benötigt ($\implies$ zweistufiger Halo-Austausch `halo_phi` $\to$ Randgradienten $\to$ `halo_grad` mit `cudaEventSynchronize` vor `MPI_Isend`).
  2. Beim Verschieben von Rand-Saatpunkten entlang des LLVM-Enzyme-Adjoint-Gradienten $\nabla_{\mathbf{x}_{\text{seed}}} J$ hält die Tangentialprojektion $(\mathbf{I} - \mathbf{n}\otimes\mathbf{n})\nabla J$ kombiniert mit einer **Newton-Rückprojektion** auf die SDF-Nullkontur $\psi(\mathbf{x}) = 0$ die Knoten exakt auf gekrümmten CAD-Wänden:
```cpp
// Tangentiale CAD-Wandprojektion + 1-Schritt Newton-Rückprojektion auf die gekrümmte Oberfläche psi(x) = 0:
for (int i = 0; i < M.n_boundary_nodes; ++i) {
    const int idx = M.bnode[i];
    const Vec3 n  = M.sdf_gradient(M.seeds[idx]); // Einheitsnormale n = grad(psi) / ||grad(psi)||
    const Vec3 g{seed_d[3*idx], seed_d[3*idx + 1], seed_d[3*idx + 2]};
    const Vec3 g_tan = g - n * dot(g, n);         // 1. Tangentialprojektion (I - n*n^T)*g
    Vec3 x_trial = M.seeds[idx] - eta * g_tan;    // 2. Tangentialer Schritt
    const double psi_val = M.sdf_eval(x_trial);   // 3. Newton-Korrektur gegen Abdriften bei starker Krümmung:
    M.seeds[idx] = x_trial - psi_val * M.sdf_gradient(x_trial);
}
```

---

### Snippet 15: Rhie-Chow PISO Re-Projektion, Norm-skaliertes `FP32`-AMG & Kombinierter $\eta_{\text{round}}$-Detektor (aus [`SolverSIMPLEPISOChatGPT6SolMax/native/include/continuum.hpp#L248-L271`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/continuum.hpp#L248-L271), [`linear.hpp#L252-L289`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/linear.hpp#L252-L289) & [`KimiK31ProMaxSolverFVM.txt#L414-L426`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L414-L426))
* **Warum wichtig:**
  1. Dämpft Schachbrett-Druckoszillationen auf kollokierten Polyedergittern exakt (verschwindet identisch für jedes lineare Druckfeld $p(\mathbf{x}) = a + \mathbf{b}\cdot\mathbf{x}$!) und wertet in mehrstufigen PISO-Schleifen (`pass > 0`) $\nabla p$ und `rhie_chow_faces` vor jedem weiteren Korrekturschritt neu aus.
  2. Skaliert den `FP64`-Defektvektor $\mathbf{r}$ vor dem Cast auf `float` mit $s = \|\mathbf{r}\|_\infty$ (verhindert `FP32`-Underflow bei kleinen Residuen!) und überwacht den Rundungsboden über den normweisen Rückwärtsfehler $\eta_{\text{round}}$ + den Rekurrenz-Drift alle 5 Krylov-Schritte:
```cpp
// 1. Rhie-Chow Massenstrom (continuum.hpp:248–271): Exakt null Korrektur für lineare Druckfelder p(x) = a + b.x
flow.face_mass[f] = rho * (dot(u_beta, geo.s) - d_f * geo.alpha * ((p_N - p_P) - dot(grad_p_beta, geo.d)));

// 2. Underflow-sichere Norm-Skalierung vor dem FP32-AMG-V-Zyklus (linear.hpp:252–263):
double scale = 0.0;
for (double v : r) scale = std::max(scale, std::abs(v));
if (scale == 0.0) return std::vector<double>(r.size(), 0.0);
std::vector<float> b_fp32(r.size()), x_fp32(r.size(), 0.0f);
for (size_t i = 0; i < r.size(); ++i) b_fp32[i] = static_cast<float>(r[i] / scale);
for (int c = 0; c < cycles; ++c) cycle(0, b_fp32, x_fp32);
for (size_t i = 0; i < r.size(); ++i) out[i] = static_cast<double>(x_fp32[i]) * scale;

// 3. A-posteriori Rundungsboden & Stagnations-Detektor (linear.hpp:277–289):
const double denom = a_inf * x_inf + b_inf + 1e-30;
const double eta_round  = r_inf / denom;
const double fp32_floor = (static_cast<double>(std::numeric_limits<float>::epsilon()) * a_inf * x_inf) / denom;
const bool round_limited = stagnated && (eta_round <= 8.0 * fp32_floor);
```

---

### Snippet 16: AoT Sparse-Chemie-Jacobian, Variabel-$c_p(Y)$ Temperatur-Ableitung & CUB / 32-Cell Shared-Memory GPU Work-Stealing (aus [`continuum_cuda.cu#L325-L331`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/cuda/continuum_cuda.cu#L325-L331) & [`KimiK31ProMaxSolverFVM.txt#L1294-L1600`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L1294-L1600))
* **Warum wichtig:** Kombiniert die exakte Quotientenregel für temperatur- und zusammensetzungsabhängige Wärmekapazitäten $c_p(\mathbf{Y})$ in der Energie-Zeile der analytischen Jacobi-Matrix mit CUDA-12 Stream-Ordered Compaction (`cub::DeviceSelect::Flagged` + `cudaMallocAsync`) und einem **32-Zellen Shared-Memory Tile-Stealing-Kernel** (reduziert globale Atomic-Zugriffe auf die Work-Queue um den Faktor 32!):
```cpp
// 1. Exakte Temperatur-Zeile d(dT/dt)/dY_j inkl. variabler Mischungs-Wärmekapazität cp(Y) (KimiK31ProMax:1294–1299):
//    d(Tdot)/dY_j = -(1 / (rho * cp)) * sum_k (d(omega_k)/dY_j * h_k) + (sum_k omega_k * h_k) / (rho * cp^2) * cp_j
for (std::size_t j = 0; j < S; ++j) {
    double d_heat_dYj = 0.0;
    for (std::size_t k = 0; k < S; ++k) d_heat_dYj += J[k * (S + 1) + j] * m.enthalpy[k];
    J[S * (S + 1) + j] = -d_heat_dYj / (rho * cp) + (heat_release * m.cp_species[j]) / (rho * cp * cp);
}

// 2. 32-Zellen Shared-Memory Tile Work-Stealing auf der GPU (KimiK31ProMax:1587–1600):
__shared__ int tile_base, tile_cursor, tile_count;
if (threadIdx.x == 0 && tile_cursor >= tile_count) {
    tile_base = atomicAdd(&queue->cursor, 32); // Nur 1 globaler Atomic pro 32 steife Chemie-Zellen!
    tile_count = min(32, queue->count - tile_base);
    tile_cursor = 0;
}
__syncthreads();
int lane = atomicAdd(&tile_cursor, 1);         // Schneller Shared-Memory-Atomic innerhalb des Blocks
if (lane < tile_count) {
    int cell = active[tile_base + lane];
    ros2_integrate_cell(state + cell * stride, rho[cell], dt);
}
```

---

### Snippet 17: Gekoppelter Druck-Geschwindigkeits-IFT-Adjoint (inkl. Rhie-Chow-Mobilität $\partial D_f/\partial\gamma_i$), Analytische 3D-CAD-Rand-Seeds & Complex-Step-Test (aus [`adjoint.hpp#L115-L166`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/SolverSIMPLEPISOChatGPT6SolMax/native/include/adjoint.hpp#L115-L166), [`KimiK31ProMaxSolverFVM.txt#L1380-L1402`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/KimiK31ProMaxSolverFVM.txt#L1380-L1402) & [`verification.cpp#L424-L491`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Mistral4LargeAdjointSolver/cpp/tests/verification.cpp#L424-L491))
* **Warum wichtig:** Auf kollokierten FVM-Gittern hängt nicht nur der Impuls-Bremsterm $\alpha(\gamma_i)\mathbf{u}_i$ von der SIMP-Designvariable $\gamma_i$ ab, sondern über die Hauptdiagonale $a_P(\gamma_i)$ auch die **Rhie-Chow-Flächenmobilität $D_f(\gamma) = V_P / a_P(\gamma_i)$ in der Kontinuitätsgleichung**! Erst mit diesem Zusatzterm + den geschlossenen 3D-Rand-Seeds $(\partial Q/\partial\mathbf{x}_f, \partial Q/\partial\mathbf{S}_f)$ stimmt der Adjoint auf **10–12 signifikante Stellen** mit dem auslöschungsfreien Complex-Step-Test ($h_{\text{cs}} = 10^{-30}$) überein:
```cpp
// 1. Vollständige SIMP-Brinkman-Sensitivität inkl. Rhie-Chow-Kontinuitäts-Kopplung (KimiK31ProMax:1380–1386):
//    dJ/d(gamma_i) = lambda_{u,i} * V_i * d(alpha)/d(gamma_i) * u_i
//                  + sum_{f in i} (lambda_{p,P} - lambda_{p,N}) * (dD_f / d(gamma_i)) * (p_N - p_P)

// 2. Geschlossene 3D-Rand-Sensitivität nach Flächenschwerpunkt x_f und Flächenvektor S_f (KimiK31ProMax:1395–1402):
inline FaceGeometrySeed boundary_geometry_seed(Vec3 xP, Vec3 xf, Vec3 S, Vec3 u, Vec3 ub,
                                               double p, double mu, double lambda_c, Vec3 lambda_u) {
    const Vec3 e = xP - xf;
    const double eS = dot(e, S), S2 = dot(S, S), g = mu * S2 / eS;
    const double lu = dot(lambda_u, u - ub);
    FaceGeometrySeed d;
    d.S   = (lambda_u * p - lambda_c * ub) + ((lu * mu * 2.0 / eS) * S) - ((lu * g / eS) * e);
    d.x_f = (lu * g / eS) * S; // Wird direkt mit dem Differentiable-CAD-DAG dx_f / d(theta_CAD) kontrahiert!
    return d;
}

// 3. Auslöschungsfreier Complex-Step-Taylor-Check mit h_cs = 1e-30 (Mistral4Large verification.cpp:432–445):
std::vector<std::complex<double>> gamma_c(N);
for (int i = 0; i < N; ++i) gamma_c[i] = std::complex<double>(gamma[i], (i == k) ? 1e-30 : 0.0);
const double dJ_dgamma_exact = solve_complex_objective(gamma_c).imag() / 1e-30; // 0 Subtraktions-Auslöschung!
```

---

### Snippet 18: In-Situ Post-Processing (`Paket 3: CFDFLD03`): Trigonometrischer Cardano-$\lambda_2$-Löser & Topologischer Face-Exit Streamline-Walker (aus [`CAEVisualisation2Unknown.txt#L1212-L1377`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/CAEVisualisation2Unknown.txt#L1212-L1377))
* **Warum wichtig:**
  1. Berechnet das **$Q$-Kriterium** $Q = \frac{1}{2}(\|\boldsymbol{\Omega}\|_F^2 - \|\mathbf{S}\|_F^2)$ und das **$\lambda_2$-Wirbelkriterium** (zweitgrößter Eigenwert von $\mathbf{M} = \mathbf{S}^2 + \boldsymbol{\Omega}^2$) numerisch stabil in geschlossener trigonometrischer Cardano-Form pro Zelle.
  2. Verfolgt RK4-/Cash-Karp-Stromlinien über die **exakte Face-Plane-Austrittszeit** $t_{\text{exit}} = \min_{f\in c,\,\mathbf{u}\cdot\mathbf{S}_f > 0} \frac{(\mathbf{x}_f - \mathbf{x})\cdot\mathbf{S}_f}{\mathbf{u}\cdot\mathbf{S}_f}$ direkt von Nachbarzelle zu Nachbarzelle (**null globale BVH-/Octree-Punktsuchen nach dem Seeding!**):
```cpp
// Topologischer Face-Exit-Walker entlang eines Stromlinien-Segments x0 -> x1 (CAEVisualisation2Unknown.txt:1338–1377):
for (uint32_t f : mesh.cell_faces.row((uint32_t)cell)) {
    const Vec3 Sf   = mesh.outward_area(f, cell);
    const double un = dot(dir, Sf);
    if (un <= 1e-14) continue; // Strahl verlässt die Zelle nicht über Fläche f
    const double t  = dot(mesh.face_centroids[f] - x, Sf) / un;
    if (t > eps && t < t_exit) { t_exit = t; exit_face = static_cast<int32_t>(f); }
}
// Direkt über mesh.face_neighbour_cell(exit_face, cell) in O(1) in die Nachbarzelle wechseln!
```

---

### Snippet 19: Zero-Copy `bytemuck` Binärpuffer-View (`CFDGEO01`/`CFDMSH02`/`CFDFLD03`) & Camera-Relative `f64->(f32_hi, f32_lo)` Reverse-Z (aus [`ChatGPT6LunaMaxCAENoSync/reference/rust/src/buffers.rs#L49-L164`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/buffers.rs#L49-L164) & [`viewport.rs#L17-L50`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/rust/src/viewport.rs#L17-L50))
* **Warum wichtig:** Borgt die gemappten Binärpuffer im Rust-Cockpit ohne eine einzige Heap-Allokation via `bytemuck::try_cast_slice` (mit Overflow-sicherer `checked_mul`/`checked_add`-Arithmetik) und verhindert Z-Fighting auf $\Delta y_1 = 10^{-6}\,\text{m}$ dünnen Grenzschichten durch Camera-Relative `f64`-Subtraktion + unendliche Reverse-Z-Projektion (`Near -> 1.0`, `Infinity -> 0.0`, `CompareFunction::GreaterEqual`):
```rust
// 1. Overflow-sicherer Zero-Copy Slice-Cast (buffers.rs:155–164):
fn cast_array<'a, T: bytemuck::Pod>(bytes: &'a [u8], offset: &mut usize, count: usize) -> Result<&'a [T], BufferError> {
    let byte_count = count.checked_mul(std::mem::size_of::<T>()).ok_or(BufferError::Bounds)?;
    let end = offset.checked_add(byte_count).ok_or(BufferError::Bounds)?;
    let slice = bytes.get(*offset..end).ok_or(BufferError::Bounds)?;
    *offset = end;
    bytemuck::try_cast_slice(slice).map_err(|_| BufferError::Alignment)
}

// 2. WebGPU [0, 1] Infinite Reverse-Z Projektionsmatrix (viewport.rs:39–50):
pub fn reverse_z_infinite_projection(vertical_fov: f32, aspect: f32, near: f32) -> glam::Mat4 {
    let f = 1.0 / (vertical_fov * 0.5).tan();
    glam::Mat4::from_cols(
        glam::Vec4::new(f / aspect, 0.0, 0.0, 0.0),
        glam::Vec4::new(0.0, f, 0.0, 0.0),
        glam::Vec4::new(0.0, 0.0, 0.0, -1.0),
        glam::Vec4::new(0.0, 0.0, near, 0.0),
    )
}
```

---

### Snippet 20: WGSL Compute-Shader für 2D Surface-LIC (`@workgroup_size(8,8,1)`), Screen-Space EDL & Perzeptuelle 1D-Oklab-LUT (aus [`surface_lic.wgsl#L17-L47`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/surface_lic.wgsl#L17-L47), [`mesh_inspect.wgsl#L96-L160`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/ChatGPT6LunaMaxCAENoSync/reference/shaders/mesh_inspect.wgsl#L96-L160) & [`colormaps.ts#L32-L98`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Frontend/Qwen27bCockpitCAENoSync/src/fields/colormaps.ts#L32-L98))
* **Warum wichtig:** Faltet ein hochfrequentes Rauschfeld vorwärts und rückwärts entlang des normierten Wandschubspannungs-Tangentenfeldes $\boldsymbol{\tau}_w / \|\boldsymbol{\tau}_w\|$ mit einem exponentiellen Filterkern $\exp(-s \cdot 0.035)$, um Ablöselinien und Wiederanlegepunkte direkt auf der Bauteiloberfläche mit 120 FPS sichtbar zu machen, während `@interpolate(flat)` im Mesh-Inspector garantiert, dass defekte oder `NaN`-Zellen niemals weichgezeichnet werden:
```wgsl
// Vorwärts-/Rückwärts-Streamline-Faltung für Surface-LIC (surface_lic.wgsl:17–47):
fn integrate_direction(origin: vec2<f32>, direction_sign: f32) -> f32 {
    var cursor = origin; var accumulated = 0.0; var weight_sum = 0.0;
    for (var step = 0u; step < settings.steps; step += 1u) {
        let shear = sample_shear(vec2<i32>(cursor));
        let mag = length(shear);
        if (mag < 1e-8) { break; }
        cursor += (shear / mag * direction_sign) * settings.step_length;
        let noise = textureLoad(noise_texture, bounded_pixel(vec2<i32>(cursor * settings.noise_scale)), 0).r;
        let kernel = exp(-f32(step) * settings.step_length * 0.035);
        accumulated += noise * kernel; weight_sum += kernel;
    }
    return select(0.0, accumulated / weight_sum, weight_sum > 0.0);
}
```

---

### Snippet 21: $4\times 4$ Block-Coupled $(\mathbf{u}, p)$ FVM-Assembler für MRF-Coriolis, Moving Walls, Rodrigues-Cyclic & Direct Sub-Face AMI (aus [`coupledMFRSteadyIncFoam-Final.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/coupledMFRSteadyIncFoam-Final.pdf), Mangani, Buchmayr & Darwish 2014, Gl. 5–19)
* **Warum wichtig:** Löst die vier größten Konvergenzkiller segregierter Turbomaschinen-Solver (`simpleFoam`) in einer einzigen $4\times 4$-Blockmatrix:
  1. **Schiefsymmetrischer MRF-Coriolis-Block ($\mathbf{\Omega}\times\mathbf{u}$, §3.1)** direkt auf den 6 Nebendiagonalen von $\mathbf{A}_C^{uu}$ (ohne Zentrifugal-Quellterm dank Absolutgeschwindigkeit $\mathbf{u}$ mit Relativfluss $\dot{V}_r = \dot{V} - (\mathbf{\Omega}\times\mathbf{r}_f)\cdot\mathbf{S}_f$, Gl. 4–6).
  2. **Implizite Tangential-Projektion $\mathbf{I} - \mathbf{n}_b\otimes\mathbf{n}_b$ an rotierenden/bewegten Wänden (Gl. 14–15)**.
  3. **$4\times 4$ Rodrigues-Rotation $\mathbf{A}_{NB} = \mathbf{A}_{NB,\text{geom}}\cdot\mathbf{T}_{NB}$ (Gl. 16–17)** für rotationsperiodische `cyclic`-Patches.
  4. **Direkte Sub-Face-AMI-Kopplung mit Flächengewichten $w_{f_k}$ (Gl. 18–19)** ohne virtuelle Schatten-Zellen ($\implies 6.1\times\text{ bis }35.1\times$ schneller als SIMPLE!):
```cpp
struct alignas(64) Block4x4 { double m[4][4]{}; }; // [0=u, 1=v, 2=w, 3=p]

// 1. Impliziter schiefsymmetrischer MRF-Coriolis-Block (\Omega x u) auf dem Zell-Diagonalblock A_C (§3.1):
inline void add_mrf_coriolis_block(Block4x4& A_C, const double omega[3], double V_C) noexcept {
    const double ox = omega[0] * V_C, oy = omega[1] * V_C, oz = omega[2] * V_C;
    A_C.m[0][1] -= oz; A_C.m[0][2] += oy;
    A_C.m[1][0] += oz; A_C.m[1][2] -= ox;
    A_C.m[2][0] -= oy; A_C.m[2][1] += ox;
}

// 2. Implizite Tangential-Wandschubspannungs-Projektion P = I - n_b (x) n_b an bewegter/rotierender Wand (Gl. 14–15):
inline void add_moving_wall_shear_block(Block4x4& A_C, double b_C[4], double nu_eff,
                                        const double S_b[3], double d_perp, const double u_wall[3]) noexcept {
    const double area = std::sqrt(S_b[0]*S_b[0] + S_b[1]*S_b[1] + S_b[2]*S_b[2]);
    const double n[3] = {S_b[0] / area, S_b[1] / area, S_b[2] / area};
    const double c = (nu_eff * area) / d_perp;
    for (int i = 0; i < 3; ++i) {
        double u_par_i = 0.0;
        for (int j = 0; j < 3; ++j) {
            const double P_ij = (i == j ? 1.0 : 0.0) - n[i] * n[j];
            A_C.m[i][j] += c * P_ij;
            u_par_i     += P_ij * u_wall[j];
        }
        b_C[i] += c * u_par_i;
    }
}

// 3. Direkte implizite Sub-Face-AMI / Interne Face-Assemblierung mit Überlappungsgewicht w_f (Gl. 9, 18–19):
//    Für normale interne Faces ist w_f = 1.0; für AMI-Subfaces ist w_f = w_{f_k} (mit sum_k w_{f_k} == 1.0).
inline void assemble_coupled_subface(Block4x4& A_C, Block4x4& A_NB, double b_C[4],
                                     double w_f, double Vdot_rel, double nu_eff, double rho, double g_f,
                                     const double S_f[3], const double d[3], const double D_rc[3], const double grad_p[3]) noexcept {
    const double wf_V = w_f * Vdot_rel;
    const double dS   = d[0]*S_f[0] + d[1]*S_f[1] + d[2]*S_f[2];
    const double S2   = S_f[0]*S_f[0] + S_f[1]*S_f[1] + S_f[2]*S_f[2];
    const double c_C  = std::max(wf_V, 0.0), c_NB = -std::max(-wf_V, 0.0), diff = w_f * nu_eff * (S2 / dS);
    for (int i = 0; i < 3; ++i) {
        A_C.m[i][i]  += c_C  + diff;                A_NB.m[i][i] += c_NB - diff;
        A_C.m[i][3]  += w_f * (1.0 - g_f) * S_f[i] / rho; A_NB.m[i][3] += w_f * g_f * S_f[i] / rho; // A^{up}
        A_C.m[3][i]  += w_f * (1.0 - g_f) * S_f[i];       A_NB.m[3][i] += w_f * g_f * S_f[i];       // A^{pu}
    }
    const double a_pp = w_f * (S_f[0]*D_rc[0]*S_f[0] + S_f[1]*D_rc[1]*S_f[1] + S_f[2]*D_rc[2]*S_f[2]) / dS;
    A_C.m[3][3] += a_pp; A_NB.m[3][3] -= a_pp; // Rhie-Chow A^{pp} (Gl. 19)
    for (int i = 0; i < 3; ++i) b_C[3] += (w_f * S_f[i] * D_rc[i] - a_pp * d[i]) * grad_p[i]; // N_k . grad_p (Gl. 18)
}
```

---

### Snippet 22: Block-Dreiecks-PCD-Schur-Vorkonditionierer ($\rho_2/\rho_1 = \mu_2/\mu_1 = 10^6$) & Direkte STEP-NURBS-Kontrollpunkt-Adjoint-Kontraktion (aus [`Coupled_Solver_1-s2.0-S0045793023002207-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/Coupled_Solver_1-s2.0-S0045793023002207-main.pdf), El Ouafa et al. 2023, Gl. 19–21, Alg. 2 & [`CAD_Optimization_1-s2.0-S0045793011000508-main.pdf`](file:///G:/Datein/Projects/CFDSolver/resources/papers/CAD_Optimization_1-s2.0-S0045793011000508-main.pdf), Yu & Müller 2011, Gl. 7–11)
* **Warum wichtig:**
  1. **El Ouafa et al. (2023, Alg. 2):** Vorkonditioniert das monolithische Sattelpunktsystem über das zweiphasige *Pressure Convection-Diffusion (PCD)*-Schur-Komplement $\hat{S}_{PCD}^{-1} = (M_p^{(1/\mu)})^{-1} + (A_p^{(1/\rho)})^{-1}(N_p^{(1)} + \Delta t^{-1}M_p^{(1)})(M_p^{(1)})^{-1}$ und eine **Ordnung-0 Block-Gauss-Seidel-Rückwärtseinsetzung** auf $(F_{ww}, F_{vv}, F_{uu})$ mit insgesamt nur **4 skalaren AMG-V-Zyklen pro Aufruf** (skaliert bis 1,1 Mrd. Zellen bei Dichte-/Viskositätssprüngen von $10^6$).
  2. **Yu & Müller (2011, Gl. 7–11):** Kontrahiert für beliebige importierte ISO-STEP-NURBS-Flächen die FVM-Oberflächensensitivität $\frac{\mathrm{d}L}{\mathrm{d}\mathbf{X}_B}$ über $\frac{\partial\mathbf{X}_s(u,v)}{\partial\mathbf{P}_{i,j}} = R_{i,j}(u,v)\mathbf{I}_3$ lokal im aktiven Knotenintervall $(\text{span}_u, \text{span}_v)$ in $\mathcal{O}(pq)$ (**0,24 s statt 3 h CATIA-FD, 0 künstliche Gradientenglättung!**):
```cpp
// 1. Zweiphasiges PCD Schur-Komplement S_PCD^-1 * z_p (El Ouafa et al. 2023, Gl. 20–21 & Alg. 2 Zeilen 2–7):
for (size_t i = 0; i < N; ++i) {
    rp_visc[i] = (mu_eff[i] / vol_p[i]) * z_p[i]; // (M_p^{(1/mu)})^{-1} * z_p  (Stokes-/Hochviskos-Zweig)
    rp_mass[i] = (1.0       / vol_p[i]) * z_p[i]; // (M_p^{(1)})^{-1} * z_p
}
apply_pressure_convection_Np(rp_mass.data(), rp_conv.data()); // N_p^{(1)} * rp_mass
for (size_t i = 0; i < N; ++i) rp_conv[i] += dt_inv * z_p[i]; // + dt^{-1} * M_p^{(1)} * (M_p^{(1)})^{-1} * z_p
amg_Ap_inv_rho.v_cycle(rp_conv.data(), rp_inert.data());      // 1 AMG-V-Zyklus auf div(rho^{-1} grad phi)
for (size_t i = 0; i < N; ++i) r_p[i] = rp_inert[i] + rp_visc[i];
// Danach: z_u -= B_p^T * r_p und 3 skalare AMG-V-Zyklen rückwärts auf F_ww -> F_vv -> F_uu (Block-Gauss-Seidel)!

// 2. Lokale STEP-NURBS-Kontrollpunkt-Adjoint-Kontraktion dL/dP_{i,j} += R_{i,j}(u,v) * dL/dX_B (Yu & Müller Gl. 7–11):
for (int iu = 0; iu <= p; ++iu) {
    const int i = span_u - p + iu;
    for (int iv = 0; iv <= q; ++iv) {
        const int idx = i * n_ctrl_v + (span_v - q + iv);
        const double R_ij = (Nu[iu] * Nv[iv] * W[idx]) / W_sum; // Rationale NURBS-Basis (Gl. 8)
        dL_dP[idx].x += R_ij * dL_dXB.x;
        dL_dP[idx].y += R_ij * dL_dXB.y;
        dL_dP[idx].z += R_ij * dL_dXB.z;
    }
}
```

---

### Snippet 23: Mimetischer Polyeder-Hodge-Stern $\mathbf{M}_P$ (MFD/VEM) & Port-Hamiltonscher Dirac-Kopplungsvertrag (aus [`literature_survey_2026.md` §10.1–§10.2](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L634))
* **Warum wichtig:**
  1. **Mimetische Polyeder-Stabilisierung (Brezzi, Buffa & Lipnikov):** Liefert für jede noch so stark verzerrte Cut-Cell oder Mosaic-Polyederzelle $P$ ($m$ Flächen) rein aus den vorhandenen Größen $(\mathbf{S}_{P,f}, \mathbf{x}_f - \mathbf{x}_P, V_P)$ eine **garantiert symmetrisch positiv definite ($m\times m$)-Leitwertmatrix $\mathbf{M}_P \succ 0$**, die für jedes lineare Feld exakt ist ($\mathbf{M}_P \mathbf{R}_P = \mathbf{N}_P \mathbf{K}_P$).
  2. **Port-Hamiltonscher Dirac-Port $(e, f)$:** Erzwingt an jeder Modulgrenze (FSI, CHT, Batterie, MHD) leistungskonjugierte Paare ($\dim(e\cdot f) = [\text{W}] = [1,2,-3,0,0,0,0]$) mit $e_A = e_B, f_A + f_B = 0 \implies \frac{\mathrm{d}\mathcal{H}_{\text{tot}}}{\mathrm{d}t} \le 0$ (unbedingte Kopplungs-Stabilität!):
```cpp
// 1. Mimetischer lokaler (m x m)-Leitwert-Eintrag M_P(f1, f2) für verzerrte Klasse-1-Polyeder (Lipnikov et al. 2014):
//    N_P[f] = S_{P,f} (äußerer Flächenvektor), R_P[f] = x_f - x_P, G = (R_P^T * R_P)^{-1} in R^{3x3}:
inline double mimetic_polyhedral_conductance_entry(
    int f1, int f2, double V_P, double kappa_iso,
    const Vec3* N_P, const Vec3* R_P, const Mat3& RtR_inv) noexcept
{
    const double consistency = (kappa_iso / V_P) * dot(N_P[f1], N_P[f2]); // (1/V_P) * N_P * K_P * N_P^T
    const double gamma_P     = (3.0 * kappa_iso) / std::cbrt(V_P);        // tr(K_P) / V_P^{1/3}
    const double proj_f1_f2  = (f1 == f2 ? 1.0 : 0.0) - dot(R_P[f1], RtR_inv * R_P[f2]);
    return consistency + gamma_P * proj_f1_f2; // Garantiert SPD (M_P > 0) und exakt für lineare Felder!
}

// 2. Port-Hamiltonscher Dirac-Kopplungs-Check (van der Schaft & Kotyczka):
//    Prüft, dass Effort e und Flow f exakt die SI-Dimension Leistung [W] = [1, 2, -3, 0, 0, 0, 0] ergeben
//    und die verlustfreie Dirac-Bilanz e_A * f_A + e_B * f_B == 0 (+ Dämpfung R_diss >= 0) erfüllen:
constexpr bool verify_port_hamiltonian_power_pair(const int8_t dim_effort[7], const int8_t dim_flow[7]) noexcept {
    constexpr int8_t kWattDim[7] = {1, 2, -3, 0, 0, 0, 0}; // [kg * m^2 * s^-3] = [W]
    for (int i = 0; i < 7; ++i) {
        if (dim_effort[i] + dim_flow[i] != kWattDim[i]) return false;
    }
    return true;
}
```

---

### Snippet 24: Anderson Acceleration `AA(m)` für SIMPLE/CHT & Unbedingt positives Modified Patankar-Chemie-Update (MPRK) (aus [`literature_survey_2026.md` §10.5](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L675))
* **Warum wichtig:**
  1. **Anderson Acceleration `AA(m)` (Walker & Ni 2011; Pollock & Rebholz 2019):** Beschleunigt segregierte Fixpunkt-Schleifen (`SIMPLE`, partitioniertes `CHT`/`FSI`) durch eine winzige $m\times m$ Least-Squares-Extrapolation ($m \approx 5$) der letzten Residuen $\mathbf{f}_k = \mathcal{G}(\mathbf{x}_k) - \mathbf{x}_k$ um **Faktor $2.5\times\text{ bis }4\times$** komplett ohne Änderung der FVM-Matrixassemblierung.
  2. **Modified Patankar (Burchard, Deleersnijder & Meister):** Gewichtet in steifen Produktions-Destruktions-Systemen $\dot{Y}_k = \sum_j p_{kj} - \sum_j d_{kj}$ ($p_{kj} = d_{jk} \ge 0$) die Terme implizit mit $\frac{Y_j^{n+1}}{Y_j^n}$ und garantiert damit **strikte Positivität ($Y_k^{n+1} > 0$) und exakte Massenerhaltung ($\sum_k Y_k^{n+1} \equiv \sum_k Y_k^n = 1$) für jedes $\Delta t > 0$** ohne künstliches Renormierungs-Clipping:
```cpp
// 1. Modified Patankar-Euler Matrix-Assemblierung M * Y^{n+1} = Y^n (Burchard et al., strikte M-Matrix!):
//    Für p_{kj} >= 0 (Produktion von k aus j) und d_{kj} = p_{jk} >= 0 (Destruktion von k nach j):
inline void assemble_modified_patankar_system(
    int Ns, double dt, const double* Y_n, const double* p_matrix, double* M_out) noexcept
{
    for (int k = 0; k < Ns; ++k) {
        double diag_sum = 1.0;
        const double inv_Yk = 1.0 / std::max(Y_n[k], 1e-300);
        for (int j = 0; j < Ns; ++j) {
            if (j == k) continue;
            const double d_kj = p_matrix[j * Ns + k]; // Destruktion von k -> Produktion von j
            const double p_kj = p_matrix[k * Ns + j]; // Produktion von k <- Destruktion von j
            diag_sum          += dt * d_kj * inv_Yk;
            M_out[k * Ns + j]  = -dt * p_kj / std::max(Y_n[j], 1e-300); // Off-Diag <= 0
        }
        M_out[k * Ns + k] = diag_sum; // Spaltensumme jeder Spalte j ist exakt 1.0 ==> sum_k Y_k^{n+1} == sum_k Y_k^n!
    }
}
```

---

### Snippet 25: Stetige quadratische Prismen-Jacobi-Schranke $\min_{t\in[0,1]}\det\mathbf{J}(t)$, Pyramiden-Basis-Faltungstest & Exakte Dreiecks-Abwicklungs-Geodäte im `FaMesh` (aus [`BoundaryLayerMeshing1GPTAstra62max/engine/geometry.hpp#L185-L204`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/geometry.hpp#L185-L204), [`fa_mesh.hpp#L54-L70`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing1GPTAstra62max/engine/fa_mesh.hpp#L54-L70) & [`BoundaryLayerMeshing2MistralLarge4high/mesher/quality.h#L51-L64`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/BoundaryLayerMeshing2MistralLarge4high/mesher/quality.h#L51-L64))
* **Warum wichtig:**
  1. **Stetige Prismen-Schranke (`GPTAstra62max`):** Ein verdrilltes Grenzschicht-Prisma kann an Boden ($t=0$) und Deckel ($t=1$) $\det\mathbf{J} > 0$ besitzen und trotzdem bei $t^* = -B/(2A) \in (0,1)$ invertieren! Die exakte Auswertung des quadratischen Polynoms $\det\mathbf{J}_i(t) = At^2 + Bt + C$ schließt jede innere Selbstüberschneidung analytisch aus.
  2. **Exakte Geodäten-Abwicklung (`GPTAstra62max`):** Klappt benachbarte Wanddreiecke um ihre gemeinsame Kante $\mathbf{t}$ in eine Ebene auf und berechnet exakte geodätische Abstände und Dihedral-Krümmungstensoren für den 2D-Wandfilm (`faMesh`).
```cpp
// 1. Stetige quadratische Prismen-Jacobi-Schranke über die gesamte Schichtdicke t in [0, 1] (geometry.hpp:185-204):
inline double prismJacobianBound(const Points &p, const Ids &v) {
    Vec a = p[v[1]] - p[v[0]], b = p[v[2]] - p[v[0]], at = p[v[4]] - p[v[3]], bt = p[v[5]] - p[v[3]];
    double result = 1.0;
    for (int i = 0; i < 3; i++) {
        Vec d = p[v[i + 3]] - p[v[i]], da = at - a, db = bt - b;
        double A = det(da, db, d), B = det(da, b, d) + det(a, db, d), C = det(a, b, d);
        double m = std::min(C, A + B + C);
        if (A > 0.0) {
            double t = -B / (2.0 * A);
            if (t > 0.0 && t < 1.0) m = std::min(m, (A * t + B) * t + C); // Inneres Minimum t* in (0,1)!
        }
        double denom = std::max(a.norm(), at.norm()) * std::max(b.norm(), bt.norm()) * d.norm();
        result = std::min(result, denom > 0.0 ? m / denom : -1.0);
    }
    return result;
}

// 2. Exakte Dreiecks-Abwicklungs-Geodäte & symmetrischer Dihedral-Krümmungstensor im FaMesh (fa_mesh.hpp:54-70):
Vec v = p[ed.b] - p[ed.a], t = v.unit();
Vec a = f.c - p[ed.a], b = g.c - p[ed.a];
double u = a.dot(t), w = b.dot(t);
double h = (a - t * u).norm(), k = (b - t * w).norm(), crossing = (u * k + w * h) / (h + k);
ed.distance = (crossing >= 0.0 && crossing <= v.norm())
    ? std::hypot(u - w, h + k)
    : std::min(a.norm() + b.norm(), (f.c - p[ed.b]).norm() + (g.c - p[ed.b]).norm());
double theta = std::atan2(t.dot(f.n.cross(g.n)), f.n.dot(g.n));
Vec m = ed.le.unit();
for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
        f.curvature[3 * r + c] += 0.5 * v.norm() * theta * m[r] * m[c] / f.area;
```

---

### Snippet 26: Implizite WLS-Gradienten-Einbettung in die FVM-Systemmatrix $\mathbf{A}$ (Konvergenz in 1 Solve ohne Picard-Lagging!) & Skilling 3D-Hilbert + AMIPS (aus [`FVMVerificationEngine1Inkling2Small/engine/assembly.h#L206-L235`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Mesher/FVMVerificationEngine1Inkling2Small/engine/assembly.h#L206-L235) & [`Fable52high.txt`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Fable52high.txt))
* **Warum wichtig:**
  1. **Implizite WLS-Einbettung (`Inkling2Small`):** Drückt den WLS-Zellgradienten als exakte Linearkombination $(\nabla\phi)_c = \sum_j \mathbf{L}_{c,j}\phi_j + \mathbf{t}_c$ aus und assembliert die Skewness- und Nicht-Orthogonalitäts-Korrektur $\mathbf{c}_f \cdot \frac{1}{2}(\mathbf{L}_{P,j} + \mathbf{L}_{N,j})$ mit $\mathbf{c}_f = \dot{m}_f\mathbf{m}_f^{\text{skew}} - \Gamma\mathbf{k}_f^{\text{corr}}$ **direkt in die Matrix $\mathbf{A}$**. Dadurch konvergiert das System selbst bei $47.7^\circ$ Nicht-Orthogonalität in **einem einzigen linearen Solve ohne Picard-Schleife** ($p_{\text{obs}} = 1.9586$) und besitzt eine exakte Transponierte $\mathbf{A}^T$ für den Diskreten Adjoint ($1.14\times 10^{-14}$ Fehler gegen Complex-Step!).
  2. **AMIPS Tetraeder-Qualitätsenergie (`Fable52high.txt:11080-11099`):** $\text{AMIPS}(\mathbf{J}) = \operatorname{tr}(\mathbf{J}^T\mathbf{J})/(\det\mathbf{J})^{2/3} \ge 3$ für den Crash-Flight-Recorder:
```cpp
// 1. Implizite Einbettung der WLS-Gradientengewichte L_{P,j}, L_{N,j} für m_skew und k_corr in Matrix A (assembly.h:206-235):
const T aPP = m * (T)(1.0 - beta) + gamma * (T)alpha;
const T aPN = m * (T)beta - gamma * (T)alpha;
const std::array<T, 3> cvec{m * (T)mSkew.x - gamma * (T)kCorr.x,
                            m * (T)mSkew.y - gamma * (T)kCorr.y,
                            m * (T)mSkew.z - gamma * (T)kCorr.z};
for (int j : neighbor_stencil_union_PN) {
    std::array<double, 3> lp{0, 0, 0}, ln{0, 0, 0};
    bool hp = false, hn = false;
    LvecOf<T>(G, P, j, lp, hp);
    LvecOf<T>(G, N, j, ln, hn);
    const T corr = cvec[0] * (T)(0.5 * (lp[0] + ln[0]))
                 + cvec[1] * (T)(0.5 * (lp[1] + ln[1]))
                 + cvec[2] * (T)(0.5 * (lp[2] + ln[2]));
    const T valP = (j == P ? aPP : T(0)) + (j == N ? aPN : T(0)) + corr;
    add(P, j,  valP);
    add(N, j, -valP);
}
```

---

### Snippet 27: Geschlossene quadratische Peng-Robinson-EOS in $w = \sqrt{T}$, All-Mach Low-Mach HLLC & OPMIS Wendland-$C^2$ MLS-Transfer (aus [`MultiphaseCompressible1PGT62SolMax/native/physics.hpp#L44-L93`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/physics.hpp#L44-L93), [`#L200-L235`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/physics.hpp#L200-L235) & [`hybrid.hpp#L50-L92`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphaseCompressible1PGT62SolMax/native/hybrid.hpp#L50-L92))
* **Warum wichtig:**
  1. **Exakte Newton-freie Peng-Robinson-Inversion (`GPT62SolMax`):** Da im Peng-Robinson-Real-Gas-Modell sowohl $p(v, w)$ als auch die innere Energie $e(v, w)$ **exakte quadratische Funktionen in $w = \sqrt{T}$** sind, löst `positive_root` sowohl `from_p(rho, p)` als auch `from_e(rho, e)` geschlossen in $\mathcal{O}(1)$ auf $2.22\times 10^{-16}$ genau!
  2. **All-Mach Low-Mach-vorkonditioniertes HLLC:** Skaliert nur den akustischen Geschwindigkeitssprung im Stern-Druck über $\Delta p^* = (\beta - 1)\frac{\alpha_L\alpha_R}{\alpha_L+\alpha_R}(v_L - v_R)$ mit Schocksensor $\beta = \max(\text{Ma}_{\text{loc}}, \text{sensor}) \in [10^{-3}, 1]$ (99,76 % Akustikpuls-Erhaltung + exakte Sod-Stoßfronten!).
```cpp
// 1. Geschlossene quadratische Peng-Robinson-Inversion in w = sqrt(T) via Citardauq-Formel (physics.hpp:55-93):
CB_HD inline double positive_root(double a, double b, double c) {
    const double d = b * b - 4.0 * a * c;
    if (!(d >= 0.0) || !(a > 0.0)) return -1.0;
    const double root = ::sqrt(d);
    return (b >= 0.0) ? (b + root > 0.0 ? -2.0 * c / (b + root) : -1.0) : (-b + root) / (2.0 * a);
}
// Inversion aus (rho, p):
// w = positive_root(t.R/(t.v - t.b) - sq(t.B)/t.den, 2*t.A*t.B/t.den, -sq(t.A)/t.den - p);
// Inversion aus (rho, e):
// w = positive_root(t.cv, t.A*t.B*t.I, -sq(t.A)*t.I - e);

// 2. All-Mach Low-Mach-Korrektur im HLLC-Riemann-Löser (physics.hpp:226-232):
if (scheme == Scheme::AllMach) {
    const double sensor = clip(6.0 * ::fabs(l.p - r.p) / min(l.rho * sq(l.c), r.rho * sq(r.c)), 0.0, 1.0);
    const double beta   = max(beta_hllc(l, r, floor), sensor);
    const double dp     = (beta - 1.0) * (al * ar / (al + ar)) * (vl - vr);
    f.m += n * dp;
    f.E += sm * dp;
}
```

---

### Snippet 28: Exakte 3D Swept-Volume Simpson-BDF2-DGCL-Quadratur & 4-Stufen Hatchard-Spotnitz Thermal-Runaway mit SEI-Tunnelbarriere $t_{\text{SEI}}$ (aus [`MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L510-L561`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules2GPT61SolMax/public/solver/cluster_c.cpp#L510-L561) & [`MultiphysicsModules1GeminiArgon4High/cpp/cluster_c_multiphysics.cpp#L419-L476`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/MultiphysicsModules1GeminiArgon4High/cpp/cluster_c_multiphysics.cpp#L419-L476))
* **Warum wichtig:**
  1. **Exaktes 3D-Swept-Volume für rotierende/deformierende Polyederflächen (`GPT61SolMax`):** Bei linearer Knotenbewegung über $[t^n, t^{n+1}]$ ist der Dreiecks-Flächenvektor $\mathbf{S}(\xi)$ quadratisch in $\xi\in[0,1]$, sodass die **Simpson-Regel $\frac{\mathbf{S}_0 + 4\mathbf{S}_{1/2} + \mathbf{S}_1}{6}$ das überstrichene Volumen auf Maschinenpräzision ($1.21\times 10^{-17}\,\text{m}^3/\text{s}$) exakt integriert**!
  2. **4-Stufen Hatchard-Spotnitz Batterie-Thermal-Runaway (`GeminiArgon4High`):** Beinhaltet in Stufe 2 (Anode-Elektrolyt) die physikalische **SEI-Tunnelbarriere $\exp(-t_{\text{SEI}}/t_{\text{SEI},0})$** mit wachsender Schichtdicke $t_{\text{SEI}}^{n+1} = t_{\text{SEI}}^n + \Delta t R_{\text{ne}}$ und die autokatalytische Kathoden-Slack-Variable $1 - \alpha_{\text{pe}}$:
```cpp
// 1. Exakte 3D Swept-Volume Simpson-Quadratur für ein bewegtes Flächendreieck (a,b,c) -> (A,B,C) (cluster_c.cpp:510-517):
inline double triangle_sweep(Vec a, Vec b, Vec c, Vec A, Vec B, Vec C) noexcept {
    const Vec velocity_integral = ((A - a) + (B - b) + (C - c)) / 3.0;
    const Vec S0 = cross(b - a, c - a) * 0.5;
    const Vec S1 = cross(B - A, C - A) * 0.5;
    const Vec Sm = cross((b + B - a - A) * 0.5, (c + C - a - A) * 0.5) * 0.5;
    return dot(velocity_integral, (S0 + Sm * 4.0 + S1) / 6.0); // Exakt für beliebige 3D-Rotation & Streckung!
}

// 2. Stufe 2 (Anode mit SEI-Tunneling t_SEI) & Stufe 3 (Autokatalytische Kathode via 1 - alpha_pe) (cluster_c_multiphysics.cpp:427-444):
const double t_sei_old = std::max(p_.t_SEI_0, state.t_SEI[c]);
const double k_ne      = p_.A_ne * std::exp(-p_.E_ne / (p_.R_u * Tc)) * std::exp(-t_sei_old / p_.t_SEI_0);
const double c_ne_new  = state.c_ne[c] / (1.0 + dt * k_ne);
state.t_SEI[c]         = t_sei_old + dt * ((state.c_ne[c] - c_ne_new) / dt);

const double a_old           = std::clamp(state.alpha_pe[c], 1e-6, 1.0 - 1e-9);
const double prod_rate       = k_pe * std::pow(a_old, p_.m_pe1) * std::pow(1.0 - a_old, p_.m_pe2);
const double one_minus_a_new = (1.0 - a_old) / (1.0 + dt * prod_rate / std::max(1e-12, 1.0 - a_old));
state.alpha_pe[c]            = std::clamp(1.0 - one_minus_a_new, a_old, 1.0);
```

---

### Snippet 29: Analytische Rang-1 Forchheimer-Jacobi-Matrix, Aitken-$\Delta^2$ Robin-Robin CHT & $S_8/S_{16}$ fvDOM-Strahlung mit Spiegel-Reflexion (aus [`Turbulence3GPT6LunaMax/cpp/include/cluster_a/cluster_a.hpp#L489-L511`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence3GPT6LunaMax/cpp/include/cluster_a/cluster_a.hpp#L489-L511), [`Turbulence2ClaudeHaiku55med/cpp/cluster_a.hpp#L429-L462`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2ClaudeHaiku55med/cpp/cluster_a.hpp#L429-L462) & [`Turbulence2GLM53max/src/cpp/cluster_a_radiation.hpp#L161-L274`](file:///G:/Datein/Projects/CFDSolver/resources/reference_spickzettel/LMArena/Solver/Turbulence2GLM53max/src/cpp/cluster_a_radiation.hpp#L161-L274))
* **Warum wichtig:**
  1. **Exakte Fréchet-Ableitung der anisotropen Forchheimer-Senke (`Turbulence3GPT6LunaMax`):** Liefert für unseren $4\times 4$ Block-Coupled Solver neben $\mu\mathbf{D} + \frac{1}{2}\rho|\mathbf{u}|\mathbf{F}$ auch den exakten Rang-1-Tangentenbeitrag $\frac{1}{2}\rho\frac{(\mathbf{F}\mathbf{u})\otimes\mathbf{u}}{|\mathbf{u}|}$.
  2. **Aitken-$\Delta^2$ Robin-Robin CHT (`Turbulence2ClaudeHaiku55med`):** Konvergiert partitioniertes Fluid-Solid-CHT über $w_k = -w_{k-1}\frac{r_{k-1}}{r_k - r_{k-1}}$ in **3 Iterationen auf $8.79\times 10^{-11}\,\text{K}$** an die monolithische Lösung!
```cpp
// 1. Exakte 3x3 Tangenten-Jacobi-Matrix der anisotropen Darcy-Forchheimer-Senke (cluster_a.hpp:491-510):
const double speed = norm(velocity);
const Vec3   Fu    = matVec(forchheimerTensor, velocity);
for (std::size_t i = 0; i < 3; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
        implicitJacobian[i][j] = mu * darcyTensor[i][j]
            + 0.5 * rho * (speed * forchheimerTensor[i][j] + (speed > 1e-12 ? Fu[i] * velocity[j] / speed : 0.0));
    }
}

// 2. Aitken-Delta^2 dynamische Relaxationsbeschleunigung für partitioniertes Robin-Robin-CHT (cluster_a.hpp:457-460):
const double r = T_solid_interface_new - x_trace;
if (it > 0 && std::abs(r - rPrev) > 1e-300) {
    w_aitken = -w_aitken * rPrev / (r - rPrev);
}
rPrev    = r;
x_trace += w_aitken * r;
```

---

### Snippet 30: Unterrelaxations- & Zeitschritt-Konsistentes Rhie-Chow (Moukalled/Mangani/Darwish) & Zweistufiger `CPR` (Constrained Pressure Residual) Block-Vorkonditionierer (aus [`literature_survey_2026.md` §9.23](file:///G:/Datein/Projects/CFDSolver/resources/papers/literature_survey_2026.md#L628))
* **Warum wichtig:**
  1. **Konsistente Rhie-Chow-Flächengeschwindigkeit (Moukalled, Mangani & Darwish 2016, Kap. 15.6 & 18):** Verhindert, dass der konvergierte stationäre Zustand in gekoppelten oder segregierten Lösern vom Unterrelaxationsfaktor $\alpha_u$ oder vom Zeitschritt $\Delta t$ abhängt.
  2. **Zweistufiger `CPR`-Vorkonditionierer für Reaktive & Multiphysik-Systeme ($M = 10\text{–}50$ DOFs/Zelle):** Kombiniert innerhalb von Matrix-Free Enzyme-JFNK (`FGMRES`) **1 skalaren AMG-V-Zyklus** auf der Quasi-IMPES-Druckmatrix $\mathbf{A}_{pp}^{\text{CPR}} = \mathbf{R}_p^T\mathcal{J}\mathbf{P}_p$ mit **1 hochparallelen zelllokalen Block-Jacobi/BILU(0)-Sweep** auf allen Transport- und Reaktionsgleichungen:
```cpp
// 1. Unterrelaxations- und Zeitschritt-konsistenter Rhie-Chow-Flächenfluss (Moukalled, Mangani & Darwish Kap. 15.6 & 18):
inline Vec3 consistent_rhie_chow_face_velocity(
    Vec3 u_bar_k, Vec3 u_f_km1, Vec3 u_bar_km1, Vec3 u_f_old_n, Vec3 u_bar_old_n,
    Vec3 D_bar_f, Vec3 grad_p_f, Vec3 grad_p_bar_f,
    double alpha_u, double rho_f, double V_f, double dt, double a_P_bar_f) noexcept
{
    const Vec3 dp_diss    = {D_bar_f.x * (grad_p_f.x - grad_p_bar_f.x),
                             D_bar_f.y * (grad_p_f.y - grad_p_bar_f.y),
                             D_bar_f.z * (grad_p_f.z - grad_p_bar_f.z)};
    const Vec3 urf_corr   = (1.0 - alpha_u) * (u_f_km1 - u_bar_km1);
    const Vec3 dt_corr    = ((rho_f * V_f) / std::max(dt * a_P_bar_f, 1e-300)) * (u_f_old_n - u_bar_old_n);
    return u_bar_k - dp_diss + urf_corr + dt_corr; // Im Fixpunkt u^{(k)} == u^{(k-1)} == u^n exakt unabhängig von alpha_u & dt!
}

// 2. Zweistufiger CPR (Constrained Pressure Residual) Rechts-Vorkonditionierer M_CPR^{-1} * r innerhalb von Matrix-Free JFNK:
//    Stufe 1: Quasi-IMPES Druck-Restriktion r_p = R_p^T * r -> 1 skalarer AMG-V-Zyklus auf A_pp^{CPR} * dp = r_p
//    Stufe 2: Defekt-Update r_corr = r - J * (P_p * dp) via Enzyme Forward-AD -> 1 zelllokaler GPU Block-Jacobi/BILU(0)-Sweep:
inline void apply_cpr_preconditioner(
    size_t N_cells, int M_dofs, const double* r_full, double* z_out,
    double* r_p_buf, double* dp_buf, double* J_Pp_dp_buf)
{
    restrict_quasi_impes_pressure(N_cells, M_dofs, r_full, r_p_buf);    // r_p = R_p^T * r
    amg_pressure_cpr.v_cycle(r_p_buf, dp_buf);                          // dp  = (A_pp^{CPR})^{-1} * r_p (1 skalarer AMG-V-Zyklus!)
    prolongate_and_jvp_enzyme(N_cells, M_dofs, dp_buf, J_Pp_dp_buf);    // J * (P_p * dp) matrixfrei via __enzyme_fwddiff
    apply_cell_block_jacobi_or_bilu0(N_cells, M_dofs, r_full, J_Pp_dp_buf, dp_buf, z_out); // z = M_BILU^{-1}(r - J*P_p*dp) + P_p*dp
}
```

