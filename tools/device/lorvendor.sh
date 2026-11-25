#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-lor-vendor-hdr
mkdir -p "$out"
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-lor-vendor-hdr
rm -f $out/*.txt.part
n=0
while [ $n -lt 600 ]; do
 dumpsys activity activities | grep topResumedActivity | grep -q lostodyssey && break
 sleep 2; n=$((n+2))
done
echo waited=$n > $out/wait.txt
sleep 3
date -Iseconds > $out/time.txt
dumpsys activity activities | grep -m1 topResumedActivity >> $out/time.txt
dumpsys SurfaceFlinger --timestats -clear -enable >/dev/null 2>&1
dumpsys SurfaceFlinger > $out/sf-before.txt 2>&1
for i in 1 2 3 4 5; do cat /sys/class/devfreq/gpufreq_core/cur_freq; cat /sys/class/misc/mali0/device/utilization 2>/dev/null; sleep 1; done > $out/freq.txt 2>&1 &
/data/local/tmp/mali-g2/bin/counter_percore > $out/counters.json 2> $out/counters.err
wait
dumpsys SurfaceFlinger --timestats -dump > $out/timestats.txt 2>&1
dumpsys SurfaceFlinger --timestats -disable >/dev/null 2>&1
dumpsys SurfaceFlinger > $out/sf-after.txt 2>&1
cp $(ls -t /storage/emulated/0/Android/data/io.github.freefrank.lostodyssey/files/logs/runtime-*.log | head -1) $out/lor-runtime.log
for f in /proc/$(pidof io.github.freefrank.lostodyssey)/map_files/*; do l=$(readlink $f); case "$l" in *vulkan*) echo $l > $out/driver.txt; break;; esac; done
cat /sys/class/misc/mali0/device/core_mask >> $out/driver.txt
date -Iseconds >> $out/time.txt
'
echo su_exit=$?
