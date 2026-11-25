#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-logs
exec >/storage/emulated/0/Download/panvk-logs/access5.txt 2>&1
/system/bin/su -c '
P=io.github.freefrank.lostodyssey
{ dumpsys package $P | grep -E "versionName|versionCode|lastUpdateTime|firstInstallTime"; date; 
  s=/data/media/0/Android/data/$P/files/logs; cat $s/native-stderr.log; cat $s/native-stderr.previous.log; } > /storage/emulated/0/Download/panvk-logs/pkg.txt 2>&1
'
echo su_exit=$?
