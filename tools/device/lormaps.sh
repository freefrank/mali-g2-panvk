#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lormaps-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover
pid=$(pidof io.github.freefrank.lostodyssey)
echo pid=$pid > $out/lormaps.txt
grep -E "mali|kbase|vulkan" /proc/$pid/maps >> $out/lormaps.txt
grep -c "/dev/mali0" /proc/$pid/maps >> $out/lormaps.txt
ls -l /proc/$pid/fd 2>/dev/null | grep -c mali >> $out/lormaps.txt
'
echo su_exit=$?
