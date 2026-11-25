#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-jank/probe2-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-jank
dumpsys SurfaceFlinger --frametimeline -jank </dev/null > $o/ft2.txt 2>&1
pm list packages 2>/dev/null | grep -iE "miui.home|launcher|systemui" > $o/pkgs.txt
dumpsys activity recents </dev/null 2>/dev/null | grep -m3 -iE "realActivity" >> $o/pkgs.txt
chmod -R a+r $o
'
echo su_exit=$?
