#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-shaderdiag2
mkdir -p $out
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-shaderdiag2
L=/storage/emulated/0/Download/panvk-g2-driver-events.log
P=io.github.freefrank.lostodyssey
: > $L; chmod 666 $L
setprop debug.mesa.panvk.debug none
setprop debug.mesa.panvk.g2.diagnostics 1
old=$(pidof $P)
echo "start $(date -Iseconds) old=$old diag=$(getprop debug.mesa.panvk.g2.diagnostics) debug=$(getprop debug.mesa.panvk.debug)" > $out/state.txt
n=0; while [ $n -lt 900 ]; do cur=$(pidof $P); [ -n "$cur" ] && [ "$cur" != "$old" ] && break; sleep 1; n=$((n+1)); done
echo "new=$cur after=$n" >> $out/state.txt
n=0; while [ $n -lt 300 ]; do dumpsys activity activities | grep topResumedActivity | grep -q lostodyssey && break; sleep 2; n=$((n+2)); done
sleep 75
setprop debug.mesa.panvk.g2.diagnostics 0
setprop debug.mesa.panvk.debug sync
cp $L $out/events.log
cp $(ls -t /storage/emulated/0/Android/data/$P/files/logs/runtime-*.log | head -1) $out/lor-runtime.log
for f in /proc/$cur/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*) echo "$l" >> $out/state.txt; break;; esac; done
echo "end $(date -Iseconds) diag=$(getprop debug.mesa.panvk.g2.diagnostics) debug=$(getprop debug.mesa.panvk.debug)" >> $out/state.txt
chmod -R a+r $out
'
echo su_exit=$?
