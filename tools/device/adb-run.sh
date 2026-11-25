#!/usr/bin/env bash
# usage: adb-run.sh <script.sh> [--bg]   (run from Git Bash, WSL or any POSIX shell on the PC)
# Pushes one script from tools/device/ to /data/local/tmp/mali-g2/ and runs it with the device
# shell. The scripts call /system/bin/su themselves, so KernelSU must allow root for "shell".
# --bg detaches it (nohup) for the A/B scripts that wait for the user to start the game.
# Results land in /storage/emulated/0/Download/panvk-*; fetch them with adb pull.
# Replaces tools/work/takeover/run.sh (TermuxService, PRoot only).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
s=$1
dst=/data/local/tmp/mali-g2
adb shell mkdir -p "$dst"
adb push "$here/$s" "$dst/$s" >/dev/null
if [ "${2:-}" = --bg ]; then
   adb shell "nohup sh $dst/$s >/dev/null 2>&1 &"
   echo "started $s in background"
else
   adb shell sh "$dst/$s"
fi
