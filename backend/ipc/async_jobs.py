"""async_jobs.py
==============
Thread-safe asynchronous job manager and lifecycle controller for long-running
backend tasks (geometry import, mesh generation, solver execution).
Supports clean cancellation, progress tracking, and partial artifact cleanup.
"""

from __future__ import annotations

import logging
import threading
import time
import uuid
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional

logger = logging.getLogger(__name__)


class JobStatus(str, Enum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    CANCELLED = "cancelled"
    FAILED = "failed"


@dataclass
class JobRecord:
    """Tracks status, timing, cancellation, and artifacts of an asynchronous task."""
    job_id: str
    command: str
    status: JobStatus = JobStatus.QUEUED
    created_at: float = field(default_factory=time.time)
    started_at: Optional[float] = None
    finished_at: Optional[float] = None
    progress: float = 0.0
    result: Optional[Any] = None
    error: Optional[str] = None
    cancel_event: threading.Event = field(default_factory=threading.Event)
    cleanup_func: Optional[Callable[[], None]] = None
    _thread: Optional[threading.Thread] = None

    def is_cancelled(self) -> bool:
        return self.cancel_event.is_set()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "job_id": self.job_id,
            "command": self.command,
            "status": self.status.value,
            "progress": round(self.progress, 3),
            "created_at": self.created_at,
            "started_at": self.started_at,
            "finished_at": self.finished_at,
            "result": self.result if self.status == JobStatus.COMPLETED else None,
            "error": self.error,
        }


class AsyncJobManager:
    """Central registry and executor for asynchronous background tasks."""

    def __init__(self) -> None:
        self._jobs: Dict[str, JobRecord] = {}
        self._lock = threading.Lock()

    def create_job(
        self,
        command: str,
        target_func: Callable[[JobRecord], Any],
        cleanup_func: Optional[Callable[[], None]] = None,
    ) -> JobRecord:
        """Creates and launches a background thread job."""
        job_id = f"job_{uuid.uuid4().hex[:12]}"
        record = JobRecord(
            job_id=job_id,
            command=command,
            status=JobStatus.QUEUED,
            cleanup_func=cleanup_func,
        )

        def _runner():
            record.started_at = time.time()
            record.status = JobStatus.RUNNING
            try:
                if record.is_cancelled():
                    record.status = JobStatus.CANCELLED
                    if record.cleanup_func:
                        record.cleanup_func()
                    return

                res = target_func(record)

                if record.is_cancelled():
                    record.status = JobStatus.CANCELLED
                    record.result = None
                    if record.cleanup_func:
                        record.cleanup_func()
                else:
                    record.result = res
                    record.status = JobStatus.COMPLETED
                    record.progress = 1.0
            except Exception as e:
                logger.error("Job %s (%s) failed: %s", job_id, command, e, exc_info=True)
                record.error = str(e)
                record.status = JobStatus.FAILED
                record.result = None
                if record.cleanup_func:
                    try:
                        record.cleanup_func()
                    except Exception:
                        pass
            finally:
                record.finished_at = time.time()

        th = threading.Thread(target=_runner, name=f"CFDJob-{job_id}", daemon=True)
        record._thread = th

        with self._lock:
            self._jobs[job_id] = record

        th.start()
        return record

    def get_job(self, job_id: str) -> Optional[JobRecord]:
        with self._lock:
            return self._jobs.get(job_id)

    def cancel_job(self, job_id: str) -> bool:
        """Cancels a job, sets cancel event, and executes cleanup function.

        Guarantees that a cancelled job NEVER publishes partial valid results.
        """
        with self._lock:
            record = self._jobs.get(job_id)
            if not record:
                return False

            if record.status in (JobStatus.COMPLETED, JobStatus.CANCELLED, JobStatus.FAILED):
                return True

            record.cancel_event.set()
            record.status = JobStatus.CANCELLED
            record.result = None
            record.finished_at = time.time()

            # Execute cleanup immediately
            if record.cleanup_func:
                try:
                    record.cleanup_func()
                except Exception as e:
                    logger.warning("Error running cleanup for cancelled job %s: %s", job_id, e)

            return True

    def list_jobs(self) -> List[Dict[str, Any]]:
        with self._lock:
            return [j.to_dict() for j in self._jobs.values()]
