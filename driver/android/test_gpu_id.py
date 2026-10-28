#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default=os.environ.get("CC") or shutil.which("cc"))
parser.add_argument("--device")
args = parser.parse_args()
if not args.cc:
    parser.error("specify --cc or set CC")

root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
if "/com.termux/" in args.cc:
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("LD_PRELOAD", None)

with tempfile.TemporaryDirectory(prefix="panvk-gpu-id-") as directory:
    for name, options in [("debug", ["-O0", "-g"]), ("release", ["-O2", "-DNDEBUG"])]:
        output = Path(directory) / name
        command = [args.cc]
        if "clang" in Path(args.cc).name:
            command.append("-no-canonical-prefixes")
        command += ["-std=c11", "-Wall", "-Wextra", "-Werror", *options,
                    "-I" + str(root / "src"),
                    str(root / "android/tests/kbase_gpu_id.c"),
                    str(root / "src/panfrost/model/pan_model.c"),
                    "-o", str(output)]
        subprocess.run(command, env=env, check=True)
        print(name, flush=True)
        command = [str(output)]
        if args.device:
            command.append(args.device)
        subprocess.run(command, env=env, check=True)
