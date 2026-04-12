"""Build a platform wheel for opus-qext.

Since we bundle a pre-built native DLL, we need to produce a platform-specific
wheel (not a pure-python wheel). This script:
1. Builds the wheel using setuptools
2. Renames it with the correct platform tag (win_amd64, etc.)

Usage:
    python build_wheel.py           # Build wheel for current platform
    python build_wheel.py --check   # Build + verify install in a venv
"""
import os
import sys
import glob
import shutil
import subprocess
import platform
import re

ROOT = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(ROOT, "dist")


def get_platform_tag():
    """Get the wheel platform tag for the current system."""
    if sys.platform == "win32":
        if platform.machine().lower() in ("amd64", "x86_64"):
            return "win_amd64"
        elif platform.machine().lower() in ("arm64", "aarch64"):
            return "win_arm64"
        else:
            return "win32"
    elif sys.platform == "darwin":
        if platform.machine() == "arm64":
            return "macosx_11_0_arm64"
        else:
            return "macosx_10_9_x86_64"
    else:
        if platform.machine() == "x86_64":
            return "manylinux_2_17_x86_64.manylinux2014_x86_64"
        elif platform.machine() == "aarch64":
            return "manylinux_2_17_aarch64.manylinux2014_aarch64"
        else:
            return f"linux_{platform.machine()}"


def get_python_tag():
    """Get python version tag like cp310."""
    return f"cp{sys.version_info.major}{sys.version_info.minor}"


def build_wheel():
    """Build the wheel and fix its platform tag."""
    # Clean dist/
    if os.path.exists(DIST):
        shutil.rmtree(DIST)
    os.makedirs(DIST, exist_ok=True)

    # Build wheel
    print("=== Building wheel ===")
    subprocess.check_call([
        sys.executable, "-m", "pip", "wheel",
        "--no-deps", "--wheel-dir", DIST, ROOT
    ])

    # Find the built wheel
    wheels = glob.glob(os.path.join(DIST, "opus_qext-*.whl"))
    if not wheels:
        print("ERROR: No wheel found in dist/")
        sys.exit(1)

    src_whl = wheels[0]
    print(f"  Built: {os.path.basename(src_whl)}")

    # Rename to platform wheel if it's tagged as "any" (pure python)
    if "-any.whl" in src_whl or "-none-any.whl" in src_whl:
        plat = get_platform_tag()
        py = get_python_tag()
        new_name = re.sub(
            r"-(py\d|cp\d+)-none-any\.whl$",
            f"-{py}-none-{plat}.whl",
            os.path.basename(src_whl)
        )
        if new_name == os.path.basename(src_whl):
            # Fallback: just replace the tag part
            new_name = os.path.basename(src_whl).replace(
                "-any.whl", f"-{plat}.whl"
            ).replace(
                "py3-none", f"{py}-none"
            )
        dst_whl = os.path.join(DIST, new_name)
        os.rename(src_whl, dst_whl)
        print(f"  Renamed -> {new_name}")
    else:
        dst_whl = src_whl
        print(f"  Platform tag already set: {os.path.basename(dst_whl)}")

    return dst_whl


def verify_wheel(whl_path):
    """Quick verification: install in temp venv and try to import."""
    import tempfile
    venv_dir = os.path.join(tempfile.gettempdir(), "opus_qext_test_venv")

    if os.path.exists(venv_dir):
        shutil.rmtree(venv_dir)

    print("\n=== Verifying wheel ===")
    print(f"  Creating venv at {venv_dir}")
    subprocess.check_call([sys.executable, "-m", "venv", venv_dir])

    if sys.platform == "win32":
        pip = os.path.join(venv_dir, "Scripts", "pip.exe")
        python = os.path.join(venv_dir, "Scripts", "python.exe")
    else:
        pip = os.path.join(venv_dir, "bin", "pip")
        python = os.path.join(venv_dir, "bin", "python")

    print(f"  Installing {os.path.basename(whl_path)}")
    subprocess.check_call([pip, "install", whl_path],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    print("  Testing import...")
    result = subprocess.run(
        [python, "-c",
         "import opus_qext; print(f'opus_qext {opus_qext.__version__} loaded OK')"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        print(f"  {result.stdout.strip()}")
        print("  PASS")
    else:
        print(f"  FAIL: {result.stderr.strip()}")
        sys.exit(1)

    # Cleanup
    shutil.rmtree(venv_dir, ignore_errors=True)


if __name__ == "__main__":
    whl = build_wheel()
    print(f"\nWheel ready: {whl}")
    print(f"  Size: {os.path.getsize(whl):,} bytes")

    if "--check" in sys.argv:
        verify_wheel(whl)
    else:
        print("\nRun with --check to verify install in a temp venv.")
