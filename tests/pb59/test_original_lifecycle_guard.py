#!/usr/bin/env python3
"""Compile exact Player unsettled-state observer; check conservative save scope."""
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
header = (player / "Player.h").read_text()
storage = (player / "PlayerStorage.cpp").read_text()
session = (root / "src/server/game/Server/WorldSession.cpp").read_text()
start = header.index("    [[nodiscard]] bool HasPB59UnsettledOriginals() const noexcept")
end = header.index("    void CleanupsBeforeDelete(", start)
methods = header[start:end]
save_start = storage.index("void Player::SaveToDB(bool create, bool logout)")
save_end = storage.index("void Player::SaveToDB(CharacterDatabaseTransaction trans", save_start)
save = storage[save_start:save_end]
assert save.index("!create && !logout && HasPB59UnsettledOriginals()") < save.index("CharacterDatabase.BeginTransaction()")
assert save.index("ScheduleDelayedOperation(DELAYED_SAVE_PLAYER)") < save.index("CharacterDatabase.BeginTransaction()")
logout_start = session.index("void WorldSession::LogoutPlayer(bool save, bool redirecting)")
logout = session[logout_start:session.index("m_playerLogout = true;", logout_start)]
assert "RetainPB59OwnerForUnsettledLogout" not in logout
assert "LogoutPlayer(true)" in session[session.index("WorldSession::~WorldSession()"):]
# Destructor-invoked logout and caller-owned DB writes remain unprotected release blockers.
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / "native_lifecycle.inc").write_text(methods)
    exe = work / "lifecycle"
    flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run([args.cxx, *flags, "-I", str(player), "-I", str(work),
        str(Path(__file__).with_name("pb59_original_lifecycle_native.cpp")), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print("non-logout standalone save source gate: PASS; destructor logout intentionally not claimed safe")
