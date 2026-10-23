"""
registry.py
===========
Zentrales Registry-System für alle physikalischen Module des CFD-Solvers.
Registriert Standardmodule und erlaubt dynamisches Aktivieren/Deaktivieren.
"""

from __future__ import annotations
from typing import Dict, List, Type
from .base_module import BasePhysicsModule


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
        return {
            "constant/turbulenceProperties": (
                f"simulationType RAS;\nRAS\n{{\n    RASModel {self.config.model_variant};\n"
                "    turbulence on;\n    printCoeffs on;\n}"
            )
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


class SpeciesChemistryModule(BasePhysicsModule):
    def __init__(self):
        super().__init__(
            name="Species & Chemistry",
            category="Reactions",
            description="Mehrkomponententransport, Finite-Rate-Reaktionskinetik, Wandkatalyse (SMR/WGS) und Verbrennung."
        )
        self.config.model_variant = "finiteRateKinetics"

    def get_supported_solvers(self) -> List[str]:
        return ["reactingFoam", "chtMultiRegionFoam", "rhoReactingFoam"]

    def get_required_fields(self) -> List[str]:
        return ["Ydefault", "CH4", "H2O", "H2", "CO", "CO2", "N2"]

    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        return {
            "constant/chemistryProperties": (
                "chemistryType\n{\n    method ode;\n    solver EulerImplicit;\n}\n"
                "chemistry on;\ninitialChemicalTimeStep 1e-07;\n"
            )
        }

    def validate_dependencies(self, active_modules: Dict[str, BasePhysicsModule]) -> List[str]:
        warnings = []
        if not active_modules.get("Energy", None) or not active_modules["Energy"].config.enabled:
            warnings.append("Species & Chemistry erfordert aktives Energy-Modul zur Enthalpie- und Temperaturberechnung.")
        return warnings


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
        ]
        for m in modules:
            self._modules[m.name] = m

    def get_module(self, name: str) -> Optional[BasePhysicsModule]:
        return self._modules.get(name)

    def get_all_modules(self) -> Dict[str, BasePhysicsModule]:
        return self._modules

    def enable_module(self, name: str, variant: Optional[str] = None):
        if name in self._modules:
            self._modules[name].config.enabled = True
            if variant:
                self._modules[name].config.model_variant = variant

    def disable_module(self, name: str):
        if name in self._modules:
            self._modules[name].config.enabled = False

    def validate_configuration(self) -> List[str]:
        warnings = []
        for mod in self._modules.values():
            if mod.config.enabled:
                warnings.extend(mod.validate_dependencies(self._modules))
        return warnings
