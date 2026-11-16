#!/usr/bin/env python3
"""Run exact PB59 original queue methods in a narrow C++ mock."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
player = root / "src/server/game/Entities/Player"
source = (player / "PlayerStorage.cpp").read_text()
start = source.index("bool Player::SuspendPB59OriginalQueue(")
queue_end = source.index("PB59::LinkTransferResult Player::HoldPB59OriginalItem(", start)
end = source.index("PB59::LinkTransferResult Player::RelocatePB59InventoryLinks(", start)
methods = source[start:end]
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.sanitize:
        flags.append("-fsanitize=address,undefined")
    for filename in ["pb59_original_queue_custody.cpp", "pb59_original_queue_native.cpp",
                     "pb59_original_pointer_custody.cpp", "pb59_original_pointer_native.cpp"]:
        (work / "native_methods.inc").write_text(
            source[start:queue_end] if filename == "pb59_original_queue_native.cpp" else methods)
        exe = work / Path(filename).stem
        subprocess.run([args.cxx, *flags, "-I", str(player), "-I", str(work),
                        str(Path(__file__).with_name(filename)), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
