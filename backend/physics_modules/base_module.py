"""
base_module.py
==============
Abstrakte Basisklasse für alle physikalischen CFD-Module im Backend.
Jedes Modul definiert seine benötigten OpenFOAM-Dictionaries, Felder,
Randbedingungen und Validierungsregeln.
"""

from __future__ import annotations
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Dict, List, Any, Optional


@dataclass
class PhysicsModuleConfig:
    """Konfigurationseinstellungen für ein Physik-Modul."""
    enabled: bool = False
    model_variant: str = "default"
    parameters: Dict[str, Any] = field(default_factory=dict)


class BasePhysicsModule(ABC):
    """
    Basisklasse für modulare physikalische Modelle (inspiriert vom Ansys Fluent Models Panel).
    """

    def __init__(self, name: str, category: str, description: str):
        self.name = name
        self.category = category
        self.description = description
        self.config = PhysicsModuleConfig()

    @abstractmethod
    def get_supported_solvers(self) -> List[str]:
        """Gibt die kompatiblen OpenFOAM-Solver zurück (z. B. chtMultiRegionFoam, reactingFoam)."""
        pass

    @abstractmethod
    def get_required_fields(self) -> List[str]:
        """Gibt die primären Feldvariablen zurück (z. B. T, U, p, k, omega)."""
        pass

    @abstractmethod
    def generate_dictionary_entries(self, case_dir: str) -> Dict[str, str]:
        """
        Erzeugt die entsprechenden Einträge für OpenFOAM-Dictionaries
        (z. B. constant/radiationProperties, system/fvOptions, etc.).
        """
        pass

    def validate_dependencies(self, active_modules: Dict[str, BasePhysicsModule]) -> List[str]:
        """Prüft Abhängigkeiten zu anderen Modulen (z. B. Radiation erfordert Energy)."""
        warnings = []
        return warnings

    def to_dict(self) -> Dict[str, Any]:
        """Serialisierung für Frontend / Trame UI."""
        return {
            "name": self.name,
            "category": self.category,
            "description": self.description,
            "enabled": self.config.enabled,
            "model_variant": self.config.model_variant,
            "parameters": self.config.parameters,
            "required_fields": self.get_required_fields(),
            "supported_solvers": self.get_supported_solvers(),
        }
