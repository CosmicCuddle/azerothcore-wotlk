"""Compile the actual service class with isolated doubles (no native server or SQL)."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compiler", nargs="+", required=True)
parser.add_argument("--ndebug", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / "src/server/game/Entities/Player/PlayerStorage.cpp").read_text(encoding="utf-8")
start = source.index("class Player::PB59InventorySaveService final")
end = source.index("\nvoid Player::SaveToDB(bool create, bool logout)", start)
service = source[start:end]
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.ndebug:
    flags += ["-DNDEBUG", "-O2"]
with tempfile.TemporaryDirectory(prefix="pb59-native-service-") as temp:
    work = Path(temp)
    (work / "service.inc").write_text(service, encoding="utf-8")
    binary = work / "service_checks"
    subprocess.run(args.compiler + flags + ["-I", str(work),
        str(Path(__file__).with_name("native_service_readback_checks.cpp")), "-o", str(binary)],
        check=True, timeout=90)
    subprocess.run([str(binary)], check=True, timeout=30)
