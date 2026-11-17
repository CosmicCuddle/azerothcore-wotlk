#!/usr/bin/env python3
"""Standalone synthetic PB59 producer boundary tests (not native Worldserver proof)."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="g++")
parser.add_argument("--sanitize", action="store_true")
args = parser.parse_args()
source = Path(__file__).with_name("pb59_producer_boundary_model.cpp")
flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic"]
if args.sanitize:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="pb59-producer-model-") as directory:
    binary = Path(directory) / "producer_model"
    subprocess.run([args.cxx, *flags, str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
