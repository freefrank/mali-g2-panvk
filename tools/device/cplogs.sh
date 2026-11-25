#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-logs
exec >/storage/emulated/0/Download/panvk-logs/access.txt 2>&1
/system/bin/su -c '
d=/storage/emulated/0/Download/panvk-logs; mkdir -p $d
for f in /data/media/0/Android/data/io.github.freefrank.lostodyssey/files/logs/runtime-*.log; do cp $f $d/; done
chmod -R a+r $d
'
echo su_exit=$?
