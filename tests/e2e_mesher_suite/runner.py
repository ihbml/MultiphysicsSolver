"""Standalone CLI test runner for CFDSolver E2E Mesher Verification Suite."""

from __future__ import annotations

import argparse
import json
import logging
from pathlib import Path
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Sequence

logger = logging.getLogger(__name__)

_SUITE_DIR = Path(__file__).resolve().parent


def build_marker_expression(
    tier: str,
    *,
    fast: bool = False,
    include_5m: bool = False,
) -> str:
    """Build pytest ``-m`` marker expression for the requested tier and flags."""
    expr = "e2e" if tier == "all" else f"tier{tier}"
    if fast:
        expr = f"{expr} and not slow and not scale_5m"
    elif tier == "all" and not include_5m:
        expr = f"{expr} and not scale_5m"
    return expr


def build_pytest_command(
    tier: str,
    *,
    fast: bool = False,
    include_5m: bool = False,
    verbose: bool = False,
    keyword: Optional[str] = None,
) -> List[str]:
    """Construct the pytest subprocess command vector."""
    marker_expr = build_marker_expression(tier, fast=fast, include_5m=include_5m)
    cmd = [
        sys.executable,
        "-m",
        "pytest",
        str(_SUITE_DIR),
        "-m",
        marker_expr,
    ]
    if verbose:
        cmd.append("-v")
    if keyword:
        cmd.extend(["-k", keyword])
    return cmd


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the E2E mesher pytest suite and optionally emit a JSON summary report."""
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )
    parser = argparse.ArgumentParser(
        description="CFDSolver E2E Mesher Verification Suite Runner"
    )
    parser.add_argument(
        "--tier",
        choices=("1", "2", "3", "4", "all"),
        default="all",
        help="Test tier to execute (1, 2, 3, 4, or all; default: all).",
    )
    parser.add_argument(
        "--fast",
        action="store_true",
        help="Exclude slow and scale_5m tests.",
    )
    parser.add_argument(
        "--include-5m",
        action="store_true",
        help="Include 5M-12M cell scale_5m tests when running --tier all.",
    )
    parser.add_argument(
        "--json-report",
        type=Path,
        default=None,
        help="Optional path to write a JSON execution summary.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose pytest output (-v).",
    )
    parser.add_argument(
        "-k",
        "--keyword",
        type=str,
        default=None,
        help="Optional pytest -k keyword filter expression.",
    )

    args = parser.parse_args(argv)
    cmd = build_pytest_command(
        args.tier,
        fast=args.fast,
        include_5m=args.include_5m,
        verbose=args.verbose,
        keyword=args.keyword,
    )

    logger.info("Executing E2E suite: %s", " ".join(cmd))
    t0 = time.perf_counter()
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        check=False,
    )
    duration_s = time.perf_counter() - t0

    if proc.stdout:
        sys.stdout.write(proc.stdout)
        sys.stdout.flush()
    if proc.stderr:
        sys.stderr.write(proc.stderr)
        sys.stderr.flush()

    logger.info(
        "E2E suite completed in %.2fs with exit code %d",
        duration_s,
        proc.returncode,
    )

    if args.json_report is not None:
        report_payload: Dict[str, Any] = {
            "command": cmd,
            "returncode": int(proc.returncode),
            "duration_s": round(duration_s, 4),
            "tier": args.tier,
            "fast": bool(args.fast),
            "include_5m": bool(args.include_5m),
            "stdout_tail": proc.stdout[-4000:] if proc.stdout else "",
            "stderr_tail": proc.stderr[-4000:] if proc.stderr else "",
        }
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(
            json.dumps(report_payload, indent=2),
            encoding="utf-8",
        )
        logger.info("Wrote JSON summary report to %s", args.json_report)

    return int(proc.returncode)


if __name__ == "__main__":
    sys.exit(main())
