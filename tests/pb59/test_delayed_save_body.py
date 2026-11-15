#!/usr/bin/env python3
"""Compile exact delayed-operation and autosave bodies with synthetic gameplay."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    if source.count(start) != 1:
        raise RuntimeError(f"expected unique source marker: {start}")
    pos = source.index(start)
    return source[pos:source.index(end, pos)].rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--sanitizers", action="store_true")
    args = parser.parse_args()
    native = Path(__file__).resolve().parents[2] / "src/server/game/Entities/Player"
    player = (native / "Player.cpp").read_text(encoding="utf-8")
    updates = (native / "PlayerUpdates.cpp").read_text(encoding="utf-8")
    delayed = between(player, "void Player::ProcessDelayedOperations()", "void Player::AddToWorld()")
    autosave = between(updates, "    if (m_nextSave)\n", "    UpdateAdditionalSaves(p_time);")
    fixture = Path(__file__).with_suffix(".cpp")
    with tempfile.TemporaryDirectory(prefix="pb59-delayed-save-") as tmp:
        work = Path(tmp)
        (work / "ProcessDelayedOperations.inc").write_text(delayed, encoding="utf-8")
        (work / "AutoSaveTick.inc").write_text(autosave, encoding="utf-8")
        binary = work / "delayed"
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(native), "-I", str(work), str(fixture), "-o", str(binary)]
        if args.sanitizers:
            command.append("-fsanitize=address,undefined")
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: exact delayed save and autosave bodies with synthetic gameplay")


if __name__ == "__main__":
    main()
