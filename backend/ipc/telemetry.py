"""telemetry.py
============
Paket 4: Live NDJSON telemetry streaming for iteration progress, residuals,
Courant numbers, NaN cell monitors, and written timestep events.
"""

from __future__ import annotations

import json
import math
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Generator, List, Optional, TextIO, Union


@dataclass
class IterationEvent:
    """Telemetry data emitted per solver iteration."""
    type: str = "iter"
    step: int = 0
    time: float = 0.0
    dt: float = 1e-4
    co_mean: float = 0.0
    co_max: float = 0.0
    wall_s: float = 0.0
    residuals: Dict[str, float] = field(default_factory=dict)
    monitors: Dict[str, Any] = field(default_factory=lambda: {"n_nan_cells": 0})

    def to_ndjson(self) -> str:
        """Serializes event to a single compact NDJSON line."""
        # Replace non-finite floats with None or sentinel to ensure valid JSON
        safe_residuals: Dict[str, Optional[float]] = {}
        for k, v in self.residuals.items():
            if math.isnan(v) or math.isinf(v):
                safe_residuals[k] = None
            else:
                safe_residuals[k] = v

        safe_monitors = dict(self.monitors)
        if "n_nan_cells" not in safe_monitors:
            # Check if any residual was NaN/None
            n_nan = sum(1 for v in self.residuals.values() if math.isnan(v) or math.isinf(v))
            safe_monitors["n_nan_cells"] = n_nan

        payload = {
            "type": self.type,
            "step": self.step,
            "time": self.time,
            "dt": self.dt,
            "co_mean": self.co_mean if not (math.isnan(self.co_mean) or math.isinf(self.co_mean)) else None,
            "co_max": self.co_max if not (math.isnan(self.co_max) or math.isinf(self.co_max)) else None,
            "wall_s": round(self.wall_s, 3),
            "residuals": safe_residuals,
            "monitors": safe_monitors,
        }
        return json.dumps(payload, separators=(",", ":"))


@dataclass
class TimestepWrittenEvent:
    """Telemetry event emitted when a timestep is flushed to disk."""
    type: str = "timestep_written"
    time_dir: str = "0.0"
    reconstructed: bool = True
    available_fields: List[str] = field(default_factory=list)

    def to_ndjson(self) -> str:
        payload = {
            "type": self.type,
            "time_dir": self.time_dir,
            "reconstructed": self.reconstructed,
            "available_fields": list(self.available_fields),
        }
        return json.dumps(payload, separators=(",", ":"))


class TelemetryStreamer:
    """Manages real-time streaming of NDJSON telemetry lines to callbacks or IO streams."""

    def __init__(self, output_stream: Optional[TextIO] = None, callback: Optional[Callable[[str], None]] = None) -> None:
        self.output_stream = output_stream
        self.callback = callback
        self.history: List[Dict[str, Any]] = []
        self._start_time = time.time()

    def reset_clock(self) -> None:
        self._start_time = time.time()

    def elapsed_wall_seconds(self) -> float:
        return time.time() - self._start_time

    def emit(self, event_json_line: str) -> None:
        """Emits an NDJSON line and appends newline."""
        line = event_json_line.strip() + "\n"
        if self.output_stream is not None:
            self.output_stream.write(line)
            self.output_stream.flush()
        if self.callback is not None:
            self.callback(event_json_line.strip())

    def emit_iter(
        self,
        step: int,
        sim_time: float,
        dt: float,
        co_mean: float,
        co_max: float,
        residuals: Dict[str, float],
        monitors: Optional[Dict[str, Any]] = None,
        wall_s: Optional[float] = None,
    ) -> str:
        """Formats, emits, and records an iteration progress event."""
        w_s = wall_s if wall_s is not None else self.elapsed_wall_seconds()
        event = IterationEvent(
            step=step,
            time=sim_time,
            dt=dt,
            co_mean=co_mean,
            co_max=co_max,
            wall_s=w_s,
            residuals=residuals,
            monitors=monitors or {"n_nan_cells": 0},
        )
        line = event.to_ndjson()
        self.history.append(json.loads(line))
        self.emit(line)
        return line

    def emit_timestep_written(
        self,
        time_dir: str,
        reconstructed: bool = True,
        available_fields: Optional[List[str]] = None,
    ) -> str:
        """Formats, emits, and records a timestep written event."""
        event = TimestepWrittenEvent(
            time_dir=time_dir,
            reconstructed=reconstructed,
            available_fields=available_fields or [],
        )
        line = event.to_ndjson()
        self.history.append(json.loads(line))
        self.emit(line)
        return line

    @staticmethod
    def parse_ndjson_line(line: str) -> Dict[str, Any]:
        """Parses an NDJSON telemetry line into a dictionary."""
        clean = line.strip()
        if not clean:
            raise ValueError("Empty NDJSON line")
        data = json.loads(clean)
        if not isinstance(data, dict):
            raise ValueError(f"Expected JSON object in NDJSON line, got {type(data).__name__}")
        if "type" not in data:
            raise ValueError("NDJSON telemetry event missing mandatory 'type' field")
        return data
