#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo6-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover/lor
P=io.github.freefrank.lostodyssey
D=/storage/emulated/0/Android/data/$P/files/logs
rm -rf $out; mkdir -p $out
ls -lt $D > $out/ls.txt
cp $(ls -t $D/runtime-*.log | head -2) $D/native-stderr*.log $out/
pid=$(pidof $P); echo "pid=$pid" > $out/state.txt
[ -n "$pid" ] && ls -la /proc/$pid/root/data/data/$P/files/gpu_driver >> $out/state.txt
[ -n "$pid" ] && for f in /proc/$pid/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*) echo "$l" >> $out/state.txt; sha256sum $f >> $out/state.txt; break;; esac; done
dumpsys activity exit-info $P | grep -E -A3 "ApplicationExitInfo #[0-2]:" >> $out/state.txt
ls -lt /data/tombstones | head -3 >> $out/state.txt
dmesg | grep -iE "mali|kbase|csf|gpu.*fault|mmu" | tail -30 > $out/dmesg.txt
chmod -R a+r $out
'
echo su_exit=$?
