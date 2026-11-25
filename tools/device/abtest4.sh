#!/system/bin/sh
# A/B for UI jank: in-flight 1 then 2. For each: wait for a fresh LOR process (tracked by pid),
# wait until LOR's log shows a map, reset gfxinfo of SystemUI and the launcher, then for 60 s
# take one SurfaceFlinger frametimeline snapshot per second (display-frame jank types, about
# 111 frames = 0.93 s each) plus the GPU frequency; then dump gfxinfo and keep LOR's log.
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-ab4
mkdir -p $out
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-ab4
P=io.github.freefrank.lostodyssey
LOGS=/data/media/0/Android/data/$P/files/logs
S=$out/state.txt
F=/sys/class/devfreq/gpufreq_core/cur_freq
KNOB1=debug.mesa.panvk.g2.vt.no.frag.wait
KNOB2=debug.mesa.panvk.kbase.max.inflight
KNOB3=debug.mesa.panvk.kbase.group.priority
SP=/sys/class/misc/mali0/device/csg_scheduling_period
SP0=$(cat $SP)
setprop debug.mesa.panvk.g2.diagnostics 0
setprop $KNOB1 0
echo "start $(date -Iseconds)" > $S
prev=$(pidof $P)
n=0
for variant in low period20 low_period20; do
  n=$((n+1))
  case $variant in
    low) setprop $KNOB2 2; setprop $KNOB3 2; echo $SP0 > $SP;;
    period20) setprop $KNOB2 2; setprop $KNOB3 -1; echo 20 > $SP;;
    low_period20) setprop $KNOB2 2; setprop $KNOB3 2; echo 20 > $SP;;
  esac
  echo "variant $n $variant ready $(date +%T) inflight=$(getprop $KNOB2) prio=$(getprop $KNOB3) period=$(cat $SP) -- waiting for a new LOR process" >> $S
  while :; do cur=$(pidof $P); [ -n "$cur" ] && [ "$cur" != "$prev" ] && break; sleep 2; done
  echo "variant $n pid=$cur started $(date +%T)" >> $S
  sleep 5
  log=$(ls -t $LOGS/runtime-*.log | head -1)
  while [ -d /proc/$cur ]; do
    grep "texinfo spec" $log | tail -1 | grep -q "map=[^- ]" && break
    sleep 3
  done
  echo "variant $n in scene $(date +%T) log=$(basename $log)" >> $S
  dumpsys gfxinfo com.android.systemui reset </dev/null >/dev/null 2>&1
  dumpsys gfxinfo com.miui.home reset </dev/null >/dev/null 2>&1
  t=0
  while [ $t -lt 60 ] && [ -d /proc/$cur ]; do
    echo "$t $(cat $F)" >> $out/$n-$variant-freq.txt
    dumpsys SurfaceFlinger --frametimeline -jank </dev/null 2>/dev/null | grep -A2 "^Display Frame" | grep "Jank Type" | sed "s/^/$t /" >> $out/$n-$variant-sfjank.txt
    sleep 1; t=$((t+1))
  done
  dumpsys gfxinfo com.android.systemui </dev/null > $out/$n-$variant-gfx-systemui.txt 2>&1
  dumpsys gfxinfo com.miui.home </dev/null > $out/$n-$variant-gfx-home.txt 2>&1
  for f in /proc/$cur/map_files/*; do l=$(readlink $f); case "$l" in *libvulkan_panfrost*|*vulkan.mali*) echo "variant $n driver $l" >> $S; break;; esac; done
  cp $log $out/$n-$variant.log
  echo "variant $n sampled ${t}s $(date +%T) -- please exit LOR" >> $S
  while [ -d /proc/$cur ]; do sleep 2; done
  prev=$cur
  echo "variant $n LOR exited $(date +%T)" >> $S
done
setprop $KNOB2 1; setprop $KNOB3 -1; echo $SP0 > $SP
echo "end $(date -Iseconds) inflight=$(getprop $KNOB2) prio=$(getprop $KNOB3) period=$(cat $SP)" >> $S
chmod -R a+r $out
'
echo su_exit=$?
