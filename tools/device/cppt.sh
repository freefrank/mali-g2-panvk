#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-logs
exec >/storage/emulated/0/Download/panvk-logs/access4.txt 2>&1
/system/bin/su -c '
s=/data/media/0/Android/data/io.github.freefrank.lostodyssey/files/logs
d=/storage/emulated/0/Download/panvk-logs
cp $s/pass-timing.txt $d/pass-timing.txt; for f in $s/runtime-*.log; do cp $f $d/; done; chmod -R a+r $d; ls -la $s
'
echo su_exit=$?
