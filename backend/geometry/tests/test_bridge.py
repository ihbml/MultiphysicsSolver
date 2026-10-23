"""Unit tests for CADBridge and NXTranslator."""

from pathlib import Path
import os
import pytest

from backend.geometry.bridge.cad_bridge import CADBridge
from backend.geometry.bridge.nx_translator import (
    NXTranslator,
    NativeCADBridgeUnavailableError,
)

SAMPLES_DIR = Path("resources/CADTestSamples")


def test_cad_bridge_detection():
    bridge = CADBridge()
    inst = bridge.detect_installations()
    assert "Siemens_NX" in inst
    assert "Autodesk_Inventor" in inst
    assert "Ansys" in inst
    # On this machine, NX 1926 is installed
    assert inst["Siemens_NX"] is not None


def test_nx_translator_environment():
    translator = NXTranslator()
    if translator.is_available:
        env = translator.build_environment()
        assert "UGII_BASE_DIR" in env
        assert "STEP214UG_DIR" in env
        # Mandatory trailing backslash check
        assert env["STEP214UG_DIR"].endswith(os.sep)
        assert env["ROSE_DB"].endswith(os.sep)
        assert env["ROSE"].endswith(os.sep)


def test_nx_translator_execution(tmp_path):
    translator = NXTranslator()
    if not translator.is_available:
        pytest.skip("Siemens NX is not installed on this system.")

    prt_path = SAMPLES_DIR / "TestCAD.prt"
    assert prt_path.exists(), f"Sample missing: {prt_path}"

    out_stp = tmp_path / "translated.stp"
    stp_path, log_path = translator.translate_prt_to_step(
        prt_path=prt_path,
        output_step=out_stp,
        protocol="AP214",
    )

    assert stp_path.exists()
    assert stp_path.stat().st_size > 0
    assert log_path.exists()

    # Read log file and verify solids processed >= 1
    log_text = log_path.read_text(encoding="utf-8", errors="replace")
    assert "Number of solids processed without problems" in log_text or "Entities Processed: Body: 1" in log_text


def test_cad_bridge_fallback(tmp_path):
    # Test fallback when an unhandled native file is given but a counterpart open standard exists
    bridge = CADBridge(nx_base_dir="non_existent_dir_for_test")
    # Temporarily copy TestCAD.stl and a dummy native file into tmp_path
    (tmp_path / "Model.stl").write_text("solid test\nendsolid test\n", encoding="utf-8")
    dummy_native = tmp_path / "Model.prt"
    dummy_native.write_bytes(b"dummy")

    # When allow_fallback=True, should fallback to Model.stl
    resolved = bridge.translate(dummy_native, allow_fallback=True)
    assert resolved == tmp_path / "Model.stl"

    # When allow_fallback=False, should raise NativeCADBridgeUnavailableError
    with pytest.raises(NativeCADBridgeUnavailableError):
        bridge.translate(dummy_native, allow_fallback=False)
