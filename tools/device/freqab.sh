#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-freqab
mkdir -p "$out"
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-freqab
D=/sys/class/devfreq/gpufreq_core
P=io.github.freefrank.lostodyssey
orig=$(cat $D/min_freq)
echo "orig_min=$orig max=$(cat $D/max_freq) gov=$(cat $D/governor)" > $out/state.txt
n=0
while [ $n -lt 600 ]; do
 dumpsys activity activities | grep topResumedActivity | grep -q lostodyssey && break
 sleep 2; n=$((n+2))
done
echo "waited=$n" >> $out/state.txt
sleep 5
log=$(ls -t /storage/emulated/0/Android/data/$P/files/logs/runtime-*.log | head -1)
echo "log=$log" >> $out/state.txt
for phase in base lock; do
  if [ $phase = lock ]; then echo 1248000000 > $D/min_freq; echo "set_min=$(cat $D/min_freq)" >> $out/state.txt; sleep 2; fi
  echo "$phase start $(date +%s.%N)" >> $out/state.txt
  i=0; while [ $i -lt 30 ]; do echo "$phase $(date +%s) $(cat $D/cur_freq) $(cat /sys/class/misc/mali0/device/utilization 2>/dev/null)"; sleep 1; i=$((i+1)); done >> $out/freq.txt
  echo "$phase end $(date +%s.%N)" >> $out/state.txt
done
echo $orig > $D/min_freq
echo "restored_min=$(cat $D/min_freq)" >> $out/state.txt
cp $log $out/lor-runtime.log
for f in /proc/$(pidof $P)/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*) echo "$l" >> $out/state.txt; break;; esac; done
chmod -R a+r $out
'
echo su_exit=$?
