#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
P=com.miHoYo.Yuanshen.nightly
date -Iseconds > $out/time.txt
pid=$(pidof $P); echo "$pid" > $out/pid.txt
for t in /proc/$pid/task/*; do cat $t/comm; done | sort | uniq -c | sort -rn > $out/threads.txt
dumpsys activity activities | grep -E "topResumedActivity|mResumedActivity" > $out/top.txt
dumpsys activity exit-info $P | head -n 60 > $out/exit-info.txt
ls -l /data/tombstones/ > $out/tombstones.txt
dumpsys package $P | grep -E "versionName|lastUpdateTime" > $out/version.txt
L=/storage/emulated/0/Android/data/$P/files/log
ls -l $L > $out/logs.txt
tail -n 40 $L/eden_log.txt > $out/eden-tail.txt
'
echo su_exit=$?
