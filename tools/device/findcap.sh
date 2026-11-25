#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/findcap.txt 2>&1
/system/bin/su -c 'P=io.github.freefrank.lostodyssey; for d in /storage/emulated/0/Android/data/$P /proc/$(pidof $P)/root/data/data/$P /storage/emulated/0/Download /storage/emulated/0/Pictures /storage/emulated/0/Documents; do find $d -mmin -60 -type f 2>/dev/null | grep -v "panvk-\|runtime-\|shader-\|native-stderr\|pipeline_cache" | head -20; done'
echo su_exit=$?
