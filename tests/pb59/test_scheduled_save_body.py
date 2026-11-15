#!/usr/bin/env python3
"""Compile exact scheduled-save producer bodies against synthetic dependencies."""
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
    source = (native / "PlayerUpdates.cpp").read_text(encoding="utf-8")
    start = "    if (m_nextSave)\n"
    end = "    UpdateAdditionalSaves(p_time);"
    marker = "void Player::UpdateAdditionalSaves(uint32 p_time)"
    if source.count(start) != 1 or source.count(marker) != 1:
        raise RuntimeError("expected unique scheduled save producers")
    pos = source.index(start)
    autosave = source[pos:source.index(end, pos)].rstrip() + "\n"
    pos = source.index(marker)
    opening = source.index("{", pos)
    depth = 1
    cursor = opening + 1
    while depth:
        c = source[cursor]
        depth += (c == "{") - (c == "}")
        cursor += 1
    additional = source[pos:cursor] + "\n"
    fixture = Path(__file__).with_suffix(".cpp")
    with tempfile.TemporaryDirectory(prefix="pb59-scheduled-save-") as tmp:
        work = Path(tmp)
        (work / "AutoSaveTick.inc").write_text(autosave, encoding="utf-8")
        (work / "UpdateAdditionalSaves.inc").write_text(additional, encoding="utf-8")
        binary = work / "scheduled"
        command = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(native), "-I", str(work), str(fixture), "-o", str(binary)]
        if args.sanitizers:
            command.append("-fsanitize=address,undefined")
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: exact scheduled-save producers in synthetic Player")


if __name__ == "__main__":
    main()
