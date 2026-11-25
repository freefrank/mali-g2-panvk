#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
exec >/storage/emulated/0/Download/panvk-logs/kill1.txt 2>&1
/system/bin/su -c 'kill -9 14795; echo done'
echo su_exit=$?
