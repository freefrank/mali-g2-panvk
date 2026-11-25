#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo7-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover
P=io.github.freefrank.lostodyssey
pid=$(pidof $P)
{ date -Iseconds; echo pid=$pid
  for f in /proc/$pid/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*) echo "$l"; break;; esac; done
  dumpsys activity activities | grep -m1 topResumedActivity
  ls -l --full-time /storage/emulated/0/Android/data/$P/files/logs | head -4
  grep -E "State|voluntary" /proc/$pid/status
  for t in /proc/$pid/task/*; do echo "$(cat $t/comm) $(cut -d" " -f3,14,15 $t/stat)"; done | sort -k3 -n -r | head -12
} > $out/lo7.txt 2>&1
'
echo su_exit=$?
