#!/usr/bin/env python3
"""Compile the exact native link-relocation body against a synthetic inventory."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    native = root / "src/server/game/Entities/Player"
    fixture = Path(__file__).with_suffix(".cpp")
    source = (native / "PlayerStorage.cpp").read_text(encoding="utf-8")
    marker = "PB59::LinkTransferResult Player::RelocatePB59InventoryLinks("
    if source.count(marker) != 1:
        raise RuntimeError("expected exactly one native custody method")
    start = source.index(marker)
    end = source.index("Bag* Player::GetBagByPos", start)
    body = source[start:end].rstrip() + "\n"
    with tempfile.TemporaryDirectory(prefix="pb59-custody-") as tmp:
        work = Path(tmp)
        (work / "RelocatePB59InventoryLinks.inc").write_text(body, encoding="utf-8")
        binary = work / "custody"
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(native), "-I", str(work), str(fixture), "-o", str(binary)]
        if args.sanitizers:
            command.append("-fsanitize=address,undefined")
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: exact native custody body in synthetic inventory")


if __name__ == "__main__":
    main()
