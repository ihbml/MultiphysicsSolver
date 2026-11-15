"""CFDSolver physics modules registry and base abstractions."""

from .base_module import BasePhysicsModule, PhysicsModuleConfig
from .registry import ModuleRegistry

__all__ = [
    "BasePhysicsModule",
    "PhysicsModuleConfig",
    "ModuleRegistry",
]
