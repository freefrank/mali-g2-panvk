#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo4-access.txt" 2>&1
/system/bin/su -c 'mkdir -p /storage/emulated/0/Download/panvk-takeover/lor && cp /storage/emulated/0/Android/data/io.github.freefrank.lostodyssey/files/logs/* /storage/emulated/0/Download/panvk-takeover/lor/ && cp /proc/$(pidof io.github.freefrank.lostodyssey)/root/data/data/io.github.freefrank.lostodyssey/files/config/settings.ini /storage/emulated/0/Download/panvk-takeover/lor/ 2>/dev/null; chmod -R a+r /storage/emulated/0/Download/panvk-takeover/lor'
echo su_exit=$?
