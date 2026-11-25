#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/lorall-access.txt 2>&1
/system/bin/su -c 'D=/storage/emulated/0/Android/data/io.github.freefrank.lostodyssey/files/logs; mkdir -p /storage/emulated/0/Download/panvk-takeover/lorall; cp $D/runtime-*.log /storage/emulated/0/Download/panvk-takeover/lorall/; chmod -R a+r /storage/emulated/0/Download/panvk-takeover/lorall'
echo su_exit=$?
