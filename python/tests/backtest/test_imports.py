import subprocess
import sys


def test_public_packages_import_independently_in_clean_interpreters() -> None:
    packages = (
        "quant_system.execution",
        "quant_system.portfolio",
        "quant_system.risk",
        "quant_system.strategy",
        "quant_system.backtest",
    )

    for package in packages:
        completed = subprocess.run(
            [sys.executable, "-c", f"import {package}"],
            check=False,
            capture_output=True,
            text=True,
        )
        assert completed.returncode == 0, completed.stderr
