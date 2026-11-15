"""Monolithic 3D Eulerian Fluid-Structure Interaction (FSI) Module (Paper 8).

Implements the monolithic Eulerian FSI framework from FSI_Monolithic_2505.22920v1.pdf:
- One-continuum mixture momentum formulation:
    rho = phi * rho^s + (1 - phi) * rho^f
    sigma_{ij} = phi * sigma_{ij}^s + (1 - phi) * sigma_{ij}^f
- Incompressible Neo-Hookean solid elasticity with Oldroyd-B upper-convected transport:
    partial_t B_{ij} + u_k partial_k B_{ij} = B_{ik} partial_k u_j + B_{kj} partial_k u_i
- Solid Cauchy stress:
    sigma_{ij}^s = 2 * mu^s * D_{ij} + G^s * (B_{ij} - delta_{ij})
- 3D Piecewise Linear Interface Calculation (PLIC) geometric interface reconstruction.
- Crucial fluid deformation stability reset:
    B(x) <- I_3 whenever solid volume fraction phi(x) < 1e-8.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Tuple, Union
import numpy as np

from backend.physics_modules.base_module import BasePhysicsModule


class MonolithicFSIModule(BasePhysicsModule):
    """OpenFOAM-compatible physics module registration for Monolithic Eulerian FSI."""

    def __init__(self) -> None:
        super().__init__(
            name="Monolithic Eulerian FSI",
            category="Multiphysics",
            description="Löst monolithische 3D Eulerian Fluid-Struktur-Interaktion mit Neo-Hooke-Festkörper und Oldroyd-B-Transport.",
        )
        self.config.enabled = False
        self.config.model_variant = "neoHookeanEulerian"
        self.config.parameters = {
            "rho_solid": 1000.0,
            "rho_fluid": 1000.0,
            "mu_solid": 1e-3,
            "mu_fluid": 1e-3,
            "shear_modulus_G": 1e4,
            "fluid_reset_threshold": 1e-8,
        }

    def get_supported_solvers(self) -> List[str]:
        return ["fsiFoam", "interFoam", "pimpleFoam", "chtMultiRegionFoam"]

    def get_required_fields(self) -> List[str]:
        return ["U", "p", "phi_solid", "B_tensor"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        p = self.config.parameters
        return {
            "constant/fsiProperties": (
                "fsiModel neoHookeanEulerian;\n\n"
                f"rhoSolid {p.get('rho_solid', 1000.0)};\n"
                f"rhoFluid {p.get('rho_fluid', 1000.0)};\n"
                f"muSolid  {p.get('mu_solid', 1e-3)};\n"
                f"muFluid  {p.get('mu_fluid', 1e-3)};\n"
                f"shearModulusG {p.get('shear_modulus_G', 1e4)};\n"
                f"fluidResetThreshold {p.get('fluid_reset_threshold', 1e-8)};\n"
            )
        }


class EulerianMonolithicFSI:
    """Eulerian Monolithic 3D Fluid-Structure Interaction Engine (Paper 8)."""

    def __init__(
        self,
        rho_s: float = 1000.0,
        rho_f: float = 1000.0,
        mu_s: float = 1e-3,
        mu_f: float = 1e-3,
        G_s: float = 1e4,
        eps_cutoff: float = 1e-8,
    ) -> None:
        """Initialize Eulerian Monolithic FSI engine.

        Parameters
        ----------
        rho_s : float
            Solid density in kg/m^3.
        rho_f : float
            Fluid density in kg/m^3.
        mu_s : float
            Solid artificial viscous damping in Pa.s.
        mu_f : float
            Fluid dynamic viscosity in Pa.s.
        G_s : float
            Solid shear modulus G^s in Pa.
        eps_cutoff : float
            Cutoff volume fraction threshold below which B is reset to identity.
        """
        if rho_s <= 0 or rho_f <= 0:
            raise ValueError("Densities must be strictly positive.")
        if G_s <= 0:
            raise ValueError("Solid shear modulus G_s must be strictly positive.")

        self.rho_s = float(rho_s)
        self.rho_f = float(rho_f)
        self.mu_s = float(mu_s)
        self.mu_f = float(mu_f)
        self.G_s = float(G_s)
        self.eps_cutoff = float(eps_cutoff)

    def compute_mixture_density(
        self,
        phi: Union[float, np.ndarray],
    ) -> Union[float, np.ndarray]:
        """Compute one-continuum mixture density rho = phi * rho^s + (1 - phi) * rho^f."""
        phi_arr = np.clip(np.asarray(phi, dtype=np.float64), 0.0, 1.0)
        return phi_arr * self.rho_s + (1.0 - phi_arr) * self.rho_f

    def compute_mixture_stress(
        self,
        phi: Union[float, np.ndarray],
        u_grad: np.ndarray,
        p: Union[float, np.ndarray],
        B: np.ndarray,
    ) -> np.ndarray:
        """Evaluate unified mixture momentum stress tensor sigma_{ij} (Paper 8 Eq. 7-14).

        sigma_{ij} = phi * sigma_{ij}^s + (1 - phi) * sigma_{ij}^f
        sigma_{ij}^s = 2 * mu^s * D_{ij} + G^s * (B_{ij} - delta_{ij})
        sigma_{ij}^f = 2 * mu^f * D_{ij} - p * delta_{ij}
        where D_{ij} = (1/2) * (partial_j u_i + partial_i u_j).

        Parameters
        ----------
        phi : Union[float, np.ndarray]
            Solid volume fraction phi in [0, 1].
        u_grad : np.ndarray, shape (3, 3) or (N, 3, 3)
            Velocity gradient tensor grad(u).
        p : Union[float, np.ndarray]
            Pressure scalar.
        B : np.ndarray, shape (3, 3) or (N, 3, 3)
            Left Cauchy-Green deformation tensor B.

        Returns
        -------
        sigma : np.ndarray, shape (3, 3) or (N, 3, 3)
            Unified mixture Cauchy stress tensor.
        """
        phi_val = np.atleast_1d(np.clip(np.asarray(phi, dtype=np.float64), 0.0, 1.0))
        u_g = np.asarray(u_grad, dtype=np.float64)
        p_val = np.atleast_1d(np.asarray(p, dtype=np.float64))
        B_mat = np.asarray(B, dtype=np.float64)

        is_single = (u_g.ndim == 2)
        if is_single:
            u_g = u_g.reshape(1, 3, 3)
            B_mat = B_mat.reshape(1, 3, 3)

        N = len(u_g)

        if len(phi_val) == 1 and N > 1:
            phi_val = np.broadcast_to(phi_val, (N,))
        elif len(phi_val) != N:
            raise ValueError(f"phi length {len(phi_val)} does not match velocity gradient batch size {N}")

        if len(p_val) == 1 and N > 1:
            p_val = np.broadcast_to(p_val, (N,))
        elif len(p_val) != N:
            raise ValueError(f"pressure p length {len(p_val)} does not match batch size {N}")

        if B_mat.ndim == 2 and N > 1:
            B_mat = np.tile(B_mat, (N, 1, 1))
        elif len(B_mat) != N:
            raise ValueError(f"B tensor batch size {len(B_mat)} does not match velocity gradient batch size {N}")

        sigma = np.zeros((N, 3, 3), dtype=np.float64)
        I3 = np.eye(3, dtype=np.float64)

        for i in range(N):
            # Symmetric strain rate tensor D
            D = 0.5 * (u_g[i] + u_g[i].T)
            ph = float(phi_val[i])
            press = float(p_val[i])
            b_cell = B_mat[i]

            # Incompressible Neo-Hookean solid stress (Paper 8 Eq. 14)
            sigma_s = 2.0 * self.mu_s * D + self.G_s * (b_cell - I3)
            # Fluid stress
            sigma_f = 2.0 * self.mu_f * D - press * I3

            # One-continuum linear blend
            sigma[i] = ph * sigma_s + (1.0 - ph) * sigma_f

        return sigma[0] if is_single else sigma

    def apply_fluid_stability_reset(
        self,
        B: np.ndarray,
        phi: Union[float, np.ndarray],
    ) -> np.ndarray:
        """Apply crucial stability reset B <- I where solid fraction phi < 1e-8 (Paper 8 §3.2).

        Stops unbounded growth of B in sheared fluid regions.

        Parameters
        ----------
        B : np.ndarray, shape (3, 3) or (N, 3, 3)
            Left Cauchy-Green deformation tensor array.
        phi : Union[float, np.ndarray], shape () or (N,)
            Solid volume fraction.

        Returns
        -------
        B_reset : np.ndarray
            Updated tensor array with B = I in pure fluid cells.
        """
        B_out = np.asarray(B, dtype=np.float64).copy()
        is_single = (B_out.ndim == 2)
        if is_single:
            B_out = B_out.reshape(1, 3, 3)

        N = len(B_out)
        phi_arr = np.atleast_1d(np.asarray(phi, dtype=np.float64))
        if len(phi_arr) == 1 and N > 1:
            phi_arr = np.broadcast_to(phi_arr, (N,))
        elif len(phi_arr) != N:
            raise ValueError(f"phi length {len(phi_arr)} does not match tensor batch size {N}")

        I3 = np.eye(3, dtype=np.float64)
        fluid_mask = (phi_arr < self.eps_cutoff)

        for i in range(N):
            if fluid_mask[i]:
                B_out[i] = I3

        return B_out[0] if is_single else B_out

    def advect_cauchy_green_tensor(
        self,
        B: np.ndarray,
        u: np.ndarray,
        u_grad: np.ndarray,
        dt: float,
        phi: Optional[Union[float, np.ndarray]] = None,
    ) -> np.ndarray:
        """Advect left Cauchy-Green tensor B via upper-convected Oldroyd-B transport (Paper 8 Eq. 16).

        Solves:
            partial_t B_{ij} + u_k partial_k B_{ij} = B_{ik} partial_k u_j + B_{kj} partial_k u_i

        Includes automatic stability reset B = I wherever phi < 1e-8.

        Parameters
        ----------
        B : np.ndarray, shape (3, 3) or (N, 3, 3)
            Tensor at previous time step.
        u : np.ndarray, shape (3,) or (N, 3)
            Velocity vector.
        u_grad : np.ndarray, shape (3, 3) or (N, 3, 3)
            Velocity gradient tensor.
        dt : float
            Time step size.
        phi : Optional[np.ndarray]
            Solid volume fraction.

        Returns
        -------
        B_next : np.ndarray
            Updated Left Cauchy-Green tensor.
        """
        if dt <= 0.0:
            raise ValueError(f"Time step dt must be positive, got {dt}")

        B_in = np.asarray(B, dtype=np.float64)
        u_g = np.asarray(u_grad, dtype=np.float64)

        is_single = (B_in.ndim == 2)
        if is_single:
            B_in = B_in.reshape(1, 3, 3)
            u_g = u_g.reshape(1, 3, 3)

        N = len(B_in)
        B_next = np.zeros_like(B_in)

        for i in range(N):
            b_mat = B_in[i]
            grad_u = u_g[i]

            # Upper-convected deformation production term:
            # P_{ij} = B_{ik} partial_k u_j + B_{kj} partial_k u_i = B . (grad u) + (grad u)^T . B
            P = b_mat @ grad_u + grad_u.T @ b_mat

            # Forward Euler step for production (advection handled across grid faces)
            B_next[i] = b_mat + dt * P

            # Symmetrize to prevent numerical asymmetry
            B_next[i] = 0.5 * (B_next[i] + B_next[i].T)

        if phi is not None:
            B_next = self.apply_fluid_stability_reset(B_next, phi)

        return B_next[0] if is_single else B_next

    @staticmethod
    def reconstruct_plic_3d(
        phi_cell: float,
        phi_grad: np.ndarray,
        dx: float = 1.0,
    ) -> Tuple[np.ndarray, float]:
        """3D Piecewise Linear Interface Calculation (PLIC) normal and plane constant (Paper 8 §3.3).

        Interface plane: m . x = beta with m = grad(phi) / ||grad(phi)||.

        Parameters
        ----------
        phi_cell : float
            Solid volume fraction in the cell in [0, 1].
        phi_grad : np.ndarray, shape (3,)
            Gradient vector of solid volume fraction.
        dx : float
            Cell dimension.

        Returns
        -------
        normal : np.ndarray, shape (3,)
            Unit interface normal vector pointing into solid.
        beta : float
            Plane intercept parameter.
        """
        phi_val = float(np.clip(phi_cell, 0.0, 1.0))
        g = np.asarray(phi_grad, dtype=np.float64)
        norm_g = float(np.linalg.norm(g))

        if norm_g < 1e-12:
            # Uniform field: arbitrary normal
            n = np.array([1.0, 0.0, 0.0], dtype=np.float64)
        else:
            n = g / norm_g

        # Plane intercept beta in [-0.5 * dx, 0.5 * dx]
        # In 1D approximation along normal direction:
        beta = (phi_val - 0.5) * dx
        return n, float(beta)
