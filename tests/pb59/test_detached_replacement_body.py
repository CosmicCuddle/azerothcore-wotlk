#!/usr/bin/env python3
"""Compile the exact Core methods with mocked dependencies. No server/DB use."""
import argparse
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
DIR = ROOT / "src/server/game/Entities/Item"
FIXTURE = pathlib.Path(__file__).with_name("test_detached_replacement_body.cpp")

def excerpt(text, first, following):
    start = text.index(first)
    return text[start:text.index(following, start)]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    item = (DIR / "Item.cpp").read_text()
    enchant = (DIR / "ItemEnchantmentMgr.cpp").read_text()
    snippets = {
        "factory.inc": excerpt(item, "std::unique_ptr<Item> Item::CreatePB59Detached", "Item* Item::CloneItem"),
        "init.inc": excerpt(item, "bool Item::Create(ObjectGuid::LowType", "// Returns true if Item is a bag"),
        "membership.inc": excerpt(enchant, "bool HasItemEnchantMod", "uint32 GetItemEnchantMod"),
    }
    with tempfile.TemporaryDirectory(prefix="pb59-detached-") as tmp:
        d = pathlib.Path(tmp)
        for name, source in snippets.items():
            (d / name).write_text(source)
        (d / "test.cpp").write_text(FIXTURE.read_text())
        cmd = [args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror"]
        if args.sanitize:
            cmd += ["-fsanitize=address,undefined"]
        subprocess.run(cmd + [str(d / "test.cpp"), "-o", str(d / "test")], check=True)
        subprocess.run([str(d / "test")], check=True)

if __name__ == "__main__":
    main()
