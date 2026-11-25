#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/loaded-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
P=com.miHoYo.Yuanshen.nightly
pid=$(pidof $P)
{ date -Iseconds; echo pid=$pid
  for f in /proc/$pid/map_files/*; do l=$(readlink $f); case "$l" in *vulkan*panfrost*|*gpu_driver*) echo "$l"; sha256sum $f; break;; esac; done
  for t in /proc/$pid/task/*; do cat $t/comm; done | sort | uniq -c | sort -rn | head -12
  dumpsys activity activities | grep -m1 topResumedActivity
  cat /sys/class/devfreq/gpufreq_core/cur_freq
  grep "^driver_path" /storage/emulated/0/Android/data/$P/files/config/config.ini
} > $out/loaded.txt 2>&1
'
echo su_exit=$?
