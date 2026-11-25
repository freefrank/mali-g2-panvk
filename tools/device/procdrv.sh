#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-logs
exec >/storage/emulated/0/Download/panvk-logs/access3.txt 2>&1
/system/bin/su -c '
pid=$(pidof io.github.freefrank.lostodyssey)
O=/storage/emulated/0/Download/panvk-logs/proc.txt
{ echo pid=$pid; stat -c "start %y" /proc/$pid; grep -o "gpu_driver/[^/ ]*" /proc/$pid/maps | sort -u; date; } > $O 2>&1
'
echo su_exit=$?
