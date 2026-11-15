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
9. [Gold-Snippets & Direkt-Lesezeichen (Snippets 1–14)](#8-gold-snippets--direkt-lesezeichen-wichtigste-codeblöcke-mit-zeilennummern)

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
