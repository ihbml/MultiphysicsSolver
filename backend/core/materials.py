"""materials.py
============
Extensible Material database manager and thermophysical properties for CFDSolver.
Provides standard fluid, gas, and solid materials with NASA polynomials,
categorized physical property groups (Surface, Electrical, Magnetic, Mechanical,
Optical, Thermal, Manufacturing, Radiological), and arbitrary extra_properties passthrough.
"""

from __future__ import annotations

import json
import logging
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Union

logger = logging.getLogger(__name__)

# Canonical property definitions by category
PROPERTY_CATEGORIES: Dict[str, List[str]] = {
    "surface": [
        "specific_surface_area",
        "surface_energy",
        "surface_tension",
    ],
    "electrical": [
        "electrical_conductivity",
        "electrical_resistivity",
        "permittivity",
        "dielectric_constant",
        "dielectric_strength",
        "seebeck_coefficient",
        "capacitance",
    ],
    "magnetic": [
        "magnetic_permeability",
        "curie_temperature",
        "magnetocaloric_coefficient",
        "hall_coefficient",
    ],
    "mechanical": [
        "youngs_modulus",
        "bulk_modulus",
        "shear_modulus",
        "poissons_ratio",
        "yield_strength",
        "tensile_strength",
        "compressive_strength",
        "fracture_toughness",
        "hardness",
        "speed_of_sound",
        "mass_diffusivity",
        "surface_roughness",
        "friction_coefficient",
        "acoustical_absorption",
    ],
    "optical": [
        "refractive_index",
        "emissivity",
        "absorptivity",
        "reflectivity",
        "transmittance",
    ],
    "thermal": [
        "thermal_expansion_coeff",
        "thermal_diffusivity",
        "melting_point",
        "boiling_point",
        "critical_temperature",
        "triple_point",
        "glass_transition_temperature",
        "heat_of_vaporization",
        "vapor_pressure",
        "flash_point",
    ],
    "manufacturing": [
        "castability",
        "machinability_rating",
    ],
    "radiological": [
        "attenuation_coefficient",
        "half_life",
        "neutron_cross_section",
        "specific_activity",
    ],
}

# Standard SI Units dictionary for canonical properties
DEFAULT_PROPERTY_UNITS: Dict[str, str] = {
    # Core
    "density": "kg/m^3",
    "specific_heat": "J/(kg*K)",
    "thermal_conductivity": "W/(m*K)",
    "viscosity": "Pa*s",
    "molecular_weight": "kg/kmol",
    "prandtl_number": "-",
    # Surface
    "specific_surface_area": "m^2/kg",
    "surface_energy": "J/m^2",
    "surface_tension": "N/m",
    # Electrical
    "electrical_conductivity": "S/m",
    "electrical_resistivity": "ohm*m",
    "permittivity": "F/m",
    "dielectric_constant": "-",
    "dielectric_strength": "V/m",
    "seebeck_coefficient": "V/K",
    "capacitance": "F",
    # Magnetic
    "magnetic_permeability": "H/m",
    "curie_temperature": "K",
    "magnetocaloric_coefficient": "K/T",
    "hall_coefficient": "m^3/C",
    # Mechanical
    "youngs_modulus": "Pa",
    "bulk_modulus": "Pa",
    "shear_modulus": "Pa",
    "poissons_ratio": "-",
    "yield_strength": "Pa",
    "tensile_strength": "Pa",
    "compressive_strength": "Pa",
    "fracture_toughness": "Pa*m^0.5",
    "hardness": "HV",
    "speed_of_sound": "m/s",
    "mass_diffusivity": "m^2/s",
    "surface_roughness": "m",
    "friction_coefficient": "-",
    "acoustical_absorption": "-",
    # Optical
    "refractive_index": "-",
    "emissivity": "-",
    "absorptivity": "-",
    "reflectivity": "-",
    "transmittance": "-",
    # Thermal
    "thermal_expansion_coeff": "1/K",
    "thermal_diffusivity": "m^2/s",
    "melting_point": "K",
    "boiling_point": "K",
    "critical_temperature": "K",
    "triple_point": "K",
    "glass_transition_temperature": "K",
    "heat_of_vaporization": "J/kg",
    "vapor_pressure": "Pa",
    "flash_point": "K",
    # Manufacturing
    "castability": "-",
    "machinability_rating": "%",
    # Radiological
    "attenuation_coefficient": "1/m",
    "half_life": "s",
    "neutron_cross_section": "barn",
    "specific_activity": "Bq/kg",
}


@dataclass
class Material:
    """Thermophysical, mechanical, electrical, and transport properties of a CFD material.

    Supports core properties, categorized property groups, and arbitrary extra_properties passthrough.
    """

    name: str
    category: str  # "fluid", "gas", "solid", "liquid"
    density: float  # [kg/m^3]
    specific_heat: float = 1000.0  # [J/(kg*K)]
    thermal_conductivity: float = 0.025  # [W/(m*K)]
    viscosity: Optional[float] = None  # Dynamic viscosity [Pa*s]
    molecular_weight: Optional[float] = None  # [kg/kmol]
    prandtl_number: Optional[float] = None  # [-]
    nasa_polynomials: Optional[Dict[str, Any]] = None  # High/low temperature coefficients
    properties: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    extra_properties: Dict[str, Any] = field(default_factory=dict)
    unit_si: Dict[str, str] = field(default_factory=lambda: dict(DEFAULT_PROPERTY_UNITS))

    def __post_init__(self) -> None:
        # Initialize categorized properties container
        for cat in PROPERTY_CATEGORIES:
            if cat not in self.properties:
                self.properties[cat] = {}

        # Merge defaults into unit_si if missing
        for prop, u in DEFAULT_PROPERTY_UNITS.items():
            if prop not in self.unit_si:
                self.unit_si[prop] = u

    def set_property(self, category: str, key: str, value: Any, unit: Optional[str] = None) -> None:
        """Sets a categorized property and optional SI unit."""
        cat_lower = category.lower()
        if cat_lower not in self.properties:
            self.properties[cat_lower] = {}
        self.properties[cat_lower][key] = value
        if unit:
            self.unit_si[key] = unit

    def get_property(self, key: str) -> Optional[Any]:
        """Looks up a property across core, categorized, or extra_properties."""
        if hasattr(self, key):
            val = getattr(self, key)
            if val is not None:
                return val

        for cat_dict in self.properties.values():
            if key in cat_dict:
                return cat_dict[key]

        return self.extra_properties.get(key)

    def missing_properties(self, required: Sequence[str]) -> List[str]:
        """Checks which of the requested properties are missing (i.e. None or absent).

        Allows case validation to verify ONLY the properties required by the active physics modules.

        Args:
            required: Sequence of property names (e.g. ['density', 'viscosity', 'youngs_modulus']).

        Returns:
            List of missing property names.
        """
        missing: List[str] = []
        for prop in required:
            val = self.get_property(prop)
            if val is None:
                missing.append(prop)
        return missing

    def to_dict(self) -> Dict[str, Any]:
        """Serializes material to comprehensive JSON-ready dictionary."""
        d = {
            "name": self.name,
            "category": self.category,
            "density": self.density,
            "specific_heat": self.specific_heat,
            "thermal_conductivity": self.thermal_conductivity,
            "viscosity": self.viscosity,
            "molecular_weight": self.molecular_weight,
            "prandtl_number": self.prandtl_number,
            "nasa_polynomials": self.nasa_polynomials,
            "properties": {cat: dict(p) for cat, p in self.properties.items() if p},
            "extra_properties": dict(self.extra_properties),
            "unit_si": dict(self.unit_si),
        }
        # Flatten non-empty categorized properties to top level for convenient access
        for cat, p_dict in self.properties.items():
            for k, v in p_dict.items():
                if k not in d:
                    d[k] = v
        # Flatten extra_properties to top level as well
        for k, v in self.extra_properties.items():
            if k not in d:
                d[k] = v

        return d

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Material":
        """Creates a Material instance from a dictionary, capturing unknown fields in extra_properties."""
        name = str(data.get("name", "UnnamedMaterial"))
        category = str(data.get("category", "fluid"))
        density = float(data.get("density", 1.0))
        specific_heat = float(data.get("specific_heat", 1000.0))
        thermal_conductivity = float(data.get("thermal_conductivity", 0.025))
        viscosity = float(data["viscosity"]) if "viscosity" in data and data["viscosity"] is not None else None
        molecular_weight = float(data["molecular_weight"]) if "molecular_weight" in data and data["molecular_weight"] is not None else None
        prandtl_number = float(data["prandtl_number"]) if "prandtl_number" in data and data["prandtl_number"] is not None else None
        nasa_polynomials = data.get("nasa_polynomials")

        unit_si = dict(DEFAULT_PROPERTY_UNITS)
        if "unit_si" in data and isinstance(data["unit_si"], dict):
            unit_si.update(data["unit_si"])

        properties: Dict[str, Dict[str, Any]] = {cat: {} for cat in PROPERTY_CATEGORIES}
        if "properties" in data and isinstance(data["properties"], dict):
            for cat, sub in data["properties"].items():
                cat_l = cat.lower()
                if cat_l not in properties:
                    properties[cat_l] = {}
                if isinstance(sub, dict):
                    properties[cat_l].update(sub)

        # Inverted mapping: property name -> category
        prop_to_cat = {prop: cat for cat, props in PROPERTY_CATEGORIES.items() for prop in props}

        known_core_keys = {
            "name", "category", "density", "specific_heat", "thermal_conductivity",
            "viscosity", "molecular_weight", "prandtl_number", "nasa_polynomials",
            "properties", "extra_properties", "unit_si"
        }

        extra_props = dict(data.get("extra_properties", {}))

        # Sort remaining top-level keys into either categorized properties or extra_properties
        for k, v in data.items():
            if k in known_core_keys:
                continue
            if k in prop_to_cat:
                cat = prop_to_cat[k]
                properties[cat][k] = v
            else:
                extra_props[k] = v

        mat = cls(
            name=name,
            category=category,
            density=density,
            specific_heat=specific_heat,
            thermal_conductivity=thermal_conductivity,
            viscosity=viscosity,
            molecular_weight=molecular_weight,
            prandtl_number=prandtl_number,
            nasa_polynomials=nasa_polynomials,
            properties=properties,
            extra_properties=extra_props,
            unit_si=unit_si,
        )
        return mat


class MaterialDatabase:
    """Repository of CFD materials loaded from built-in registry and resources/materials_db/."""

    def __init__(self, db_dir: Optional[Union[str, Path]] = None) -> None:
        self.db_dir = Path(db_dir).resolve() if db_dir is not None else (
            Path(__file__).resolve().parent.parent.parent / "resources" / "materials_db"
        )
        self._materials: Dict[str, Material] = {}
        self._register_defaults()

        if self.db_dir.exists():
            self.load_directory(self.db_dir)

    def _register_defaults(self) -> None:
        defaults = [
            Material(
                name="Air",
                category="gas",
                density=1.204,
                viscosity=1.825e-5,
                specific_heat=1005.0,
                thermal_conductivity=0.0257,
                molecular_weight=28.966,
                prandtl_number=0.71,
                nasa_polynomials={
                    "t_low": 200.0,
                    "t_mid": 1000.0,
                    "t_high": 6000.0,
                    "low": [3.568396, -6.787294e-4, 1.553715e-6, -3.299374e-12, -4.663954e-13, -1062.347, 5.714829],
                    "high": [3.087927, 1.245972e-3, -4.23719e-7, 6.74775e-11, -3.97077e-15, -995.2614, 4.10342],
                },
                properties={
                    "mechanical": {"speed_of_sound": 343.2},
                    "thermal": {"thermal_expansion_coeff": 1.0 / 293.15},
                    "optical": {"refractive_index": 1.000293},
                },
            ),
            Material(
                name="Water",
                category="liquid",
                density=998.2,
                viscosity=1.002e-3,
                specific_heat=4182.0,
                thermal_conductivity=0.600,
                molecular_weight=18.015,
                prandtl_number=6.99,
                properties={
                    "surface": {"surface_tension": 0.0728},
                    "mechanical": {"speed_of_sound": 1482.0, "bulk_modulus": 2.2e9},
                    "thermal": {"boiling_point": 373.15, "melting_point": 273.15, "heat_of_vaporization": 2.26e6},
                    "optical": {"refractive_index": 1.333},
                },
            ),
            Material(
                name="Steel_1.4301",
                category="solid",
                density=8000.0,
                specific_heat=500.0,
                thermal_conductivity=16.2,
                molecular_weight=55.845,
                properties={
                    "mechanical": {
                        "youngs_modulus": 2.0e11,
                        "poissons_ratio": 0.30,
                        "shear_modulus": 7.7e10,
                        "yield_strength": 2.15e8,
                        "tensile_strength": 5.05e8,
                    },
                    "electrical": {"electrical_resistivity": 7.2e-7},
                    "thermal": {"thermal_expansion_coeff": 1.6e-5, "melting_point": 1723.15},
                },
            ),
            Material(
                name="Aluminum_6061",
                category="solid",
                density=2700.0,
                specific_heat=896.0,
                thermal_conductivity=167.0,
                molecular_weight=26.982,
                properties={
                    "mechanical": {
                        "youngs_modulus": 6.89e10,
                        "poissons_ratio": 0.33,
                        "yield_strength": 2.76e8,
                        "tensile_strength": 3.10e8,
                    },
                    "electrical": {"electrical_conductivity": 2.5e7},
                    "thermal": {"thermal_expansion_coeff": 2.3e-5, "melting_point": 925.15},
                },
            ),
            Material(
                name="Copper",
                category="solid",
                density=8960.0,
                specific_heat=385.0,
                thermal_conductivity=401.0,
                molecular_weight=63.546,
                properties={
                    "mechanical": {
                        "youngs_modulus": 1.17e11,
                        "poissons_ratio": 0.34,
                        "yield_strength": 7.0e7,
                    },
                    "electrical": {"electrical_conductivity": 5.96e7, "electrical_resistivity": 1.68e-8},
                    "thermal": {"thermal_expansion_coeff": 1.65e-5, "melting_point": 1357.77},
                },
            ),
            Material(
                name="Titanium_Grade5",
                category="solid",
                density=4430.0,
                specific_heat=526.0,
                thermal_conductivity=6.7,
                molecular_weight=47.867,
                properties={
                    "mechanical": {
                        "youngs_modulus": 1.14e11,
                        "poissons_ratio": 0.342,
                        "yield_strength": 8.8e8,
                        "tensile_strength": 9.5e8,
                    },
                    "thermal": {"thermal_expansion_coeff": 8.6e-6, "melting_point": 1933.15},
                },
            ),
            Material(
                name="Inconel_718",
                category="solid",
                density=8190.0,
                specific_heat=435.0,
                thermal_conductivity=11.4,
                molecular_weight=60.0,
                properties={
                    "mechanical": {
                        "youngs_modulus": 2.05e11,
                        "poissons_ratio": 0.294,
                        "yield_strength": 1.1e9,
                        "tensile_strength": 1.37e9,
                    },
                    "thermal": {"thermal_expansion_coeff": 1.3e-5, "melting_point": 1609.15},
                },
            ),
            Material(
                name="Nitrogen_N2",
                category="gas",
                density=1.165,
                viscosity=1.76e-5,
                specific_heat=1040.0,
                thermal_conductivity=0.0260,
                molecular_weight=28.0134,
                prandtl_number=0.704,
            ),
            Material(
                name="Oxygen_O2",
                category="gas",
                density=1.331,
                viscosity=2.04e-5,
                specific_heat=919.0,
                thermal_conductivity=0.0263,
                molecular_weight=31.9988,
                prandtl_number=0.713,
            ),
            Material(
                name="CarbonDioxide_CO2",
                category="gas",
                density=1.842,
                viscosity=1.49e-5,
                specific_heat=844.0,
                thermal_conductivity=0.0168,
                molecular_weight=44.01,
                prandtl_number=0.748,
            ),
            Material(
                name="Methane_CH4",
                category="gas",
                density=0.668,
                viscosity=1.10e-5,
                specific_heat=2226.0,
                thermal_conductivity=0.0340,
                molecular_weight=16.04,
                prandtl_number=0.72,
            ),
            Material(
                name="Hydrogen_H2",
                category="gas",
                density=0.0838,
                viscosity=8.90e-6,
                specific_heat=14304.0,
                thermal_conductivity=0.187,
                molecular_weight=2.016,
                prandtl_number=0.68,
            ),
        ]
        for m in defaults:
            self._materials[m.name.lower()] = m

    def load_directory(self, db_dir: Union[str, Path]) -> int:
        """Loads all JSON material definitions from directory."""
        p = Path(db_dir).resolve()
        if not p.is_dir():
            return 0

        loaded = 0
        for item in p.glob("*.json"):
            try:
                with open(item, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if isinstance(data, dict) and "name" in data and "density" in data:
                    mat = Material.from_dict(data)
                    self._materials[mat.name.lower()] = mat
                    loaded += 1
            except Exception as e:
                logger.warning("Failed to parse material file %s: %s", item, e)

        return loaded

    def add_material(self, material: Union[Material, Dict[str, Any]], persist: bool = False) -> Material:
        """Adds a new material instance or dictionary dynamically.

        Args:
            material: Material instance or dictionary.
            persist: If True, writes the material to self.db_dir/<name>.json.

        Returns:
            Added Material instance.
        """
        if isinstance(material, dict):
            mat = Material.from_dict(material)
        elif isinstance(material, Material):
            mat = material
        else:
            raise TypeError(f"Expected Material or dict, got {type(material).__name__}")

        self._materials[mat.name.lower()] = mat

        if persist:
            self.db_dir.mkdir(parents=True, exist_ok=True)
            target_file = self.db_dir / f"{mat.name.lower().replace(' ', '_')}.json"
            with open(target_file, "w", encoding="utf-8") as f:
                json.dump(mat.to_dict(), f, indent=2)
            logger.info("Persisted material '%s' to %s", mat.name, target_file)

        return mat

    def get_material(self, name: str) -> Optional[Material]:
        """Lookup material by case-insensitive name."""
        return self._materials.get(name.lower())

    def list_materials(self, category: Optional[str] = None) -> List[Dict[str, Any]]:
        """List materials, optionally filtered by category ('gas', 'liquid', 'fluid', 'solid')."""
        results: List[Dict[str, Any]] = []
        for mat in self._materials.values():
            if category is not None:
                c_req = category.lower()
                c_mat = mat.category.lower()
                if c_req == "fluid":
                    if c_mat not in ("fluid", "gas", "liquid"):
                        continue
                elif c_mat != c_req:
                    continue
            results.append(mat.to_dict())
        results.sort(key=lambda m: m["name"])
        return results
