"""
registry.py
===========
Zentrales Registry-System für alle physikalischen Module des CFD-Solvers.
Registriert Standardmodule und erlaubt dynamisches Aktivieren/Deaktivieren.
"""

from __future__ import annotations
from typing import Any, Dict, List, Optional, Union
from .base_module import BasePhysicsModule
from .monolithic_fsi import MonolithicFSIModule
from .plugin_registry import get_module_spec_registry


class EnergyModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Energy",
            category="Thermal",
            description="Löst die thermische Energiegleichung (Enthalpie h / interne Energie e, CHT-Kopplung)."
        )
        self.config.enabled = True
        self.config.model_variant = "sensibleEnthalpy"

    def get_supported_solvers(self) -> List[str]:
        return ["chtMultiRegionFoam", "buoyantPimpleFoam", "reactingFoam", "rhoPimpleFoam"]

    def get_required_fields(self) -> List[str]:
        return ["T", "h", "thermo:Cp", "thermo:rho"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/thermophysicalProperties": (
                "thermoType\n{\n    type heRhoThermo;\n    mixture pureMixture;\n"
                "    transport const;\n    thermo hConst;\n    equationOfState perfectGas;\n"
                "    specie specie;\n    energy sensibleEnthalpy;\n}"
            )
        }

    def get_ui_schema(self) -> Dict[str, Any]:
        return {
            "module_id": "Energy",
            "display_name": "Energy & Heat",
            "ui_category": "thermal",
            "description": self.description,
            "variants": ["sensibleEnthalpy", "sensibleInternalEnergy"],
            "default_variant": "sensibleEnthalpy",
            "requires_modules": [],
            "parameters": {
                "t_ref_k": {
                    "type": "float",
                    "label": "Referenztemperatur",
                    "description": "Thermische Referenztemperatur T_ref",
                    "default": 298.15,
                    "min": 0.0,
                    "max": 5000.0,
                    "unit_si": "K",
                    "display_unit_default": "K",
                },
                "cht_coupling_mode": {
                    "type": "select",
                    "label": "CHT-Kopplungsmodus",
                    "description": "Thermische Kopplung für Festkörperzonen",
                    "options": ["none", "conjugate_heat_transfer"],
                    "default": "none",
                },
            },
        }


class ViscousTurbulenceModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Viscous & Turbulence",
            category="Flow",
            description="Laminare und turbulente Strömungsmodelle (RANS: k-omega SST, k-epsilon, SA; LES: WALE, Smagorinsky; DES/IDDES)."
        )
        self.config.enabled = True
        self.config.model_variant = "kOmegaSST"

    def get_supported_solvers(self) -> List[str]:
        return ["*"]  # Universal

    def get_required_fields(self) -> List[str]:
        return ["U", "p", "nut", "k", "omega"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        _ = case_dir
        variant = str(self.config.model_variant).strip()
        v_low = variant.lower()
        if v_low in ("laminar", "inviscid"):
            body = "simulationType laminar;\n"
        elif v_low.startswith("les") or v_low.startswith("des") or v_low.startswith("iddes"):
            if "iddes" in v_low:
                les_model = "SpalartAllmarasIDDES"
            elif "des" in v_low:
                les_model = "SpalartAllmarasDES"
            elif "smagorinsky" in v_low:
                les_model = "Smagorinsky"
            elif "keqn" in v_low:
                les_model = "kEqn"
            elif "wale" in v_low:
                les_model = "WALE"
            else:
                les_model = variant[3:].strip() or "WALE"
            body = (
                f"simulationType  LES;\nLES\n{{\n    LESModel {les_model};\n"
                "    turbulence on;\n    printCoeffs on;\n"
                "    delta cubeRootVol;\n}"
            )
        else:
            ras_model = {
                "k-omega sst": "kOmegaSST",
                "k-epsilon": "kEpsilon",
                "spalart-allmaras": "SpalartAllmaras",
            }.get(v_low, variant)
            body = (
                f"simulationType RAS;\nRAS\n{{\n    RASModel {ras_model};\n"
                "    turbulence on;\n    printCoeffs on;\n}"
            )
        return {"constant/turbulenceProperties": body}

    def get_ui_schema(self) -> Dict[str, Any]:
        return {
            "module_id": "Viscous",
            "display_name": "Viscous & Turbulence",
            "ui_category": "flow_turbulence",
            "description": self.description,
            "variants": ["Inviscid", "Laminar", "Spalart-Allmaras", "k-epsilon", "k-omega SST", "LES WALE"],
            "default_variant": "k-omega SST",
            "requires_modules": [],
            "parameters": {
                "turbulence_intensity": {
                    "type": "float",
                    "label": "Turbulenzintensität I",
                    "description": "Freistrom-Turbulenzgrad am Einlass",
                    "default": 0.05,
                    "min": 0.001,
                    "max": 0.50,
                    "unit_si": "-",
                    "display_unit_default": "%",
                    "visible_if": {"variant_not_in": ["Inviscid", "Laminar"]},
                },
                "turbulent_length_scale_m": {
                    "type": "float",
                    "label": "Turbulentes Längenmaß L",
                    "description": "Charakteristisches turbulentes Längenmaß",
                    "default": 0.01,
                    "min": 1e-6,
                    "max": 10.0,
                    "unit_si": "m",
                    "display_unit_default": "m",
                    "visible_if": {"variant_not_in": ["Inviscid", "Laminar"]},
                },
                "wall_treatment": {
                    "type": "select",
                    "label": "Wandbehandlung",
                    "description": "Auflösung der laminaren Unterschicht vs. Wandfunktionen",
                    "options": ["lowRe_yPlus1", "nutkWallFunction"],
                    "default": "lowRe_yPlus1",
                    "visible_if": {"variant_in": ["k-omega SST", "Spalart-Allmaras", "k-epsilon"]},
                },
            },
        }


class MultiphaseModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Multiphase",
            category="Multiphase",
            description="Phasenströmungen: Volume of Fluid (VOF), Euler-Euler Zweiphasenströmung, Kavitation und Phasenumwandlung."
        )
        self.config.model_variant = "VoF_InterfaceTracking"

    def get_supported_solvers(self) -> List[str]:
        return ["interFoam", "interIsoFoam", "twoPhaseEulerFoam", "multiphaseEulerFoam"]

    def get_required_fields(self) -> List[str]:
        return ["alpha.phase1", "alpha.phase2", "p_rgh", "gh"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/transportProperties": (
                "phases (water air);\nwater\n{\n    transportModel Newtonian;\n    nu 1e-06;\n    rho 1000;\n}\n"
                "air\n{\n    transportModel Newtonian;\n    nu 1.48e-05;\n    rho 1;\n}\nsigma 0.07;\n"
            )
        }

    def get_ui_schema(self) -> Dict[str, Any]:
        return {
            "module_id": "Multiphase",
            "display_name": "Multiphase",
            "ui_category": "multiphase",
            "description": self.description,
            "variants": ["VoF_InterfaceTracking", "Eulerian_Eulerian"],
            "default_variant": "VoF_InterfaceTracking",
            "requires_modules": [],
            "parameters": {
                "surface_tension_n_m": {
                    "type": "float",
                    "label": "Oberflächenspannung sigma",
                    "default": 0.0728,
                    "unit_si": "N/m",
                },
                "primary_phase": {
                    "type": "string",
                    "label": "Primäre Phase",
                    "default": "water",
                },
                "secondary_phase": {
                    "type": "string",
                    "label": "Sekundäre Phase",
                    "default": "air",
                },
            },
        }


class SpeciesChemistryModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Species & Chemistry",
            category="Reactions",
            description="Mehrkomponententransport, Finite-Rate-Reaktionskinetik, heterogene Oberflächenkatalyse und Verbrennung."
        )
        self.config.model_variant = "finiteRateKinetics"
        self.config.parameters = {
            "species": ["Ydefault"],
            "reaction_model": "finiteRateKinetics",
            "reactions_file": "constant/reactions",
            "thermo_file": "constant/thermophysicalProperties",
        }

    def get_supported_solvers(self) -> List[str]:
        return ["reactingFoam", "chtMultiRegionFoam", "rhoReactingFoam", "buoyantReactingFoam"]

    def get_required_fields(self) -> List[str]:
        species_list = self.config.parameters.get("species", ["Ydefault"])
        return list(species_list)

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        species_list = self.config.parameters.get("species", ["Ydefault"])
        specie_names = " ".join(species_list)
        return {
            "constant/chemistryProperties": (
                "chemistryType\n{\n    method ode;\n    solver EulerImplicit;\n}\n"
                "chemistry on;\ninitialChemicalTimeStep 1e-07;\n"
            ),
            "constant/thermophysicalProperties": (
                "thermoType\n{\n"
                "    type            heRhoThermo;\n"
                "    mixture         multiComponentMixture;\n"
                "    transport       sutherland;\n"
                "    thermo          janaf;\n"
                "    energy          sensibleEnthalpy;\n"
                "    equationOfState perfectGas;\n"
                "    specie          specie;\n"
                "}\n\n"
                f"species\n(\n    {specie_names}\n);\n"
            ),
        }

    def validate_dependencies(self, active_modules: Dict[str, BasePhysicsModule]) -> List[str]:
        warnings = []
        if not active_modules.get("Energy", None) or not active_modules["Energy"].config.enabled:
            warnings.append("Species & Chemistry erfordert aktives Energy-Modul zur Enthalpie- und Temperaturberechnung.")
        return warnings

    def get_ui_schema(self) -> Dict[str, Any]:
        return {
            "module_id": "Species",
            "display_name": "Species & Chemistry",
            "ui_category": "reactions",
            "description": self.description,
            "variants": ["Finite-Rate / Eddy-Dissipation", "Surface Heterogeneous Kinetics / Generic Catalysis", "Inert Species Transport"],
            "default_variant": "Finite-Rate / Eddy-Dissipation",
            "requires_modules": ["Energy"],
            "parameters": {
                "species": {
                    "type": "list",
                    "label": "Speziesliste",
                    "description": "Transportierte chemische Spezies",
                    "default": ["O2", "N2"],
                    "unit_si": "-",
                },
                "reactions_file": {
                    "type": "string",
                    "label": "Reaktionsmechanismus",
                    "description": "Pfad zur Chemkin- / OpenFOAM-Reaktionsdatei",
                    "default": "constant/reactions",
                },
                "reaction_model": {
                    "type": "select",
                    "label": "Reaktionskinetik",
                    "options": ["laminarArrhenius", "eddyDissipationConcept", "partiallyStirredReactor", "surfaceHeterogeneousKinetics"],
                    "default": "laminarArrhenius",
                },
            },
        }


class DpmLagrangianModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Discrete Phase (DPM)",
            category="Lagrangian",
            description="Diskrete Partikelverfolgung (Lagrange), Tropfenzerstäubung (Spray Break-up), Verdunstung und Partikel-Kollision."
        )
        self.config.model_variant = "coupledLagrangian"

    def get_supported_solvers(self) -> List[str]:
        return ["DPMFoam", "sprayFoam", "coalChemistryFoam"]

    def get_required_fields(self) -> List[str]:
        return ["lagrangian/defaultCloud/positions", "lagrangian/defaultCloud/d", "lagrangian/defaultCloud/U"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/kinematicCloudProperties": (
                "solution\n{\n    active true;\n    coupled true;\n    cellValueSourceCorrection on;\n"
                "    maxCo 0.3;\n    interpolationSchemes\n    {\n        cell cell;\n        cellPoint cellPoint;\n    }\n}"
            )
        }


class EulerianWallFilmModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Eulerian Wall Film (EWF)",
            category="Film",
            description="Flüssigkeitsfilm auf Wandflächen: Abscheidung, Wandreibung, Gravitationsabfluss, Filmentgasung und Verdampfung."
        )
        self.config.model_variant = "kinematicThinFilm"

    def get_supported_solvers(self) -> List[str]:
        return ["surfaceFilmFoam", "chtMultiRegionFoam", "reactingFoam"]

    def get_required_fields(self) -> List[str]:
        return ["filmHeight", "deltaf", "Uf", "Tf"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/surfaceFilmProperties": (
                "surfaceFilmModel kinematicThinFilm;\n"
                "kinematicThinFilmCoeffs\n{\n    viscosityModel constant;\n    filmViscosity 0.001;\n"
                "    density 1000;\n}\n"
            )
        }


class RadiationModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Radiation",
            category="Thermal",
            description="Thermische Strahlungstransport-Modelle: fvDOM (Discrete Ordinates Method), P1-Approximation, viewFactor."
        )
        self.config.model_variant = "fvDOM"

    def get_supported_solvers(self) -> List[str]:
        return ["chtMultiRegionFoam", "buoyantReactingFoam", "buoyantPimpleFoam"]

    def get_required_fields(self) -> List[str]:
        return ["G", "qr", "qin"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/radiationProperties": (
                "radiation on;\nradiationModel fvDOM;\n"
                "fvDOMCoeffs\n{\n    nPhi 3;\n    nTheta 3;\n    convergence 1e-4;\n    maxIter 10;\n}\n"
                "absorptionEmissionModel constantAbsorptionEmission;\n"
                "constantAbsorptionEmissionCoeffs\n{\n    a 0.5;\n    e 0.5;\n    E 0;\n}\n"
            )
        }


class AcousticsModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Acoustics",
            category="Aeroacoustics",
            description="Strömungsakustik & Schallemissionen: Curle-Analogie, Ffowcs Williams-Hawkings (FWH), Breitband-Lärmquellen."
        )
        self.config.model_variant = "fwhAnalogy"

    def get_supported_solvers(self) -> List[str]:
        return ["pimpleFoam", "rhoPimpleFoam", "icoFoam"]

    def get_required_fields(self) -> List[str]:
        return ["pPrime2Mean", "acousticPower", "soundPressureLevel"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "system/acousticProperties": (
                "type noise;\nlibs (\"libfieldFunctionObjects.so\");\n"
                "writeControl timeStep;\nwriteInterval 1;\n"
                "windowModel Hanning;\n"
            )
        }


class AblationModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Ablation & TPS",
            category="High-Enthalpy",
            description="Oberflächenablation, chemische Erosion, Sublimation und Hitzeschutz (TPS) bei Hochtemperatur- und Wiedereintrittsströmungen."
        )
        self.config.model_variant = "surfaceRecessionKinetic"

    def get_supported_solvers(self) -> List[str]:
        return ["hy2Foam", "rhoCentralFoam", "reactingFoam"]

    def get_required_fields(self) -> List[str]:
        return ["recessionRate", "ablationMassFlux", "charDensity", "virginDensity"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/ablationProperties": (
                "ablationModel ArrheniusSurfaceAblation;\n"
                "ArrheniusSurfaceAblationCoeffs\n{\n    A_pre 1.2e5;\n    Ea 1.8e5;\n    molarMassChar 0.012;\n}\n"
            )
        }


class BatteryThermalModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Battery Simulation",
            category="Electrochemical",
            description="Elektrochemisch-thermisches Zellmodell: Equivalent Circuit Model (ECM), Joule-Wärme, Entropiewärme und thermisches Durchgehen."
        )
        self.config.model_variant = "equivalentCircuitModel"

    def get_supported_solvers(self) -> List[str]:
        return ["chtMultiRegionFoam", "buoyantPimpleFoam"]

    def get_required_fields(self) -> List[str]:
        return ["SoC", "cellVoltage", "qJoule", "qEntropic", "qRunaway"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/batteryProperties": (
                "batteryModel EquivalentCircuit2RC;\n"
                "EquivalentCircuit2RCCoeffs\n{\n    nominalCapacityAh 5.0;\n    R0 0.015;\n    R1 0.010;\n    C1 1500;\n    R2 0.008;\n    C2 3000;\n}\n"
            )
        }


class MhdPlasmaModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Magnetohydrodynamics (MHD)",
            category="Electromagnetics",
            description="Kopplung von Strömung und Magnetfeldern: Lorentz-Kraft (J x B), induzierte Ströme, elektrische Leitfähigkeit und Lichtbogen/Plasma."
        )
        self.config.model_variant = "lorentzForceCoupled"

    def get_supported_solvers(self) -> List[str]:
        return ["mhdFoam", "magneticFoam"]

    def get_required_fields(self) -> List[str]:
        return ["B", "J", "electricPotential_V", "lorentzForce"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/mhdProperties": (
                "magneticPermeability 1.2566e-06;\n"
                "electricalConductivity 3.5e07;\n"
                "applyLorentzForce on;\n"
            )
        }


class PorousHexModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Porous Media & Heat Exchanger",
            category="Porous",
            description="Poröse Zonen nach Darcy-Forchheimer, Wabenkörper, Katalysatorbetten und Dual-Cell Wärmetauscher."
        )
        self.config.model_variant = "darcyForchheimer"

    def get_supported_solvers(self) -> List[str]:
        return ["chtMultiRegionFoam", "simpleFoam", "pimpleFoam"]

    def get_required_fields(self) -> List[str]:
        return ["porosity", "darcyD", "forchheimerF"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/porousZoneProperties": (
                "porousZone\n{\n    coordinateSystem\n    {\n        type cartesian;\n    }\n"
                "    Darcy (1e7 1e7 1e7);\n    Forchheimer (100 100 100);\n}\n"
            )
        }


class ModuleRegistry:
    """Zentrale Schaltstelle für alle Physikmodule."""

    def __init__(self):
        self._modules: Dict[str, BasePhysicsModule] = {}
        self._register_defaults()

    def _register_defaults(self):
        modules = [
            EnergyModule(),
            ViscousTurbulenceModule(),
            MultiphaseModule(),
            SpeciesChemistryModule(),
            DpmLagrangianModule(),
            EulerianWallFilmModule(),
            RadiationModule(),
            AcousticsModule(),
            AblationModule(),
            BatteryThermalModule(),
            MhdPlasmaModule(),
            PorousHexModule(),
            MonolithicFSIModule(),
        ]
        for m in modules:
            self._modules[m.name] = m

    def _resolve_module_name(self, name: str) -> Optional[str]:
        """Resolve an exact module key or declarative ModuleSpec alias to ``self._modules``."""
        if name in self._modules:
            return name
        spec_reg = get_module_spec_registry()
        if spec_reg.has(name):
            spec = spec_reg.get(name)
            candidates = (spec.display_name, spec.id, *spec.aliases)
            for cand in candidates:
                if cand in self._modules:
                    return cand
        for mod_key, mod in self._modules.items():
            ui = mod.get_ui_schema()
            if ui.get("module_id") == name or ui.get("display_name") == name:
                return mod_key
        return None

    def get_module(self, name: str) -> Optional[BasePhysicsModule]:
        resolved = self._resolve_module_name(name)
        return self._modules.get(resolved) if resolved is not None else None

    def get_all_modules(self) -> Dict[str, BasePhysicsModule]:
        return self._modules

    def enable_module(self, name: str, variant: Optional[str] = None):
        resolved = self._resolve_module_name(name)
        if resolved is not None:
            self._modules[resolved].config.enabled = True
            if variant:
                self._modules[resolved].config.model_variant = variant

    def disable_module(self, name: str):
        resolved = self._resolve_module_name(name)
        if resolved is not None:
            self._modules[resolved].config.enabled = False

    def validate_configuration(self) -> List[str]:
        warnings = []
        for mod in self._modules.values():
            if mod.config.enabled:
                warnings.extend(mod.validate_dependencies(self._modules))

        spec_reg = get_module_spec_registry()
        enabled_ids = set()
        for mod_key, mod in self._modules.items():
            if mod.config.enabled and spec_reg.has(mod_key):
                enabled_ids.add(spec_reg.get(mod_key).id)
        for spec_id in sorted(enabled_ids):
            spec = spec_reg.get(spec_id)
            for conflict_id in spec.conflicts:
                if conflict_id in enabled_ids:
                    warnings.append(f"Module '{spec.display_name}' conflicts with '{conflict_id}'.")
        return warnings

    def export_ui_schema(self, module_id: Optional[str] = None) -> Union[Dict[str, Any], List[Dict[str, Any]]]:
        """Exportiert das deklarative UI-Schema für das Rust-Frontend (nTop-Block-Stack & Property Inspector).

        Args:
            module_id: Optionaler Bezeichner für ein einzelnes Modul. Wenn None, werden alle Schemata geliefert.

        Returns:
            Einzelnes Schema-Dictionary oder Liste aller Modul-Schemata.
        """
        if module_id is not None:
            resolved = self._resolve_module_name(module_id)
            if resolved is not None:
                return self._modules[resolved].get_ui_schema()
            raise KeyError(f"Module '{module_id}' not found in registry")
        return [mod.get_ui_schema() for mod in self._modules.values()]
