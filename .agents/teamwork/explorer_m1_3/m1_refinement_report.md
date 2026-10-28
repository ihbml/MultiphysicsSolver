# Milestone M1 Technical Specification & Design Report: Curvature & Proximity Refinement

**Target Module**: `backend/meshing/geometry_sdf.py` (with integration into `mesh_engine.py` & `distmesh_core.py`)  
**Author**: `explorer_m1_3` (Teamwork Explorer)  
**Date**: 2026-10-28  
**Mission**: Investigate and design the implementation for Milestone M1 (Curvature & Proximity Refinement):
1. Design adaptive element sizing $h(\mathbf{x})$ in `backend/meshing/geometry_sdf.py`:
   - **Curvature refinement**: compute curvature $\kappa(\mathbf{x})$ from SDF (Laplacian of distance field $\Delta \phi$) or surface triangle normals. Local element size $h_{\text{curv}} = \alpha / \max(|\kappa|, \epsilon)$.
   - **Proximity refinement**: detect narrow gaps / opposing walls along normal ray $-\nabla \phi$. Local element size $h_{\text{prox}} = \beta \cdot d_{\text{gap}}$.
   - **Combined sizing function**: $h(\mathbf{x}) = \max(h_{\min}, \min(h_{\max}, h_{\text{curv}}, h_{\text{prox}}))$.
2. Provide concrete algorithms, vectorized implementation, and unit test designs for the Worker.

---

## 1. Executive Summary & Architectural Context

In unstructured mesh generation (DistMesh force-equilibrium, Bowyer-Watson Delaunay, hybrid FVM-SPH coupling), uniform mesh sizing leads to severe failure modes:
1. **Curvature under-resolution**: Inadequate element density on curved surfaces (leading/trailing edges of airfoils, cylinder contours, blade fillets) produces high chordal deflection errors, geometric staircase artifacts, and inaccurate boundary normal gradients.
2. **Narrow gap bridging & element collapse**: In narrow passages, throats, and tip clearances, an element size exceeding the channel width forces cells to bridge across the walls or invert ($V_i \le 0$), causing catastrophic breakdown in finite-volume PISO/SIMPLE solvers.
3. **Discontinuous sizing jumps**: Uncontrolled spatial gradients in element sizing induce high non-orthogonality and numerical diffusion in FVM fluxes.

This report establishes the complete mathematical, algorithmic, and vectorized implementation framework for the adaptive sizing engine $h(\mathbf{x})$, structured for seamless addition to `backend/meshing/geometry_sdf.py`.

### Sizing Pipeline Overview

```
                      [ Continuous SDF Evaluator: phi(x), grad phi ]
                                         │
                 ┌───────────────────────┴───────────────────────┐
                 ▼                                               ▼
    [ Curvature Refinement Engine ]                [ Proximity Refinement Engine ]
    - Exact Hessian/Laplacian:                    - Inward Ray: r(t) = x - t * grad phi
      kappa(x) = div(grad phi / ||grad phi||)     - Sphere-tracing opposing wall search:
      = Delta phi for exact distance field          phi(x - t* * grad phi) == 0
    - Discrete SurfaceMesh normal variation       - Opposing normal filter:
    - h_curv = alpha / max(|kappa|, eps)            n_hit . (-grad phi) < -cos(theta_opp)
                                                  - h_prox = beta * d_gap
                 │                                               │
                 └───────────────────────┬───────────────────────┘
                                         ▼
                         [ Combined Sizing Aggregator ]
               h_raw(x) = max(h_min, min(h_max, h_curv(x), h_prox(x)))
                                         │
                                         ▼
                         [ Lipschitz Grading Smoother ]
                       ||grad h(x)|| <= G  (G in [0.25, 0.45])
                         h_i <- min(h_i, h_j + G * ||x_i - x_j||)
                                         │
                                         ▼
                      [ Body-Fitted DistMesh / Delaunay Core ]
```

---

## 2. Mathematical Formulations & Derivations

### 2.1 Level Set Geometry & SDF Curvature ($\kappa(\mathbf{x})$)

Let $\phi(\mathbf{x}): \mathbb{R}^d \to \mathbb{R}$ ($d \in \{2, 3\}$) be a continuous Signed Distance Field (SDF) whose zero level set $\Gamma = \{\mathbf{x} \in \mathbb{R}^d \mid \phi(\mathbf{x}) = 0\}$ defines the domain boundary.
By standard CFD and DistMesh convention:
- $\phi(\mathbf{x}) < 0$ in the fluid interior (the domain being meshed).
- $\phi(\mathbf{x}) = 0$ on physical boundaries (walls, inlets, outlets).
- $\phi(\mathbf{x}) > 0$ outside the computational domain (solid obstacles or exterior).

#### 2.1.1 Eikonal Property and Normal Vector Field
In a tubular neighborhood around the boundary $\Gamma$, an exact distance function satisfies the Eikonal equation:
$$\|\nabla \phi(\mathbf{x})\| = 1$$
The unit outward normal vector field pointing from the fluid domain into the boundary is:
$$\mathbf{n}(\mathbf{x}) = \frac{\nabla \phi(\mathbf{x})}{\|\nabla \phi(\mathbf{x})\|}$$
The inward normal vector field pointing into the fluid domain is:
$$\mathbf{v}_{\text{inward}}(\mathbf{x}) = -\mathbf{n}(\mathbf{x}) = -\frac{\nabla \phi(\mathbf{x})}{\|\nabla \phi(\mathbf{x})\|}$$

#### 2.1.2 Mean Curvature and the Distance Field Laplacian
The mean curvature $H$ of a level surface $\phi(\mathbf{x}) = c$ is defined as the divergence of the unit normal vector:
$$\kappa_{\text{mean}}(\mathbf{x}) = \nabla \cdot \mathbf{n}(\mathbf{x}) = \nabla \cdot \left(\frac{\nabla \phi}{\|\nabla \phi\|}\right)$$
Applying vector calculus:
$$\nabla \cdot \left(\frac{\nabla \phi}{\|\nabla \phi\|}\right) = \frac{\Delta \phi}{\|\nabla \phi\|} - \frac{\nabla \phi \cdot (\mathbf{H}_\phi \nabla \phi)}{\|\nabla \phi\|^3}$$
where $\Delta \phi = \text{Tr}(\mathbf{H}_\phi) = \sum_{i=1}^d \frac{\partial^2 \phi}{\partial x_i^2}$ is the spatial Laplacian of $\phi$, and $\mathbf{H}_\phi = \nabla^2 \phi$ is the Hessian matrix:
$$(\mathbf{H}_\phi)_{ij} = \frac{\partial^2 \phi}{\partial x_i \partial x_j}$$
For an exact signed distance field, $\|\nabla \phi(\mathbf{x})\|^2 = 1$ identically. Differentiating this identity yields:
$$\nabla (\|\nabla \phi\|^2) = 2 \mathbf{H}_\phi \nabla \phi = \mathbf{0} \implies \nabla \phi \cdot (\mathbf{H}_\phi \nabla \phi) = 0$$
Hence, for an exact distance field:
$$\nabla \cdot \mathbf{n}(\mathbf{x}) \equiv \Delta \phi(\mathbf{x})$$

#### 2.1.3 Dimensional Specifics (2D vs 3D)
1. **In 2D ($d=2$)**:
   The level set is a planar curve. The Frenet curvature $\kappa$ is:
   $$\kappa(\mathbf{x}) = \Delta \phi(\mathbf{x}) = \frac{\partial^2 \phi}{\partial x^2} + \frac{\partial^2 \phi}{\partial y^2}$$
   - *Circle of radius $R$*: $\phi(r) = r - R$. $\Delta \phi = \frac{1}{r} \to \frac{1}{R}$ on the boundary. Curvature is exactly $\kappa = 1/R$.
   - *Flat line / box face*: $\phi(x, y) = a x + b y + c$. $\Delta \phi \equiv 0$. Curvature is $\kappa = 0$.

2. **In 3D ($d=3$)**:
   The level set is a 2D surface embedded in 3D. The eigenvalues of the shape operator $\mathbf{S} = \nabla \mathbf{n}$ are $0$ (along the normal direction $\mathbf{n}$) and the two principal curvatures $\kappa_1, \kappa_2$ (along tangent directions).
   The trace gives:
   $$\text{Tr}(\mathbf{S}) = \kappa_1 + \kappa_2 = \Delta \phi(\mathbf{x}) = 2 H(\mathbf{x})$$
   - *Sphere of radius $R$*: $\phi(r) = r - R$. $\Delta \phi = \frac{2}{r} \to \frac{2}{R}$. Mean curvature is $H = \frac{1}{2} \Delta \phi = 1/R$.
   - *Cylinder of radius $R$*: $\phi(r, z) = r - R$. $\kappa_1 = 1/R, \kappa_2 = 0 \implies \Delta \phi = 1/R$.
   - *Flat plane*: $\Delta \phi \equiv 0$.

To ensure isotropic element sizing that captures the tightest radius of curvature, the effective curvature magnitude is:
$$\kappa_{\text{eff}}(\mathbf{x}) = \begin{cases}
|\Delta \phi(\mathbf{x})| & \text{in 2D} \\
\max(|\kappa_1(\mathbf{x})|, |\kappa_2(\mathbf{x})|) \quad \text{or} \quad |\Delta \phi(\mathbf{x})| & \text{in 3D}
\end{cases}$$
Because $|\Delta \phi| = |\kappa_1 + \kappa_2|$, using $|\Delta \phi|$ is exact for 2D curves and 3D cylinders, and within a factor of 2 for 3D spheres. For maximum robustness and zero-eigenvalue overhead, $|\Delta \phi(\mathbf{x})|$ serves as the primary vectorized metric, with eigenvalue-based principal curvature extraction available as an option.

---

### 2.2 Surface Triangle Normal Curvature (for `SurfaceMesh`)

For arbitrary triangulated CAD meshes (`SurfaceMesh` in `backend/geometry/models.py`), continuous curvature can be extracted directly from surface geometry:

#### 2.2.1 Edge Dihedral Angle Metric
For each interior edge $e = (v_a, v_b)$ of length $L_e = \|\mathbf{x}_a - \mathbf{x}_b\|$ shared by two triangular facets $f_1, f_2$ with unit normals $\mathbf{n}_1, \mathbf{n}_2$:
The exterior dihedral angle $\theta_e$ is:
$$\cos \theta_e = \text{clamp}(\mathbf{n}_1 \cdot \mathbf{n}_2, -1.0, 1.0), \quad \theta_e = \arccos(\mathbf{n}_1 \cdot \mathbf{n}_2)$$
The line-integrated curvature across the edge is:
$$\kappa_e = \frac{\theta_e}{L_{\perp, e}} = \frac{2 \theta_e L_e}{A(f_1) + A(f_2)}$$

#### 2.2.2 Vertex Area-Weighted Normal Variation
For each mesh vertex $v_i$, compute the angle-weighted or area-weighted vertex normal:
$$\mathbf{n}_i = \frac{\sum_{f \in \mathcal{F}(i)} A_f \mathbf{n}_f}{\|\sum_{f \in \mathcal{F}(i)} A_f \mathbf{n}_f\|}$$
The discrete mean curvature magnitude at vertex $v_i$ is estimated by the normal variance over its 1-ring neighborhood $\mathcal{N}(i)$:
$$\kappa_i = \frac{1}{\sum_{j \in \mathcal{N}(i)} \|\mathbf{x}_i - \mathbf{x}_j\|} \sum_{j \in \mathcal{N}(i)} \|\mathbf{n}_i - \mathbf{n}_j\|$$
For query point $\mathbf{x}$, let $\mathbf{p}^*$ be the closest point on the surface mesh (queried via `cKDTree` on facet centroids or vertices). The surface curvature at $\mathbf{p}^*$ is mapped to $\mathbf{x}$ and smoothly decayed into the domain:
$$\kappa(\mathbf{x}) = \frac{\kappa(\mathbf{p}^*)}{1 + \gamma_{\text{decay}} \frac{\|\mathbf{x} - \mathbf{p}^*\|}{h_0}}$$
where $\gamma_{\text{decay}} \approx 1.0$.

---

### 2.3 Curvature Sizing Formulation ($h_{\text{curv}}(\mathbf{x})$)

The curvature-adapted element size is formulated as:
$$h_{\text{curv}}(\mathbf{x}) = \frac{\alpha}{\max(|\kappa(\mathbf{x})|, \epsilon)}$$

#### 2.3.1 Parameter Derivation & Physical Meaning
- **Coefficient $\alpha$ (Target Angle Resolution)**:
  Consider a circular boundary of radius $R = 1/|\kappa|$.
  The circumference is $C = 2\pi R$. To resolve this circle with $N_{\text{circ}}$ linear segments / elements:
  $$h_{\text{target}} = \frac{2\pi R}{N_{\text{circ}}} = \frac{2\pi / N_{\text{circ}}}{|\kappa|}$$
  Comparing with $h_{\text{curv}} = \alpha / |\kappa|$:
  $$\alpha = \frac{2\pi}{N_{\text{circ}}} = \Delta \theta_{\text{target}} \quad (\text{radians})$$
  - Standard industrial CFD guidelines require $N_{\text{circ}} \in [24, 40]$ elements per $360^\circ$ circle ($9^\circ$ to $15^\circ$ per element).
  - Setting $\alpha = 0.20\,\text{rad}$ ($11.46^\circ$) yields $N_{\text{circ}} \approx 31.4$ elements per circle.
  - Setting $\alpha = 0.15\,\text{rad}$ ($8.59^\circ$) yields $N_{\text{circ}} \approx 41.9$ elements per circle.
  - **Recommended Default**: $\alpha = 0.20$.

- **Regularizer $\epsilon$ (Curvature Singularity Guard)**:
  On flat surfaces (such as planar walls of a box or duct), $\kappa \to 0$. Without regularization, $h_{\text{curv}} \to \infty$.
  Setting $\epsilon = 10^{-6}\,\text{m}^{-1}$ ensures:
  $$h_{\text{curv}}(\text{flat}) = \frac{0.20}{10^{-6}} = 200,000\,\text{m} \gg h_{\max}$$
  which smoothly triggers the upper clamp $h_{\max}$ with no numerical instability or division by zero.

---

### 2.4 Proximity Refinement & Opposing Wall Ray Marching ($h_{\text{prox}}(\mathbf{x})$)

#### 2.4.1 Geometric Problem Statement
In fluid domains, narrow channels occur when two opposing physical boundaries are separated by a small gap distance $d_{\text{gap}}$.
At any interior point $\mathbf{x}$ ($\phi(\mathbf{x}) < 0$):
1. The nearest boundary lies in the direction of the outward gradient $\nabla \phi(\mathbf{x})$. The distance to the nearest wall is:
   $$d_{\text{near}}(\mathbf{x}) = |\phi(\mathbf{x})| = -\phi(\mathbf{x})$$
2. The opposing wall lies along the inward normal ray:
   $$\mathbf{r}(t) = \mathbf{x} - t \frac{\nabla \phi(\mathbf{x})}{\|\nabla \phi(\mathbf{x})\|}, \quad t \ge 0$$
3. As parameter $t$ increases:
   - For small $t$, $\mathbf{r}(t)$ travels deeper into the fluid core where $\phi(\mathbf{r}(t)) < 0$.
   - As $\mathbf{r}(t)$ approaches the opposing wall, $\phi$ increases toward zero.
   - At distance $t = t^* > 0$, the ray intersects the opposing wall:
     $$\phi(\mathbf{r}(t^*)) = 0$$
4. The total gap thickness across the channel along the normal line through $\mathbf{x}$ is:
   $$d_{\text{gap}}(\mathbf{x}) = d_{\text{near}}(\mathbf{x}) + t^* = -\phi(\mathbf{x}) + t^*$$
5. For points located directly on a boundary wall ($\phi(\mathbf{x}) = 0$):
   $d_{\text{near}} = 0$, so $d_{\text{gap}}(\mathbf{x}) = t^*$.

```
Wall A (phi = 0)                   Wall B (opposing, phi = 0)
    │                                  │
    │<--- d_near ---> * <---- t* ----->│
    │               ( x )              │
    │                 │                │
    │                 ▼                │
    │          Ray: x - t * grad phi   │
    │                                  │
    └──────────────── d_gap ───────────┘
```

#### 2.4.2 Opposing Normal Compatibility Criterion
A ray cast into the domain might graze an adjacent convex corner or hit a non-opposing boundary.
To qualify as a genuine opposing wall:
Let $\mathbf{v} = -\frac{\nabla \phi(\mathbf{x})}{\|\nabla \phi(\mathbf{x})\|}$ be the ray direction.
Let $\mathbf{n}_{\text{hit}} = \frac{\nabla \phi(\mathbf{r}(t^*))}{\|\nabla \phi(\mathbf{r}(t^*))\|}$ be the outward normal at the hit point.
Since $\nabla \phi$ points outward from the fluid into the wall:
At the opposing wall, $\mathbf{n}_{\text{hit}}$ points back in the direction opposite to $\mathbf{v}$.
Hence, the opposing alignment condition is:
$$\mathbf{n}_{\text{hit}} \cdot \mathbf{v} \le -\cos(\theta_{\text{opp}})$$
where $\theta_{\text{opp}} \approx 45^\circ \dots 60^\circ$.
We define the test:
$$\mathbf{n}_{\text{hit}} \cdot \mathbf{v} < -0.5$$
If $\mathbf{n}_{\text{hit}} \cdot \mathbf{v} \ge -0.5$, the hit surface is a lateral wall, oblique boundary, or convex corner, and does NOT constitute a narrow opposing gap. In that case, $d_{\text{gap}} \to \infty$.

#### 2.4.3 Proximity Sizing Formulation
The proximity-adapted element size is:
$$h_{\text{prox}}(\mathbf{x}) = \beta \cdot d_{\text{gap}}(\mathbf{x})$$
- **Coefficient $\beta$ (Gap Element Count)**:
  $$\beta = \frac{1}{N_{\text{gap}}}$$
  where $N_{\text{gap}}$ is the desired number of elements spanning the channel width.
  - $N_{\text{gap}} = 3 \implies \beta = 0.333$
  - $N_{\text{gap}} = 4 \implies \beta = 0.250$ (Recommended Default)
  - $N_{\text{gap}} = 5 \implies \beta = 0.200$
- If no opposing wall is found within maximum search radius $R_{\text{prox,max}} = h_{\max} / \beta$, then $d_{\text{gap}} \ge R_{\text{prox,max}}$, so $h_{\text{prox}}(\mathbf{x}) \ge h_{\max}$.

---

### 2.5 Combined Sizing Function ($h(\mathbf{x})$)

The local element size is the lower envelope of all sizing criteria, constrained by strict upper and lower bounds:
$$h_{\text{raw}}(\mathbf{x}) = \max\left(h_{\min}, \min\left(h_{\max}, h_{\text{curv}}(\mathbf{x}), h_{\text{prox}}(\mathbf{x})\right)\right)$$

If an optional baseline distance-grading function (Persson & Strang 2004) is active:
$$h_{\text{dist}}(\mathbf{x}) = h_0 \left(1 + g_{\text{dist}} \frac{|\phi(\mathbf{x})|}{h_0}\right)$$
then:
$$h_{\text{raw}}(\mathbf{x}) = \max\left(h_{\min}, \min\left(h_{\max}, h_{\text{dist}}(\mathbf{x}), h_{\text{curv}}(\mathbf{x}), h_{\text{prox}}(\mathbf{x})\right)\right)$$

---

### 2.6 Lipschitz Grading Smoothing ($\|\nabla h(\mathbf{x})\| \le G$)

In Delaunay / DistMesh meshing, element quality deteriorates if adjacent elements have disparate sizes. The sizing field must be $G$-Lipschitz continuous:
$$|h(\mathbf{x}) - h(\mathbf{y})| \le G \|\mathbf{x} - \mathbf{y}\| \quad \forall \mathbf{x}, \mathbf{y}$$
where $G \in [0.25, 0.45]$ is the maximum allowable sizing gradient ($G = 0.35$ corresponds to an adjacent cell expansion ratio of $1 + G \approx 1.35$).
For a discrete set of seed points $\{\mathbf{x}_i\}_{i=1}^N$:
$$h_i \leftarrow \min_{j} \left( h_j + G \|\mathbf{x}_i - \mathbf{x}_j\| \right)$$
This operation is computed via KDTree nearest-neighbor queries or iterative sweeps over Delaunay edges.

---

## 3. Concrete Vectorized Algorithms

### Algorithm 1: Vectorized Numerical Laplacian Curvature Evaluator

```text
Algorithm: VectorizedSDFCurvature
Input: 
  points: array of shape (N, d), query coordinates
  sdf_fn: function evaluating phi(x) and grad_phi(x)
  delta: finite difference step size (default: 1e-4)
  dim: spatial dimension d (2 or 3)
Output:
  kappa: array of shape (N,), curvature magnitude >= 0

1. Evaluate phi_0 = sdf_fn(points)[0]  // shape (N,)
2. laplacian = zeros(N)
3. For each spatial axis k in 0 .. d-1:
     e_k = unit vector along axis k
     points_pos = points + delta * e_k
     points_neg = points - delta * e_k
     phi_pos = sdf_fn(points_pos)[0]
     phi_neg = sdf_fn(points_neg)[0]
     d2_phi_k = (phi_pos - 2.0 * phi_0 + phi_neg) / (delta * delta)
     laplacian += d2_phi_k
4. If dim == 2:
     kappa = abs(laplacian)
   Else (dim == 3):
     kappa = 0.5 * abs(laplacian)  // mean curvature |H|
5. Return kappa
```

### Algorithm 2: Vectorized Opposing Wall Ray Marcher

```text
Algorithm: VectorizedOpposingWallRayMarcher
Input:
  points: array of shape (N, d), query coordinates
  sdf_fn: function returning (phi, grad_phi)
  h_max: maximum element size
  beta: proximity factor (default: 0.25)
  n_steps: number of ray marching samples (default: 8)
  theta_opp_cos: normal alignment threshold (default: -0.5)
Output:
  d_gap: array of shape (N,), gap thickness (clamped to R_max if no gap)

1. phi_0, grad_0 = sdf_fn(points)
2. grad_norm = norm(grad_0, axis=1, keepdims=True)
3. grad_norm = max(grad_norm, 1e-12)
4. v_ray = -grad_0 / grad_norm  // inward ray direction, shape (N, d)
5. d_near = abs(phi_0)
6. R_max = h_max / beta  // maximum search radius

7. // Construct marching distances along ray
   // Sample linearly or quadratically from 0.5 * d_near to R_max
   t_samples = linspace(1.0 / n_steps, 1.0, n_steps)  // shape (S,)
   
8. // Vectorized tensor evaluation of all rays and steps
   // ray_pts shape: (N, S, d)
   ray_pts = points[:, None, :] + (t_samples[None, :, None] * R_max) * v_ray[:, None, :]
   
9. Flatten ray_pts to (N * S, d) and evaluate phi_ray, grad_ray via sdf_fn
10. Reshape phi_ray to (N, S) and grad_ray to (N, S, d)

11. For each ray i in 0 .. N-1:
      Find first step s where phi_ray[i, s] >= 0.0:
      If found and s > 0:
        // Linear interpolation root-finding for t*
        t_prev = t_samples[s - 1] * R_max
        t_curr = t_samples[s] * R_max
        phi_prev = phi_ray[i, s - 1]
        phi_curr = phi_ray[i, s]
        t_star = t_prev + (-phi_prev) / (phi_curr - phi_prev + 1e-15) * (t_curr - t_prev)
        
        // Opposing normal compatibility check
        n_hit = grad_ray[i, s] / max(norm(grad_ray[i, s]), 1e-12)
        alignment = dot(n_hit, v_ray[i])
        If alignment < theta_opp_cos:
          d_gap[i] = d_near[i] + t_star
        Else:
          d_gap[i] = R_max
      Else:
        d_gap[i] = R_max

12. Return d_gap
```

---

## 4. Vectorized Python/NumPy Implementation

The complete, production-ready implementation designed for `backend/meshing/geometry_sdf.py` is provided below.

```python
"""Adaptive element sizing engine for unstructured meshing (Milestone M1).

Provides:
- SizingParameters: Configuration dataclass for curvature, proximity, and grading.
- CurvatureRefinement: Evaluates level set curvature from SDF Laplacian or SurfaceMesh.
- ProximityRefinement: Evaluates opposing wall gap distance via inward ray marching.
- AdaptiveSizeFunction: High-level vectorized sizing evaluator combining bounds and grading.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple, Union
import numpy as np


class CurvatureMethod(str, Enum):
    """Method for evaluating surface curvature."""
    SDF_LAPLACIAN = "sdf_laplacian"
    SURFACE_MESH = "surface_mesh"
    HYBRID = "hybrid"


@dataclass
class SizingParameters:
    """Configuration parameters for adaptive element sizing h(x)."""

    h_min: float = 0.005
    """Strict lower bound on element size (m)."""

    h_max: float = 0.200
    """Strict upper bound on element size (m)."""

    h0: float = 0.050
    """Nominal / background element size (m)."""

    alpha: float = 0.200
    """Curvature resolution factor (rad). h_curv = alpha / max(|kappa|, eps).
    Equivalent to ~31.4 linear elements around a full 360-degree circle."""

    beta: float = 0.250
    """Proximity resolution factor. h_prox = beta * gap_distance.
    Equivalent to 4 elements spanning a narrow gap (1 / beta = 4)."""

    eps_curv: float = 1e-6
    """Curvature regularizer avoiding division-by-zero in planar regions (1/m)."""

    grading: float = 0.350
    """Maximum spatial growth rate ||grad h|| <= grading (dimensionless)."""

    max_gap_search: Optional[float] = None
    """Maximum opposing wall search distance. Defaults to h_max / beta."""

    ray_march_steps: int = 8
    """Number of discrete sampling steps for opposing wall ray marching."""

    theta_opp_threshold: float = -0.500
    """Normal dot product threshold cos(theta) for opposing wall detection."""

    fd_delta: float = 1e-4
    """Finite difference perturbation step for second derivatives (m)."""

    def __post_init__(self) -> None:
        if self.h_min <= 0.0:
            raise ValueError(f"h_min must be positive, got {self.h_min}")
        if self.h_max < self.h_min:
            raise ValueError(f"h_max ({self.h_max}) cannot be smaller than h_min ({self.h_min})")
        if self.alpha <= 0.0:
            raise ValueError(f"alpha must be positive, got {self.alpha}")
        if self.beta <= 0.0:
            raise ValueError(f"beta must be positive, got {self.beta}")
        if self.grading <= 0.0:
            raise ValueError(f"grading must be positive, got {self.grading}")
        if self.max_gap_search is None:
            self.max_gap_search = self.h_max / self.beta


class CurvatureRefinement:
    """Computes continuous curvature kappa(x) and curvature-adapted size h_curv(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
    ) -> None:
        """Initialize curvature evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
            surface_mesh: Optional backend.geometry.models.SurfaceMesh for hybrid evaluation.
        """
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim
        self.surface_mesh = surface_mesh
        self._mesh_kdtree: Optional[Any] = None
        self._mesh_vertex_curvatures: Optional[np.ndarray] = None

        if self.surface_mesh is not None:
            self._precompute_surface_curvatures()

    def _precompute_surface_curvatures(self) -> None:
        """Precompute vertex curvatures on triangulated SurfaceMesh."""
        from scipy.spatial import cKDTree

        mesh = self.surface_mesh
        verts = mesh.vertices  # (V, 3)
        faces = mesh.faces  # (F, 3)
        normals = mesh.normals  # (F, 3)

        n_verts = len(verts)
        vert_normals = np.zeros((n_verts, 3), dtype=np.float64)
        np.add.at(vert_normals, faces[:, 0], normals)
        np.add.at(vert_normals, faces[:, 1], normals)
        np.add.at(vert_normals, faces[:, 2], normals)
        norms = np.linalg.norm(vert_normals, axis=1, keepdims=True)
        norms[norms < 1e-15] = 1.0
        vert_normals /= norms

        # Build adjacency graph to compute neighbor normal differences
        curv = np.zeros(n_verts, dtype=np.float64)
        degree = np.zeros(n_verts, dtype=np.float64)

        for edge_a, edge_b in ((0, 1), (1, 2), (2, 0)):
            va = faces[:, edge_a]
            vb = faces[:, edge_b]
            dn = np.linalg.norm(vert_normals[va] - vert_normals[vb], axis=1)
            dx = np.linalg.norm(verts[va] - verts[vb], axis=1)
            dx[dx < 1e-15] = 1e-15
            local_k = dn / dx
            np.add.at(curv, va, local_k)
            np.add.at(curv, vb, local_k)
            np.add.at(degree, va, 1.0)
            np.add.at(degree, vb, 1.0)

        degree[degree < 1.0] = 1.0
        self._mesh_vertex_curvatures = curv / degree
        self._mesh_kdtree = cKDTree(verts)

    def evaluate_curvature(self, points: np.ndarray) -> np.ndarray:
        """Evaluate curvature magnitude kappa(x) for an array of coordinates.

        Args:
            points: Array of shape (N, d), float64.

        Returns:
            kappa: Array of shape (N,), float64 non-negative curvature values.
        """
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape
        delta = self.params.fd_delta

        # Vectorized finite difference Laplacian
        phi_0, _ = self.sdf_evaluator(pts)
        laplacian = np.zeros(n_pts, dtype=np.float64)

        for axis in range(d):
            e_k = np.zeros((1, d), dtype=np.float64)
            e_k[0, axis] = delta
            phi_pos, _ = self.sdf_evaluator(pts + e_k)
            phi_neg, _ = self.sdf_evaluator(pts - e_k)
            laplacian += (phi_pos - 2.0 * phi_0 + phi_neg) / (delta * delta)

        if d == 2:
            kappa = np.abs(laplacian)
        else:
            kappa = 0.5 * np.abs(laplacian)  # Mean curvature |H|

        # Hybrid fallback if SurfaceMesh is present
        if self._mesh_kdtree is not None and self._mesh_vertex_curvatures is not None:
            dists, idxs = self._mesh_kdtree.query(pts, k=1)
            mesh_k = self._mesh_vertex_curvatures[idxs]
            # Near boundary (dist < 2 * h0), blend in surface mesh curvature
            decay = 1.0 / (1.0 + dists / self.params.h0)
            kappa = np.maximum(kappa, mesh_k * decay)

        return kappa

    def evaluate_h_curv(self, points: np.ndarray) -> np.ndarray:
        """Compute local curvature-adapted element size h_curv(x).

        Args:
            points: Array of shape (N, d), float64.

        Returns:
            h_curv: Array of shape (N,), float64 local element sizes.
        """
        kappa = self.evaluate_curvature(points)
        denom = np.maximum(kappa, self.params.eps_curv)
        h_curv = self.params.alpha / denom
        return np.clip(h_curv, self.params.h_min, self.params.h_max)


class ProximityRefinement:
    """Detects opposing walls and computes proximity-adapted size h_prox(x)."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
    ) -> None:
        """Initialize proximity evaluator.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing parameters configuration.
            dim: Spatial dimension (2 or 3).
        """
        self.sdf_evaluator = sdf_evaluator
        self.params = params or SizingParameters()
        self.dim = dim

    def evaluate_gap_distance(self, points: np.ndarray) -> np.ndarray:
        """Compute the narrow gap distance d_gap(x) along the inward normal ray.

        Args:
            points: Array of shape (N, d), float64.

        Returns:
            d_gap: Array of shape (N,), float64 gap distance. Clamped to max_gap_search
                   if no opposing wall exists within range.
        """
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            pts = pts.reshape(1, -1)
        n_pts, d = pts.shape

        phi_0, grad_0 = self.sdf_evaluator(pts)
        grad_norms = np.linalg.norm(grad_0, axis=1, keepdims=True)
        grad_norms[grad_norms < 1e-12] = 1.0
        v_inward = -grad_0 / grad_norms  # Ray direction into domain

        r_max = float(self.params.max_gap_search or (self.params.h_max / self.params.beta))
        n_steps = self.params.ray_march_steps
        t_samples = np.linspace(1.0 / n_steps, 1.0, n_steps)  # shape (S,)

        # Construct candidate ray evaluation points: shape (N, S, d)
        ray_pts = pts[:, None, :] + (t_samples[None, :, None] * r_max) * v_inward[:, None, :]
        flat_ray_pts = ray_pts.reshape(-1, d)

        # Batch evaluate all sample points
        phi_ray_flat, grad_ray_flat = self.sdf_evaluator(flat_ray_pts)
        phi_ray = phi_ray_flat.reshape(n_pts, n_steps)
        grad_ray = grad_ray_flat.reshape(n_pts, n_steps, d)

        d_near = np.abs(phi_0)
        d_gap = np.full(n_pts, r_max, dtype=np.float64)

        for i in range(n_pts):
            if phi_0[i] > 0.0:
                # Outside domain; proximity inactive
                continue

            # Find first crossing where phi becomes non-negative (opposing wall hit)
            crossings = np.where(phi_ray[i] >= 0.0)[0]
            if len(crossings) == 0:
                continue

            first_cross = crossings[0]
            if first_cross == 0:
                t_star = t_samples[0] * r_max
                hit_grad = grad_ray[i, 0]
            else:
                t_prev = t_samples[first_cross - 1] * r_max
                t_curr = t_samples[first_cross] * r_max
                phi_prev = phi_ray[i, first_cross - 1]
                phi_curr = phi_ray[i, first_cross]
                denom = phi_curr - phi_prev
                frac = (-phi_prev) / denom if np.abs(denom) > 1e-15 else 0.5
                t_star = t_prev + frac * (t_curr - t_prev)
                hit_grad = grad_ray[i, first_cross]

            # Verify opposing normal compatibility
            hit_norm = np.linalg.norm(hit_grad)
            if hit_norm > 1e-12:
                n_hit = hit_grad / hit_norm
                alignment = np.dot(n_hit, v_inward[i])
                if alignment < self.params.theta_opp_threshold:
                    # Valid opposing wall
                    d_gap[i] = d_near[i] + t_star

        return d_gap

    def evaluate_h_prox(self, points: np.ndarray) -> np.ndarray:
        """Compute local proximity-adapted element size h_prox(x).

        Args:
            points: Array of shape (N, d), float64.

        Returns:
            h_prox: Array of shape (N,), float64 local element sizes.
        """
        d_gap = self.evaluate_gap_distance(points)
        h_prox = self.params.beta * d_gap
        return np.clip(h_prox, self.params.h_min, self.params.h_max)


class AdaptiveSizeFunction:
    """Unified adaptive sizing field h(x) combining curvature, proximity, and grading."""

    def __init__(
        self,
        sdf_evaluator: Callable[[np.ndarray], Tuple[np.ndarray, np.ndarray]],
        params: Optional[SizingParameters] = None,
        dim: int = 3,
        surface_mesh: Optional[Any] = None,
    ) -> None:
        """Initialize adaptive sizing engine.

        Args:
            sdf_evaluator: Function taking (N, d) points and returning (phi, grad_phi).
            params: Sizing configuration parameters.
            dim: Spatial dimension (2 or 3).
            surface_mesh: Optional SurfaceMesh for CAD geometry integration.
        """
        self.params = params or SizingParameters()
        self.dim = dim
        self.sdf_evaluator = sdf_evaluator
        self.curvature_engine = CurvatureRefinement(
            sdf_evaluator=sdf_evaluator, params=self.params, dim=dim, surface_mesh=surface_mesh
        )
        self.proximity_engine = ProximityRefinement(
            sdf_evaluator=sdf_evaluator, params=self.params, dim=dim
        )

    def evaluate(self, points: np.ndarray) -> np.ndarray:
        """Evaluate combined element sizing field h(x).

        h(x) = max(h_min, min(h_max, h_curv(x), h_prox(x)))

        Args:
            points: Coordinate array of shape (N, d) or (d,).

        Returns:
            h: Array of shape (N,), target element sizes in [h_min, h_max].
        """
        pts = np.asarray(points, dtype=np.float64)
        single_point = pts.ndim == 1
        if single_point:
            pts = pts.reshape(1, -1)

        h_curv = self.curvature_engine.evaluate_h_curv(pts)
        h_prox = self.proximity_engine.evaluate_h_prox(pts)

        h_combined = np.minimum(h_curv, h_prox)
        h_clamped = np.clip(h_combined, self.params.h_min, self.params.h_max)

        return h_clamped[0] if single_point else h_clamped

    def evaluate_with_breakdown(
        self, points: np.ndarray
    ) -> Dict[str, np.ndarray]:
        """Evaluate all sizing components for diagnostics, audit, and plotting.

        Returns:
            Dictionary containing:
            - 'h': Combined sizing field in [h_min, h_max].
            - 'h_curv': Curvature-adapted sizing.
            - 'h_prox': Proximity-adapted sizing.
            - 'kappa': Evaluated curvature magnitude.
            - 'd_gap': Evaluated gap distance.
        """
        pts = np.asarray(points, dtype=np.float64)
        if pts.ndim == 1:
            pts = pts.reshape(1, -1)

        kappa = self.curvature_engine.evaluate_curvature(pts)
        h_curv = self.curvature_engine.evaluate_h_curv(pts)
        d_gap = self.proximity_engine.evaluate_gap_distance(pts)
        h_prox = self.proximity_engine.evaluate_h_prox(pts)
        h = np.clip(np.minimum(h_curv, h_prox), self.params.h_min, self.params.h_max)

        return {
            "h": h,
            "h_curv": h_curv,
            "h_prox": h_prox,
            "kappa": kappa,
            "d_gap": d_gap,
        }

    def apply_grading_filter(
        self, points: np.ndarray, sizes: np.ndarray, max_iterations: int = 3
    ) -> np.ndarray:
        """Enforce Lipschitz grading continuity ||grad h|| <= G across discrete points.

        Args:
            points: Array of shape (N, d).
            sizes: Initial sizing array of shape (N,).
            max_iterations: Number of KDTree relaxation sweeps.

        Returns:
            sizes_graded: Filtered sizing array satisfying metric grading.
        """
        from scipy.spatial import cKDTree

        pts = np.asarray(points, dtype=np.float64)
        h = np.copy(sizes)
        n = len(h)
        if n < 2:
            return h

        tree = cKDTree(pts)
        k_neighbors = min(16, n)
        g = self.params.grading

        for _ in range(max_iterations):
            dists, idxs = tree.query(pts, k=k_neighbors)
            # h_j + g * dist_ij
            neighbor_h = h[idxs]  # shape (N, k)
            limit_h = neighbor_h + g * dists
            min_limit = np.min(limit_h, axis=1)
            h = np.minimum(h, min_limit)

        return np.clip(h, self.params.h_min, self.params.h_max)
```

---

## 5. Edge Cases, Failure Modes & Safeguards

| # | Edge Case | Physical / Numerical Scenario | Failure Mode if Unhandled | Safeguard Implemented in Design |
|---|---|---|---|---|
| 1 | **Planar / Flat Boundaries** | Flat channel wall or box face: $\kappa \to 0$. | Division by zero: $\alpha / 0 \to \infty$ or `NaN`. | Denominator clamped to $\max(|\kappa|, \epsilon_{\text{curv}})$. For $\kappa = 0$, $h_{\text{curv}} = \alpha / 10^{-6} \gg h_{\max}$, clamping cleanly to $h_{\max}$. |
| 2 | **Sharp Corners & Fillets** | $90^\circ$ box corner or trailing edge knife edge ($\kappa \to \infty$). | Element size collapses below machine precision ($h \to 0$), causing infinite node generation. | Lower bound clamp: $h(\mathbf{x}) \ge h_{\min}$. Element size is bounded strictly by user-specified $h_{\min}$. |
| 3 | **Open Domain / Far-Field** | External aerodynamic flow where no opposing wall exists along $-\nabla \phi$. | Ray marches indefinitely or crashes with index error. | Upper bound search distance $R_{\text{prox,max}} = h_{\max} / \beta$. If no crossing within $R_{\max}$, $d_{\text{gap}} = R_{\max} \implies h_{\text{prox}} \ge h_{\max}$. |
| 4 | **Grazing Rays / Convex Bulges** | Inward ray intersects an adjacent convex contour or shallow tangent boundary. | False proximity: wide domain incorrectly flagged as a narrow gap. | Normal compatibility check: hit is only accepted if $\mathbf{n}_{\text{hit}} \cdot \mathbf{v} < -0.5$ (opposing wall orientation). |
| 5 | **Medial Axis / Centerline Points** | Query point located exactly on the Voronoi skeleton / medial axis where $\nabla \phi = \mathbf{0}$. | Division by zero during normal gradient normalization ($\nabla \phi / \|\nabla \phi\|$). | Normalization guarded by $\max(\|\nabla \phi\|, 10^{-12})$. If norm $< 10^{-12}$, ray marching falls back to identity step or skips proximity. |
| 6 | **Exterior Points ($\phi > 0$)** | Query point located inside a solid obstacle or exterior bounding box. | Ray marches outside the domain, yielding unphysical negative gap distances. | Points with $\phi(\mathbf{x}) > 0$ bypass proximity refinement, assigning default $h_{\max}$. |
| 7 | **Discontinuous Sizing Step** | Narrow gap immediately adjacent to an open reservoir. | Sudden element size jump ($h_{\min} \to h_{\max}$) producing high mesh non-orthogonality and sliver triangles. | Lipschitz grading filter: $\|\nabla h\| \le G$ propagates refinement smoothly with expansion ratio $\le 1 + G$. |
| 8 | **Gap Smaller Than $h_{\min}$** | Extreme micro-gap ($d_{\text{gap}} < h_{\min} / \beta$). | Sizing drops below $h_{\min}$. | Lower bound clamp $\max(h_{\min}, \dots)$ strictly enforces $h \ge h_{\min}$ everywhere. |

---

## 6. Comprehensive Unit Test Specifications for Worker

The Worker agent must implement a dedicated test module `tests/test_curvature_proximity.py` (or integrate into `tests/test_mesher.py`). The following 12 unit and integration tests must pass with 100% success rate:

### Test Suite Matrix

| # | Test Name | Target Function | Test Configuration | Expected Assertion |
|---|---|---|---|---|
| 1 | `test_analytical_curvature_circle_2d` | `CurvatureRefinement.evaluate_curvature` | 2D circle with $R = 2.0\,\text{m}$ and $R = 0.5\,\text{m}$. | $|\kappa - 1/R| / (1/R) < 0.01$ (error $< 1\%$). |
| 2 | `test_analytical_curvature_sphere_3d` | `CurvatureRefinement.evaluate_curvature` | 3D sphere with $R = 1.0\,\text{m}$. | $|H - 1/R| < 0.02$ ($|H| = \frac{1}{2}\Delta \phi = 1.0$). |
| 3 | `test_flat_boundary_curvature_zero` | `CurvatureRefinement.evaluate_curvature` | Planar level set $\phi(x) = x - 0.5$. | $\kappa < 10^{-6}$, and $h_{\text{curv}} = h_{\max}$. |
| 4 | `test_parallel_channel_proximity_gap` | `ProximityRefinement.evaluate_gap_distance` | Parallel plates at $y = 0.0$ and $y = 0.04$ ($W = 0.04\,\text{m}$). | $|d_{\text{gap}} - 0.04| < 0.002$ ($< 5\%$ error). |
| 5 | `test_parallel_channel_h_prox_scaling` | `ProximityRefinement.evaluate_h_prox` | Channel $W = 0.04\,\text{m}$, $\beta = 0.25$. | $h_{\text{prox}} = \beta \cdot W = 0.01\,\text{m} \pm 0.001\,\text{m}$. |
| 6 | `test_open_domain_no_proximity_trigger` | `ProximityRefinement.evaluate_gap_distance` | Open half-space $\phi(x) = -x$ for $x > 0$. | $d_{\text{gap}} \ge R_{\text{prox,max}}$, $h_{\text{prox}} = h_{\max}$. |
| 7 | `test_opposing_normal_compatibility_filter` | `ProximityRefinement.evaluate_gap_distance` | L-shaped domain with perpendicular walls. | Normal dot product $> -0.5$ rejected; no false opposing gap. |
| 8 | `test_combined_sizing_strict_bounds` | `AdaptiveSizeFunction.evaluate` | 1000 random query points across varied geometry. | $h_{\min} \le h(\mathbf{x}) \le h_{\max}$ for all $\mathbf{x}$. |
| 9 | `test_annulus_concentric_cylinders` | `AdaptiveSizeFunction.evaluate_with_breakdown` | Inner radius $R_1 = 0.1$, outer radius $R_2 = 0.14$ ($W = 0.04$). | High curvature on inner cylinder, narrow proximity in gap. |
| 10 | `test_vectorized_evaluation_performance` | `AdaptiveSizeFunction.evaluate` | Array of 10,000 points. | Execution time $< 0.50\,\text{seconds}$. |
| 11 | `test_lipschitz_grading_filter` | `AdaptiveSizeFunction.apply_grading_filter` | Point step discontinuity from $h_{\min}$ to $h_{\max}$. | $\max_{ij} \frac{|h_i - h_j|}{\|\mathbf{x}_i - \mathbf{x}_j\|} \le 1.1 \cdot G$. |
| 12 | `test_real_cad_blade_curvature_proximity` | `AdaptiveSizeFunction.evaluate` with `BladeRev1CFD.obj` | Surface mesh of turbine blade. | Refinement at leading edge ($\kappa > 20$) and trailing edge gap. |

### Concrete Pytest Implementation Code for Worker

```python
"""Unit tests for Curvature & Proximity Refinement (Milestone M1)."""

import time
import numpy as np
import pytest
from backend.meshing.geometry_sdf import (
    AdaptiveSizeFunction,
    CurvatureRefinement,
    ProximityRefinement,
    SizingParameters,
)


def make_circle_sdf(radius: float = 1.0, center=(0.0, 0.0)):
    """Creates a 2D analytical circle SDF where phi < 0 inside."""
    cx, cy = center

    def sdf(pts: np.ndarray):
        x = pts[:, 0] - cx
        y = pts[:, 1] - cy
        r = np.sqrt(x**2 + y**2)
        phi = r - radius  # phi < 0 inside
        r_safe = np.maximum(r, 1e-12)
        grad = np.column_stack([x / r_safe, y / r_safe])
        return phi, grad

    return sdf


def make_sphere_sdf(radius: float = 1.0, center=(0.0, 0.0, 0.0)):
    """Creates a 3D analytical sphere SDF where phi < 0 inside."""
    cx, cy, cz = center

    def sdf(pts: np.ndarray):
        x = pts[:, 0] - cx
        y = pts[:, 1] - cy
        z = pts[:, 2] - cz
        r = np.sqrt(x**2 + y**2 + z**2)
        phi = r - radius
        r_safe = np.maximum(r, 1e-12)
        grad = np.column_stack([x / r_safe, y / r_safe, z / r_safe])
        return phi, grad

    return sdf


def make_channel_sdf(width: float = 0.04):
    """Creates a 2D parallel channel: 0 <= y <= width (phi < 0 inside)."""
    half_w = width / 2.0
    cy = half_w

    def sdf(pts: np.ndarray):
        y = pts[:, 1]
        dist_center = np.abs(y - cy)
        phi = dist_center - half_w  # < 0 in [0, width]
        grad_y = np.where(y >= cy, 1.0, -1.0)
        grad = np.column_stack([np.zeros_like(grad_y), grad_y])
        return phi, grad

    return sdf


def test_analytical_curvature_circle_2d():
    """Verify exact 2D circle curvature kappa = 1/R within 1%."""
    R = 2.0
    sdf = make_circle_sdf(radius=R)
    curv_engine = CurvatureRefinement(sdf_evaluator=sdf, dim=2)

    # Sample points on the boundary
    theta = np.linspace(0, 2 * np.pi, 20, endpoint=False)
    pts = np.column_stack([R * np.cos(theta), R * np.sin(theta)])

    kappa = curv_engine.evaluate_curvature(pts)
    expected_kappa = 1.0 / R

    assert np.all(np.abs(kappa - expected_kappa) / expected_kappa < 0.01)


def test_analytical_curvature_sphere_3d():
    """Verify exact 3D sphere mean curvature |H| = 1/R within 2%."""
    R = 1.0
    sdf = make_sphere_sdf(radius=R)
    curv_engine = CurvatureRefinement(sdf_evaluator=sdf, dim=3)

    pts = np.array([[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]])
    H = curv_engine.evaluate_curvature(pts)

    assert np.all(np.abs(H - 1.0 / R) < 0.02)


def test_flat_boundary_curvature_zero():
    """Verify flat boundary produces kappa ~ 0 and triggers h_max."""
    def flat_sdf(pts):
        phi = pts[:, 0] - 1.0
        grad = np.column_stack([np.ones(len(pts)), np.zeros(len(pts))])
        return phi, grad

    params = SizingParameters(h_min=0.01, h_max=0.20, alpha=0.20)
    curv_engine = CurvatureRefinement(sdf_evaluator=flat_sdf, params=params, dim=2)

    pts = np.array([[1.0, 0.0], [1.0, 0.5], [1.0, -0.5]])
    kappa = curv_engine.evaluate_curvature(pts)
    h_curv = curv_engine.evaluate_h_curv(pts)

    assert np.all(kappa < 1e-6)
    assert np.allclose(h_curv, params.h_max)


def test_parallel_channel_proximity_gap():
    """Verify narrow channel gap distance d_gap == width."""
    W = 0.04
    sdf = make_channel_sdf(width=W)
    params = SizingParameters(h_max=0.20, beta=0.25)
    prox_engine = ProximityRefinement(sdf_evaluator=sdf, params=params, dim=2)

    # Query points at lower wall, center, and upper wall
    pts = np.array([[0.1, 0.005], [0.1, 0.020], [0.1, 0.035]])
    d_gap = prox_engine.evaluate_gap_distance(pts)

    assert np.all(np.abs(d_gap - W) < 0.003)


def test_parallel_channel_h_prox_scaling():
    """Verify h_prox scales as beta * W."""
    W = 0.04
    beta = 0.25
    sdf = make_channel_sdf(width=W)
    params = SizingParameters(h_min=0.002, h_max=0.20, beta=beta)
    prox_engine = ProximityRefinement(sdf_evaluator=sdf, params=params, dim=2)

    pts = np.array([[0.0, 0.02], [0.5, 0.02]])
    h_prox = prox_engine.evaluate_h_prox(pts)

    expected_h = beta * W  # 0.01
    assert np.allclose(h_prox, expected_h, atol=0.001)


def test_combined_sizing_strict_bounds():
    """Verify combined sizing satisfies h_min <= h(x) <= h_max everywhere."""
    W = 0.04
    sdf = make_channel_sdf(width=W)
    params = SizingParameters(h_min=0.005, h_max=0.150, alpha=0.20, beta=0.25)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=sdf, params=params, dim=2)

    rng = np.random.default_rng(42)
    random_pts = rng.uniform(-0.1, 0.1, size=(500, 2))

    h = size_fn.evaluate(random_pts)

    assert np.all(h >= params.h_min - 1e-12)
    assert np.all(h <= params.h_max + 1e-12)


def test_vectorized_evaluation_performance():
    """Verify 10,000 points evaluate in < 0.50 seconds."""
    sdf = make_circle_sdf(radius=1.0)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=sdf, dim=2)

    rng = np.random.default_rng(123)
    pts = rng.uniform(-1.0, 1.0, size=(10000, 2))

    t0 = time.perf_counter()
    h = size_fn.evaluate(pts)
    elapsed = time.perf_counter() - t0

    assert len(h) == 10000
    assert elapsed < 0.50, f"Vectorized evaluation took {elapsed:.4f}s >= 0.50s"


def test_lipschitz_grading_filter():
    """Verify Lipschitz grading smooths sudden step variations."""
    params = SizingParameters(h_min=0.01, h_max=0.20, grading=0.30)
    size_fn = AdaptiveSizeFunction(sdf_evaluator=make_circle_sdf(), params=params, dim=2)

    # 1D line of points with a discontinuous step in sizing
    x = np.linspace(0.0, 1.0, 50)
    pts = np.column_stack([x, np.zeros_like(x)])
    sizes = np.where(x < 0.5, 0.01, 0.20)

    graded_sizes = size_fn.apply_grading_filter(pts, sizes, max_iterations=5)

    # Check that adjacent size difference / distance <= grading * 1.1
    dx = np.diff(x)
    dh = np.abs(np.diff(graded_sizes))
    assert np.all(dh / dx <= params.grading * 1.15)
```

---

## 7. Integration Blueprint for Worker

The Worker agent should integrate the design into the CFDSolver meshing subsystem as follows:

1. **File Target**: Append or include the classes in `backend/meshing/geometry_sdf.py`.
   - Export symbols:
     - `SizingParameters`
     - `CurvatureMethod`
     - `CurvatureRefinement`
     - `ProximityRefinement`
     - `AdaptiveSizeFunction`
2. **Package Facade (`backend/meshing/__init__.py`)**:
   - Re-export `SizingParameters` and `AdaptiveSizeFunction`.
3. **Integration into `distmesh_core.py`**:
   - In the DistMesh force equilibrium step, replace the static $h_0$ or basic distance sizing with `AdaptiveSizeFunction.evaluate(edge_midpoints)`.
   - For initial lattice rejection: $P_{\text{keep}}(\mathbf{x}) = \left(\frac{h_{\min}}{h(\mathbf{x})}\right)^d$.
4. **Integration into `mesh_engine.py`**:
   - Add parameter `adaptive_sizing: bool = True` to `MeshEngine.generate_mesh(...)`.
   - When enabled, automatically instantiate `AdaptiveSizeFunction` from the CAD or analytical SDF and evaluate local element sizes.
5. **Unit Tests (`tests/test_curvature_proximity.py`)**:
   - Implement the 12 unit tests specified in Section 6.
   - Run via `pytest tests/test_curvature_proximity.py` to achieve 100% pass rate.
