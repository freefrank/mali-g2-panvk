#!/system/bin/sh
# A/B runner for LOR: for each variant set Mesa properties, wait for a fresh LOR process, wait
# until LOR's own log shows a map loaded (map= not "-"), sample 60 s, keep its runtime log, wait
# for LOR to exit, next variant. Diagnostics stay off. Progress in $out/state.txt.
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-ab
mkdir -p $out
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-ab
P=io.github.freefrank.lostodyssey
LOGS=/data/media/0/Android/data/$P/files/logs
S=$out/state.txt
KNOB1=debug.mesa.panvk.g2.vt.no.frag.wait
KNOB2=debug.mesa.panvk.kbase.max.inflight
setprop debug.mesa.panvk.g2.diagnostics 0
echo "start $(date -Iseconds)" > $S
prev=$(pidof $P)
n=0
for variant in base nowait nowait_inflight2; do
  n=$((n+1))
  case $variant in
    base) setprop $KNOB1 0; setprop $KNOB2 1;;
    nowait) setprop $KNOB1 1; setprop $KNOB2 1;;
    nowait_inflight2) setprop $KNOB1 1; setprop $KNOB2 2;;
  esac
  echo "variant $n $variant ready $(date +%T) knob1=$(getprop $KNOB1) knob2=$(getprop $KNOB2) -- waiting for a new LOR process" >> $S
  while :; do cur=$(pidof $P); [ -n "$cur" ] && [ "$cur" != "$prev" ] && break; sleep 2; done
  echo "variant $n pid=$cur started $(date +%T)" >> $S
  sleep 5
  log=$(ls -t $LOGS/runtime-*.log | head -1)
  while :; do
    [ -d /proc/$cur ] || break
    grep "texinfo spec" $log | tail -1 | grep -q "map=[^- ]" && break
    sleep 3
  done
  echo "variant $n in scene $(date +%T) log=$(basename $log)" >> $S
  t=0; while [ $t -lt 60 ] && [ -d /proc/$cur ]; do sleep 2; t=$((t+2)); done
  for f in /proc/$cur/map_files/*; do l=$(readlink $f); case "$l" in *libvulkan_panfrost*|*vulkan.mali*) echo "variant $n driver $l" >> $S; break;; esac; done
  cp $log $out/$n-$variant.log
  echo "variant $n sampled ${t}s $(date +%T) -- please exit LOR" >> $S
  while [ -d /proc/$cur ]; do sleep 2; done
  prev=$cur
  echo "variant $n LOR exited $(date +%T)" >> $S
done
setprop $KNOB1 0; setprop $KNOB2 1
echo "end $(date -Iseconds) knob1=$(getprop $KNOB1) knob2=$(getprop $KNOB2)" >> $S
chmod -R a+r $out
'
echo su_exit=$?
