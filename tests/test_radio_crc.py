"""Run native CRC/bit-slip tests: python3 tests/test_radio_crc.py."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="frisquet-crc-") as temp:
    binary = Path(temp) / "radio_crc_test"
    subprocess.run([
        "c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
        "-I", str(root / "src"), str(root / "tests/radio_crc_test.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
