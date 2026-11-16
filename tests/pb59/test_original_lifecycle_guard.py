#!/usr/bin/env python3
"""Compile exact Player lifecycle methods and verify early save/logout gates."""
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
assert save.index("HasPB59UnsettledOriginals()") < save.index("CharacterDatabase.BeginTransaction()")
assert save.index("ScheduleDelayedOperation(DELAYED_SAVE_PLAYER)") < save.index("CharacterDatabase.BeginTransaction()")
logout_start = session.index("void WorldSession::LogoutPlayer(bool save, bool redirecting)")
logout = session[logout_start:session.index("m_playerLogout = true;", logout_start)]
assert logout.index("RetainPB59OwnerForUnsettledLogout()") < logout.index("HandleMoveWorldportAck()")
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
    print("early standalone-save and normal-logout source gates: PASS")
