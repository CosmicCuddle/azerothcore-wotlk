"""Exercise the real save observer's receipt retention without engine or DB doubles."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]
with tempfile.TemporaryDirectory(prefix="pb59-receipt-retention-") as temporary:
    binary = Path(temporary) / "receipt.exe"
    subprocess.run(args.compiler + flags + ["-I", str(root / "src/server/game/Entities/Player"),
        "-I", str(root / "src/server/database/Database"),
        str(Path(__file__).with_name("recovery_receipt_retention_checks.cpp")), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
