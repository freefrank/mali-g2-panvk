#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-logs
exec >/storage/emulated/0/Download/panvk-logs/access2.txt 2>&1
/system/bin/su -c '
P=io.github.freefrank.lostodyssey
O=/storage/emulated/0/Download/panvk-logs/gpudir.txt
N="nsenter -t 1 -m --"
D=/data/data/$P/files
{ $N ls -la $D/gpu_driver; date +%H:%M; pidof $P; $N grep -E "driver|gpu" $D/config/settings.ini; } > $O 2>&1
'
echo su_exit=$?
