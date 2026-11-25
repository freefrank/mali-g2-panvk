#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-logs/access6.txt 2>&1
/system/bin/su -c '
P=io.github.freefrank.lostodyssey
L=/data/media/0/Android/data/$P/files/logs
o=/storage/emulated/0/Download/panvk-logs/lslogs.txt
{ ls -la $L; echo "--- native-stderr"; cat $L/native-stderr.log; echo "--- prev"; cat $L/native-stderr.previous.log;
  echo "--- exit-info"; dumpsys activity exit-info $P | grep -E "timestamp|reason|description|status|importance" | head -24;
  echo "--- tombstones"; ls -lt /data/tombstones | head -5; } > $o 2>&1
for f in $(ls -t $L/runtime-*.log | head -3); do cp $f /storage/emulated/0/Download/panvk-logs/; done
chmod -R a+r /storage/emulated/0/Download/panvk-logs
'
echo su_exit=$?
