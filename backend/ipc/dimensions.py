"""dimensions.py
=============
Strict 7-element SI dimensions validation [kg, m, s, K, mol, A, cd] and
OpenFOAM dimension set parser/formatter for CFDSolver.
"""

from __future__ import annotations

import math
import re
from typing import Any, Dict, List, Sequence, Union

# Strict 7 SI base units in canonical OpenFOAM order
SI_BASE_UNITS = ("kg", "m", "s", "K", "mol", "A", "cd")

# Standard physical field dimension sets [kg, m, s, K, mol, A, cd]
STANDARD_FIELD_DIMENSIONS: Dict[str, List[int]] = {
    # Kinematic & geometric
    "u": [0, 1, -1, 0, 0, 0, 0],              # Velocity [m/s]
    "velocity": [0, 1, -1, 0, 0, 0, 0],
    "points": [0, 1, 0, 0, 0, 0, 0],           # Coordinates [m]
    "cell_centres": [0, 1, 0, 0, 0, 0, 0],
    "volume": [0, 3, 0, 0, 0, 0, 0],           # Volume [m^3]
    "area": [0, 2, 0, 0, 0, 0, 0],             # Area [m^2]
    # Pressure & stress
    "p_incompressible": [0, 2, -2, 0, 0, 0, 0], # Kinematic pressure p/rho [m^2/s^2]
    "p_compressible": [1, -1, -2, 0, 0, 0, 0],  # Absolute/static pressure [Pa = kg/(m*s^2)]
    "p_rgh": [1, -1, -2, 0, 0, 0, 0],          # Hydrostatic/relative pressure [Pa]
    "wallshearstress": [1, -1, -2, 0, 0, 0, 0], # Wall shear stress [Pa]
    # Thermodynamics & transport
    "t": [0, 0, 0, 1, 0, 0, 0],                # Temperature [K]
    "temperature": [0, 0, 0, 1, 0, 0, 0],
    "rho": [1, -3, 0, 0, 0, 0, 0],              # Density [kg/m^3]
    "density": [1, -3, 0, 0, 0, 0, 0],
    "h": [0, 2, -2, 0, 0, 0, 0],                # Specific enthalpy [J/kg = m^2/s^2]
    "e": [0, 2, -2, 0, 0, 0, 0],                # Specific internal energy [J/kg = m^2/s^2]
    "cp": [0, 2, -2, -1, 0, 0, 0],              # Specific heat [J/(kg*K) = m^2/(s^2*K)]
    "k_thermal": [1, 1, -3, -1, 0, 0, 0],       # Thermal conductivity [W/(m*K) = kg*m/(s^3*K)]
    "mu": [1, -1, -1, 0, 0, 0, 0],              # Dynamic viscosity [Pa*s = kg/(m*s)]
    "nu": [0, 2, -1, 0, 0, 0, 0],               # Kinematic viscosity [m^2/s]
    # Turbulence
    "k": [0, 2, -2, 0, 0, 0, 0],                # Turbulent kinetic energy [m^2/s^2]
    "omega": [0, 0, -1, 0, 0, 0, 0],            # Specific dissipation rate [1/s]
    "epsilon": [0, 2, -3, 0, 0, 0, 0],          # Dissipation rate [m^2/s^3]
    "nut": [0, 2, -1, 0, 0, 0, 0],              # Turbulent kinematic viscosity [m^2/s]
    "mut": [1, -1, -1, 0, 0, 0, 0],              # Turbulent dynamic viscosity [Pa*s]
    # Dimensionless
    "alpha": [0, 0, 0, 0, 0, 0, 0],             # Phase fraction [-]
    "yplus": [0, 0, 0, 0, 0, 0, 0],             # Non-dimensional wall distance [-]
    "courant": [0, 0, 0, 0, 0, 0, 0],           # Courant number [-]
    "dimensionless": [0, 0, 0, 0, 0, 0, 0],
    # Electromagnetics
    "b": [1, 0, -2, 0, 0, -1, 0],               # Magnetic flux density [T = kg/(s^2*A)]
    "j": [0, -2, 0, 0, 0, 1, 0],                # Current density [A/m^2]
    "electricpotential": [1, 2, -3, 0, 0, -1, 0],# Electric potential [V = kg*m^2/(s^3*A)]
}


def validate_si_dimensions(dims: Any) -> List[int]:
    """Strictly validates a 7-element SI dimension vector [kg, m, s, K, mol, A, cd].

    Rules:
    1. Must be an iterable of exactly 7 elements.
    2. Every element must be an integer (or integral value with zero fractional part).
    3. Rejects float fractions, NaNs, infinities, strings, and wrong lengths.

    Args:
        dims: Sequence or array of exponents.

    Returns:
        List of exactly 7 integer exponents [kg, m, s, K, mol, A, cd].

    Raises:
        TypeError: If dims is not a sequence or contains non-numeric entries.
        ValueError: If dims length != 7, contains NaNs/infs, or has non-integer values.
    """
    if dims is None:
        raise TypeError("SI dimensions cannot be None; must be 7-element vector [kg, m, s, K, mol, A, cd]")

    if isinstance(dims, (str, bytes, dict)):
        raise TypeError(f"Expected sequence of 7 integers, got {type(dims).__name__}")

    try:
        dim_list = list(dims)
    except TypeError as exc:
        raise TypeError(f"SI dimensions must be an iterable, got {type(dims).__name__}") from exc

    if len(dim_list) != 7:
        raise ValueError(
            f"Strict SI dimensions require exactly 7 elements [kg, m, s, K, mol, A, cd], "
            f"got {len(dim_list)} elements: {dim_list}"
        )

    int_dims: List[int] = []
    for idx, val in enumerate(dim_list):
        if val is None or isinstance(val, (str, bytes)):
            raise TypeError(f"Dimension exponent at index {idx} ({SI_BASE_UNITS[idx]}) is not a number: {val!r}")

        if isinstance(val, float):
            if math.isnan(val) or math.isinf(val):
                raise ValueError(f"Dimension exponent at index {idx} cannot be NaN or Inf: {val}")
            if not val.is_integer():
                raise ValueError(
                    f"Dimension exponent at index {idx} ({SI_BASE_UNITS[idx]}) must be an integer, got non-integral float: {val}"
                )
            int_dims.append(int(val))
        elif isinstance(val, (int,)):
            # In Python, bool is a subclass of int (True==1, False==0) -> reject explicit booleans
            if isinstance(val, bool):
                raise TypeError(f"Dimension exponent at index {idx} cannot be a boolean: {val}")
            int_dims.append(int(val))
        elif hasattr(val, "__int__") and not isinstance(val, (bool,)):
            try:
                int_val = int(val)
                # Ensure numpy float or ctypes didn't truncate
                if float(val) != float(int_val):
                    raise ValueError(f"Dimension exponent at index {idx} has fractional part: {val}")
                int_dims.append(int_val)
            except Exception as e:
                raise ValueError(f"Invalid dimension exponent at index {idx}: {val}") from e
        else:
            raise TypeError(f"Dimension exponent at index {idx} must be integer, got {type(val).__name__}")

    return int_dims


def get_field_dimensions(field_name: str, compressible: bool = False) -> List[int]:
    """Lookup standard 7-element SI dimensions for a CFD field name.

    Args:
        field_name: Name of the field (e.g. 'U', 'p', 'T', 'k', 'nut').
        compressible: If True, pressure 'p' is Pa [1, -1, -2, 0, 0, 0, 0];
                      if False, kinematic pressure 'p' is m^2/s^2 [0, 2, -2, 0, 0, 0, 0].

    Returns:
        7-element integer list [kg, m, s, K, mol, A, cd].
    """
    key = field_name.strip().lower()

    if key == "p":
        return list(STANDARD_FIELD_DIMENSIONS["p_compressible" if compressible else "p_incompressible"])

    # Species mass fraction Y_<specie> is dimensionless
    if key.startswith("y_") or key.startswith("ydefault") or key == "y":
        return [0, 0, 0, 0, 0, 0, 0]

    if key in STANDARD_FIELD_DIMENSIONS:
        return list(STANDARD_FIELD_DIMENSIONS[key])

    # Default fallback: dimensionless
    return [0, 0, 0, 0, 0, 0, 0]


def format_openfoam_dimensions(dims: Sequence[int]) -> str:
    """Format 7-element SI dimensions into standard OpenFOAM syntax: [0 1 -1 0 0 0 0]."""
    valid = validate_si_dimensions(dims)
    return "[" + " ".join(str(d) for d in valid) + "]"


def parse_openfoam_dimensions(dim_str: str) -> List[int]:
    """Parse an OpenFOAM dimensions bracket string into 7 integer exponents."""
    match = re.search(r"\[\s*([0-9eE\.\+\-\s]+)\s*\]", dim_str)
    if not match:
        raise ValueError(f"Could not parse OpenFOAM dimensions string: {dim_str}")
    tokens = match.group(1).split()
    if len(tokens) != 7:
        raise ValueError(f"Expected 7 dimension integers inside brackets, found {len(tokens)}: {dim_str}")
    return validate_si_dimensions([int(float(t)) for t in tokens])
