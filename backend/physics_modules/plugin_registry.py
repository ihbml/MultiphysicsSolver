"""Declarative Registry[T] & ModuleSpec plugin system (Blueprint §2.2, AUD-038).

Provides:
- Generic ``Registry[T]`` with canonical IDs, alias resolution, and
  ``importlib.metadata.entry_points`` discovery.
- Declarative spec dataclasses: ``ParamSpec``, ``ModuleSpec``,
  ``BoundaryConditionSpec``, ``MaterialLaw``, ``MeshPassSpec``.
- Derivative parity checker ``verify_derivative_parity`` for ``MaterialLaw``.
- Single-source UI schema and JSON Schema (draft 2020-12) generators.
- Pre-populated global registries for physics modules, BCs, material laws, and mesh passes.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from importlib import metadata as importlib_metadata
from typing import Any, Callable, Dict, Generic, List, Optional, Sequence, Tuple, TypeVar

T = TypeVar("T")


class Registry(Generic[T]):
    """Generic declarative registry supporting canonical IDs, aliases, and entry points."""

    def __init__(self, name: str = "registry") -> None:
        self.name = name
        self._items: Dict[str, T] = {}
        self._aliases: Dict[str, str] = {}

    def register(
        self,
        item: T,
        *,
        id_attr: str = "id",
        aliases_attr: str = "aliases",
        replace: bool = False,
    ) -> T:
        """Register ``item`` by ``getattr(item, id_attr)`` and its aliases."""
        item_id = getattr(item, id_attr, None)
        if not item_id or not isinstance(item_id, str):
            raise ValueError(f"Registered item must expose a non-empty string '{id_attr}'")

        if item_id in self._items and not replace:
            raise ValueError(f"Duplicate id '{item_id}' in registry '{self.name}'")
        if item_id in self._aliases and self._aliases[item_id] != item_id and not replace:
            raise ValueError(f"ID '{item_id}' collides with an existing alias in '{self.name}'")

        aliases = tuple(getattr(item, aliases_attr, ()) or ())
        for alias in aliases:
            if not isinstance(alias, str) or not alias:
                continue
            existing = self._aliases.get(alias)
            if existing is not None and existing != item_id and not replace:
                raise ValueError(
                    f"Alias '{alias}' for '{item_id}' already maps to '{existing}'"
                )
            if alias in self._items and alias != item_id and not replace:
                raise ValueError(f"Alias '{alias}' collides with registered id '{alias}'")

        self._items[item_id] = item
        self._aliases[item_id] = item_id
        for alias in aliases:
            if isinstance(alias, str) and alias:
                self._aliases[alias] = item_id
        return item

    def resolve_id(self, key: str) -> str:
        """Resolve canonical id from canonical key or alias."""
        if key in self._items:
            return key
        if key in self._aliases:
            return self._aliases[key]
        raise KeyError(f"Key '{key}' not found in registry '{self.name}'")

    def get(self, key: str) -> T:
        """Retrieve item by canonical ID or alias."""
        return self._items[self.resolve_id(key)]

    def has(self, key: str) -> bool:
        return key in self._items or key in self._aliases

    def list_specs(self) -> List[T]:
        return list(self._items.values())

    def ids(self) -> List[str]:
        return list(self._items.keys())

    def load_entry_points(self, group: str) -> int:
        """Load plugin specs from ``importlib.metadata.entry_points(group=group)``."""
        eps = importlib_metadata.entry_points(group=group)
        count = 0
        for ep in eps:
            loaded = ep.load()
            obj = loaded() if callable(loaded) and not isinstance(loaded, type) else loaded
            items = obj if isinstance(obj, (list, tuple)) else [obj]
            for item in items:
                if not self.has(getattr(item, "id", "")):
                    self.register(item)
                    count += 1
        return count


@dataclass(frozen=True)
class ParamSpec:
    """Declarative parameter descriptor generating both UI and JSON schemas."""
    name: str
    type: str
    label: str = ""
    description: str = ""
    default: Any = None
    unit_si: str = "-"
    min_val: Optional[float] = None
    max_val: Optional[float] = None
    options: Optional[Sequence[Any]] = None
    visible_if: Optional[Dict[str, Any]] = None
    required: bool = False


@dataclass(frozen=True)
class ModuleSpec:
    """Declarative specification for a multiphysics module plugin."""
    id: str
    display_name: str
    category: str
    description: str = ""
    aliases: Tuple[str, ...] = ()
    requires: Tuple[str, ...] = ()
    conflicts: Tuple[str, ...] = ()
    provides_fields: Tuple[str, ...] = ()
    supported_solvers: Tuple[str, ...] = ("*",)
    params: Tuple[ParamSpec, ...] = ()
    variants: Tuple[str, ...] = ()
    default_variant: str = ""


@dataclass(frozen=True)
class BoundaryConditionSpec:
    """Declarative specification for a boundary condition plugin."""
    id: str
    display_name: str
    field: str = "U"
    aliases: Tuple[str, ...] = ()
    needs_wall_curvature: bool = False
    params: Tuple[ParamSpec, ...] = ()


@dataclass(frozen=True)
class MaterialLaw:
    """Declarative constitutive property law with exact analytical derivative d/dT."""
    id: str
    display_name: str
    property_name: str
    unit_si: str
    aliases: Tuple[str, ...] = ()
    evaluate: Optional[Callable[..., float]] = None
    d_dT: Optional[Callable[..., float]] = None


@dataclass(frozen=True)
class MeshPassSpec:
    """Declarative specification for a meshing pipeline pass."""
    id: str
    display_name: str
    order: int = 0
    aliases: Tuple[str, ...] = ()
    requires_passes: Tuple[str, ...] = ()


def _call_law(fn: Callable[..., float], temp_k: float, pressure_pa: float) -> float:
    """Invoke a MaterialLaw callback supporting both (T, p) and (T,) signatures."""
    try:
        return float(fn(temp_k, pressure_pa))
    except TypeError:
        return float(fn(temp_k))


def verify_derivative_parity(
    law: MaterialLaw,
    T_samples: Sequence[float] = (300.0, 500.0),
    p: float = 101325.0,
    rtol: float = 1e-5,
) -> bool:
    """Verify ``law.d_dT`` against central finite differences of ``law.evaluate``."""
    if law.evaluate is None or law.d_dT is None:
        raise ValueError(f"MaterialLaw '{law.id}' must define both evaluate and d_dT")

    for temp_k in T_samples:
        t_val = float(temp_k)
        eps = max(1e-5 * abs(t_val), 1e-6)
        analytic = _call_law(law.d_dT, t_val, p)
        f_plus = _call_law(law.evaluate, t_val + eps, p)
        f_minus = _call_law(law.evaluate, t_val - eps, p)
        fd = (f_plus - f_minus) / (2.0 * eps)
        tol = max(1e-12, rtol * max(abs(analytic), abs(fd)))
        if abs(analytic - fd) > tol:
            return False
    return True


def spec_to_ui_schema(spec: ModuleSpec) -> Dict[str, Any]:
    """Generate the frontend UI schema dictionary from a single ``ModuleSpec``."""
    params_dict: Dict[str, Any] = {}
    for p in spec.params:
        entry: Dict[str, Any] = {
            "type": p.type,
            "label": p.label or p.name,
            "description": p.description,
            "default": p.default,
            "unit_si": p.unit_si,
        }
        if p.min_val is not None:
            entry["min"] = p.min_val
        if p.max_val is not None:
            entry["max"] = p.max_val
        if p.options is not None:
            entry["options"] = list(p.options)
        if p.visible_if is not None:
            entry["visible_if"] = dict(p.visible_if)
        params_dict[p.name] = entry

    variants = list(spec.variants) if spec.variants else ["default"]
    default_variant = spec.default_variant or variants[0]

    return {
        "module_id": spec.id,
        "display_name": spec.display_name,
        "ui_category": spec.category.lower(),
        "description": spec.description,
        "aliases": list(spec.aliases),
        "variants": variants,
        "default_variant": default_variant,
        "requires_modules": list(spec.requires),
        "conflicts_modules": list(spec.conflicts),
        "provides_fields": list(spec.provides_fields),
        "supported_solvers": list(spec.supported_solvers),
        "parameters": params_dict,
    }


_PARAM_TO_JSON_TYPE = {
    "float": "number",
    "number": "number",
    "int": "integer",
    "integer": "integer",
    "bool": "boolean",
    "boolean": "boolean",
    "string": "string",
    "select": "string",
    "list": "array",
}


def spec_to_json_schema(spec: ModuleSpec) -> Dict[str, Any]:
    """Generate a JSON Schema draft 2020-12 document from a ``ModuleSpec``."""
    properties: Dict[str, Any] = {}
    required: List[str] = []

    for p in spec.params:
        prop: Dict[str, Any] = {
            "type": _PARAM_TO_JSON_TYPE.get(p.type, "string"),
            "title": p.label or p.name,
            "description": p.description,
            "x-unit-si": p.unit_si,
        }
        if p.default is not None:
            prop["default"] = p.default
        if p.min_val is not None:
            prop["minimum"] = p.min_val
        if p.max_val is not None:
            prop["maximum"] = p.max_val
        if p.options is not None:
            prop["enum"] = list(p.options)
        if p.visible_if is not None:
            prop["x-visible-if"] = dict(p.visible_if)
        properties[p.name] = prop
        if p.required:
            required.append(p.name)

    return {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": f"cfdsolver://modules/{spec.id}.schema.json",
        "title": spec.display_name,
        "description": spec.description,
        "type": "object",
        "additionalProperties": False,
        "properties": properties,
        "required": required,
    }


def _build_default_module_specs() -> List[ModuleSpec]:
    return [
        ModuleSpec(
            id="energy", display_name="Energy & Heat", category="Thermal",
            description="Thermal energy transport (enthalpy h / internal energy e, CHT coupling).",
            aliases=("Energy", "Energy & Heat", "energy_heat"),
            provides_fields=("T", "h", "thermo:Cp", "thermo:rho"),
            supported_solvers=("chtMultiRegionFoam", "buoyantPimpleFoam", "reactingFoam", "rhoPimpleFoam"),
            variants=("sensibleEnthalpy", "sensibleInternalEnergy"), default_variant="sensibleEnthalpy",
            params=(
                ParamSpec("t_ref_k", "float", "Referenztemperatur", "Thermal reference temperature T_ref", 298.15, "K", 0.0, 5000.0),
                ParamSpec("cht_coupling_mode", "select", "CHT-Kopplungsmodus", "Conjugate heat transfer coupling", "none", "-", options=("none", "conjugate_heat_transfer")),
            ),
        ),
        ModuleSpec(
            id="viscous", display_name="Viscous & Turbulence", category="Flow",
            description="Laminar and turbulent flow models (RANS, LES, DES/IDDES).",
            aliases=("Viscous", "Viscous & Turbulence", "viscous_turb"),
            provides_fields=("U", "p", "nut", "k", "omega"), supported_solvers=("*",),
            variants=("Inviscid", "Laminar", "Spalart-Allmaras", "k-epsilon", "k-omega SST", "LES WALE"),
            default_variant="k-omega SST",
            params=(
                ParamSpec("turbulence_intensity", "float", "Turbulenzintensität I", "Freestream turbulence intensity", 0.05, "-", 0.001, 0.50, visible_if={"variant_not_in": ["Inviscid", "Laminar"]}),
                ParamSpec("turbulent_length_scale_m", "float", "Turbulentes Längenmaß L", "Characteristic turbulent length scale", 0.01, "m", 1e-6, 10.0, visible_if={"variant_not_in": ["Inviscid", "Laminar"]}),
                ParamSpec("wall_treatment", "select", "Wandbehandlung", "Near-wall modeling strategy", "lowRe_yPlus1", "-", options=("lowRe_yPlus1", "nutkWallFunction"), visible_if={"variant_in": ["k-omega SST", "Spalart-Allmaras", "k-epsilon"]}),
            ),
        ),
        ModuleSpec(
            id="multiphase", display_name="Multiphase", category="Multiphase",
            description="Volume of Fluid (VOF), Euler-Euler two-phase flow, and phase change.",
            aliases=("Multiphase", "VoF"),
            provides_fields=("alpha.phase1", "alpha.phase2", "p_rgh", "gh"),
            supported_solvers=("interFoam", "interIsoFoam", "twoPhaseEulerFoam", "multiphaseEulerFoam"),
            variants=("VoF_InterfaceTracking", "Eulerian_Eulerian"), default_variant="VoF_InterfaceTracking",
            params=(
                ParamSpec("surface_tension_n_m", "float", "Oberflächenspannung sigma", "Interfacial surface tension", 0.0728, "N/m", 0.0, 5.0),
                ParamSpec("primary_phase", "string", "Primäre Phase", "Primary fluid phase name", "water", "-"),
                ParamSpec("secondary_phase", "string", "Sekundäre Phase", "Secondary fluid phase name", "air", "-"),
            ),
        ),
        ModuleSpec(
            id="species", display_name="Species & Chemistry", category="Reactions",
            description="Multi-species transport, finite-rate reaction kinetics, and catalysis.",
            aliases=("Species", "Species & Chemistry", "species_chem"), requires=("energy",),
            provides_fields=("Ydefault",),
            supported_solvers=("reactingFoam", "chtMultiRegionFoam", "rhoReactingFoam", "buoyantReactingFoam"),
            variants=("Finite-Rate / Eddy-Dissipation", "Surface Heterogeneous Kinetics / Generic Catalysis", "Inert Species Transport"),
            default_variant="Finite-Rate / Eddy-Dissipation",
            params=(
                ParamSpec("species", "list", "Speziesliste", "Transported chemical species", ["O2", "N2"], "-"),
                ParamSpec("reactions_file", "string", "Reaktionsmechanismus", "Path to reactions dictionary", "constant/reactions", "-"),
                ParamSpec("reaction_model", "select", "Reaktionskinetik", "Chemical kinetics model", "laminarArrhenius", "-", options=("laminarArrhenius", "eddyDissipationConcept", "partiallyStirredReactor", "surfaceHeterogeneousKinetics")),
            ),
        ),
        ModuleSpec(
            id="radiation", display_name="Radiation", category="Thermal",
            description="Thermal radiative transfer models (fvDOM, P1, viewFactor).",
            aliases=("Radiation",), requires=("energy",), provides_fields=("G", "qr", "qin"),
            supported_solvers=("chtMultiRegionFoam", "buoyantReactingFoam", "buoyantPimpleFoam"),
            variants=("fvDOM", "P1", "viewFactor"), default_variant="fvDOM",
        ),
        ModuleSpec(
            id="porous", display_name="Porous Media & Heat Exchanger", category="Porous",
            description="Darcy-Forchheimer porous zones and dual-cell heat exchangers.",
            aliases=("Porous Media & Heat Exchanger", "porous_hex", "Porous"),
            provides_fields=("porosity", "darcyD", "forchheimerF"),
            supported_solvers=("chtMultiRegionFoam", "simpleFoam", "pimpleFoam"),
            variants=("darcyForchheimer", "dualCellHeatExchanger"), default_variant="darcyForchheimer",
        ),
        ModuleSpec(
            id="compressible", display_name="Compressible Flow", category="Flow",
            description="High-Mach compressible Navier-Stokes and transonic/supersonic shocks.",
            aliases=("Compressible", "Compressible Flow", "compressible_flow"), requires=("energy",),
            provides_fields=("p", "U", "T", "rho", "Ma"),
            supported_solvers=("rhoSimpleFoam", "rhoPimpleFoam", "rhoCentralFoam", "sonicFoam"),
            variants=("pressureBased", "densityBased"), default_variant="pressureBased",
        ),
        ModuleSpec(
            id="cavitation", display_name="Cavitation", category="Multiphase",
            description="Phase-change cavitation mass-transfer models (Schnerr-Sauer, Kunz, Merkle).",
            aliases=("Cavitation",), requires=("multiphase",), provides_fields=("alpha.vapor", "p_rgh"),
            supported_solvers=("interPhaseChangeFoam", "interFoam"),
            variants=("SchnerrSauer", "Kunz", "Merkle"), default_variant="SchnerrSauer",
        ),
        ModuleSpec(
            id="sph_opmis", display_name="SPH / OPMIS", category="Lagrangian",
            description="Smoothed Particle Hydrodynamics / OPMIS free-surface particle solver.",
            aliases=("SPH", "OPMIS", "SPH / OPMIS", "sph"),
            provides_fields=("particlePosition", "particleVelocity", "particleDensity"),
            supported_solvers=("sphOpmisSolver",), variants=("wcsph", "isph"), default_variant="wcsph",
        ),
        ModuleSpec(
            id="dpm", display_name="Discrete Phase (DPM)", category="Lagrangian",
            description="Lagrangian particle tracking, spray break-up, and evaporation.",
            aliases=("Discrete Phase (DPM)", "dpm_lagrangian"),
            provides_fields=("lagrangian/defaultCloud/positions", "lagrangian/defaultCloud/d", "lagrangian/defaultCloud/U"),
            supported_solvers=("DPMFoam", "sprayFoam", "coalChemistryFoam"),
            variants=("coupledLagrangian", "oneWayLagrangian"), default_variant="coupledLagrangian",
        ),
        ModuleSpec(
            id="wall_film", display_name="Eulerian Wall Film (EWF)", category="Film",
            description="Thin liquid wall film transport, impingement, and phase evaporation.",
            aliases=("Eulerian Wall Film (EWF)", "wall_film_ewf", "ewf"),
            provides_fields=("filmHeight", "deltaf", "Uf", "Tf"),
            supported_solvers=("surfaceFilmFoam", "chtMultiRegionFoam", "reactingFoam"),
            variants=("kinematicThinFilm", "thermoThinFilm"), default_variant="kinematicThinFilm",
        ),
        ModuleSpec(
            id="battery", display_name="Battery Simulation", category="Electrochemical",
            description="Electrochemical-thermal equivalent circuit and thermal runaway model.",
            aliases=("Battery Simulation", "battery_thermal"), requires=("energy",),
            provides_fields=("SoC", "cellVoltage", "qJoule", "qEntropic", "qRunaway"),
            supported_solvers=("chtMultiRegionFoam", "buoyantPimpleFoam"),
            variants=("equivalentCircuitModel", "newmanP2D"), default_variant="equivalentCircuitModel",
        ),
        ModuleSpec(
            id="acoustics_fwh", display_name="Acoustics", category="Aeroacoustics",
            description="Curle analogy, Ffowcs Williams-Hawkings (FWH), and broadband noise.",
            aliases=("Acoustics", "acoustics", "fwh"),
            provides_fields=("pPrime2Mean", "acousticPower", "soundPressureLevel"),
            supported_solvers=("pimpleFoam", "rhoPimpleFoam", "icoFoam"),
            variants=("fwhAnalogy", "curleAnalogy"), default_variant="fwhAnalogy",
        ),
        ModuleSpec(
            id="mhd", display_name="Magnetohydrodynamics (MHD)", category="Electromagnetics",
            description="Coupled Lorentz force (J x B), induced currents, and plasma MHD.",
            aliases=("Magnetohydrodynamics (MHD)", "mhd_plasma", "MHD"),
            provides_fields=("B", "J", "electricPotential_V", "lorentzForce"),
            supported_solvers=("mhdFoam", "magneticFoam"),
            variants=("lorentzForceCoupled", "magneticInduction"), default_variant="lorentzForceCoupled",
        ),
        ModuleSpec(
            id="tps_ablation", display_name="Ablation & TPS", category="High-Enthalpy",
            description="Surface ablation, charring recession, and thermal protection systems.",
            aliases=("Ablation & TPS", "ablation", "tps"), requires=("energy",),
            provides_fields=("recessionRate", "ablationMassFlux", "charDensity", "virginDensity"),
            supported_solvers=("hy2Foam", "rhoCentralFoam", "reactingFoam"),
            variants=("surfaceRecessionKinetic",), default_variant="surfaceRecessionKinetic",
        ),
        ModuleSpec(
            id="monolithic_fsi", display_name="Monolithic Eulerian FSI", category="Multiphysics",
            description="One-continuum Neo-Hookean Eulerian FSI with Oldroyd-B stress transport.",
            aliases=("Monolithic Eulerian FSI", "fsi", "monolithicFSI"),
            provides_fields=("U", "p", "phi_solid", "B_tensor"),
            supported_solvers=("fsiFoam", "interFoam", "pimpleFoam", "chtMultiRegionFoam"),
            variants=("neoHookeanEulerian",), default_variant="neoHookeanEulerian",
        ),
    ]


def _build_default_bc_specs() -> List[BoundaryConditionSpec]:
    return [
        BoundaryConditionSpec("noSlip", "No-Slip Wall", field="U", aliases=("no_slip", "wall_noslip")),
        BoundaryConditionSpec("fixedValue", "Fixed Value (Dirichlet)", field="*", aliases=("fixed_value", "dirichlet"), params=(ParamSpec("value", "string", "Value", "Uniform boundary value", "uniform 0", "-"),)),
        BoundaryConditionSpec("zeroGradient", "Zero Gradient (Neumann)", field="*", aliases=("zero_gradient", "neumann")),
        BoundaryConditionSpec("inletOutlet", "Inlet-Outlet", field="*", aliases=("inlet_outlet",)),
        BoundaryConditionSpec("pressureInletOutletVelocity", "Pressure Inlet-Outlet Velocity", field="U"),
        BoundaryConditionSpec("symmetry", "Symmetry Plane", field="*", aliases=("symmetryPlane",)),
        BoundaryConditionSpec("curvatureCorrectedSlipWall", "Curvature-Corrected Slip Wall", field="U", aliases=("curvature_corrected_slip_wall", "slip_wall_curvature"), needs_wall_curvature=True, params=(ParamSpec("slip_length_m", "float", "Slip Length", "Navier slip length", 0.0, "m", 0.0, 1.0),)),
    ]


def _build_default_material_laws() -> List[MaterialLaw]:
    as_air, ts_air, r_air, k0_air, alpha_k, t_ref_k = 1.458e-6, 110.4, 287.058, 0.026, 7.0e-5, 300.0
    return [
        MaterialLaw(
            id="sutherland_air_viscosity", display_name="Sutherland Air Viscosity",
            property_name="dynamic_viscosity", unit_si="Pa*s", aliases=("sutherland_air", "mu_sutherland_air"),
            evaluate=lambda T, p=101325.0: (as_air * (T ** 1.5)) / (T + ts_air),
            d_dT=lambda T, p=101325.0: as_air * (T ** 0.5) * (0.5 * T + 1.5 * ts_air) / ((T + ts_air) ** 2),
        ),
        MaterialLaw(
            id="ideal_gas_air_density", display_name="Ideal Gas Air Density",
            property_name="density", unit_si="kg/m^3", aliases=("ideal_gas_air", "rho_ideal_gas_air"),
            evaluate=lambda T, p=101325.0: p / (r_air * T),
            d_dT=lambda T, p=101325.0: -p / (r_air * (T ** 2)),
        ),
        MaterialLaw(
            id="linear_thermal_conductivity", display_name="Linear Thermal Conductivity",
            property_name="thermal_conductivity", unit_si="W/(m*K)", aliases=("linear_k_air",),
            evaluate=lambda T, p=101325.0: k0_air + alpha_k * (T - t_ref_k),
            d_dT=lambda T, p=101325.0: alpha_k,
        ),
    ]


def _build_default_mesh_passes() -> List[MeshPassSpec]:
    return [
        MeshPassSpec("surface_audit", "Surface Geometry Audit", order=10, aliases=("audit",)),
        MeshPassSpec("octree_hexcore", "Octree Hexcore Generation", order=20, aliases=("hexcore", "octree"), requires_passes=("surface_audit",)),
        MeshPassSpec("boundary_layer_inflation", "Boundary Layer Prism Inflation", order=30, aliases=("inflation", "prism_layers"), requires_passes=("octree_hexcore",)),
        MeshPassSpec("difvm_topology", "DiFVM Topology Assembly", order=40, aliases=("topology", "difvm"), requires_passes=("octree_hexcore",)),
        MeshPassSpec("quality_optimization", "Mesh Quality & AMIPS Smoothing", order=50, aliases=("quality", "smoothing"), requires_passes=("difvm_topology",)),
    ]


_REGISTRIES: Dict[str, Registry[Any]] = {}


def _get_or_init_registry(key: str, builder: Callable[[], List[Any]]) -> Registry[Any]:
    reg = _REGISTRIES.get(key)
    if reg is None:
        reg = Registry(key)
        for item in builder():
            reg.register(item)
        _REGISTRIES[key] = reg
    return reg


def get_module_spec_registry() -> Registry[ModuleSpec]:
    return _get_or_init_registry("physics_modules", _build_default_module_specs)


def get_bc_registry() -> Registry[BoundaryConditionSpec]:
    return _get_or_init_registry("boundary_conditions", _build_default_bc_specs)


def get_material_law_registry() -> Registry[MaterialLaw]:
    return _get_or_init_registry("material_laws", _build_default_material_laws)


def get_mesh_pass_registry() -> Registry[MeshPassSpec]:
    return _get_or_init_registry("mesh_passes", _build_default_mesh_passes)
