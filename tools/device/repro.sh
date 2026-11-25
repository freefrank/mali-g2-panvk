#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-repro
mkdir -p "$out"
exec >"$out/access.txt" 2>&1
/system/bin/su -c 'getprop debug.mesa.panvk.g2.diagnostics > /storage/emulated/0/Download/panvk-repro/prop.txt; getprop debug.mesa.panvk.debug >> /storage/emulated/0/Download/panvk-repro/prop.txt; touch /storage/emulated/0/Download/panvk-g2-driver-events.log; chmod 666 /storage/emulated/0/Download/panvk-g2-driver-events.log
out=/storage/emulated/0/Download/panvk-repro
P=io.github.freefrank.lostodyssey
old=$(pidof $P)
echo "start $(date -Iseconds) old_pid=$old" > $out/state.txt
n=0
while [ $n -lt 600 ]; do
  cur=$(pidof $P)
  [ -n "$cur" ] && [ "$cur" != "$old" ] && break
  sleep 1; n=$((n+1))
done
echo "new_pid=$cur after=$n $(date -Iseconds)" >> $out/state.txt
dmesg -w > $out/dmesg.txt 2>&1 &
d=$!
logcat -v threadtime -T 1 > $out/logcat.txt 2>&1 &
l=$!
m=0
while [ $m -lt 180 ] && [ "$(pidof $P)" = "$cur" ]; do sleep 1; m=$((m+1)); done
sleep 3
kill $d $l
echo "end $(date -Iseconds) followed=$m alive=$(pidof $P)" >> $out/state.txt
for f in /proc/$cur/map_files/*; do l2=$(readlink $f); case "$l2" in *panfrost*) echo "$l2" >> $out/state.txt; break;; esac; done
grep -v "avc:" $out/dmesg.txt | grep -iE "mali|kbase|csg|csf|gpu|fault|mmu|shareab" | grep -vE "mali-g2-project|servicemanager|PowerDet" > $out/dmesg-gpu.txt
grep -E "MESA|LostOdyssey|hook_impl|DEBUG|libc " $out/logcat.txt | grep -v "Access denied" > $out/logcat-gpu.txt
chmod -R a+r $out
'
echo su_exit=$?
