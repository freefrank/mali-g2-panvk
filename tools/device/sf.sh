#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/sf-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
date -Iseconds > $out/sf-time.txt
dumpsys SurfaceFlinger > $out/sf.txt 2>&1
dumpsys activity activities | grep -m2 -E "topResumedActivity|mResumedActivity" >> $out/sf-time.txt
'
echo su_exit=$?
