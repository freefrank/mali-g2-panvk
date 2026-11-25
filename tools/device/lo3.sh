#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo3-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover
P=io.github.freefrank.lostodyssey
pid=$(pidof $P)
R=/proc/$pid/root
{ echo pid=$pid
  dumpsys package $P | grep -E "versionName|lastUpdateTime|dataDir" | head -5
  echo "## maps vulkan"; grep -E "vulkan|panfrost|mali|gpu_driver" /proc/$pid/maps | awk "{print \$6}" | sort -u
  for f in /proc/$pid/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*|*gpu_driver*|*adrenotools*) echo "$l"; sha256sum $f; break;; esac; done
  echo "## threads"; for t in /proc/$pid/task/*; do cat $t/comm; done | sort | uniq -c | sort -rn | head -15
  echo "## data"; ls -laR $R/data/data/$P/files 2>&1 | head -60
  echo "## ext"; ls -laR /storage/emulated/0/Android/data/$P 2>&1 | head -60
  echo "## exit"; dumpsys activity exit-info $P | grep -E -A4 "ApplicationExitInfo #[0-4]:" | head -40
} > $out/lo3.txt 2>&1
'
echo su_exit=$?
