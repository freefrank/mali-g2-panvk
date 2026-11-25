#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-jank
exec >/storage/emulated/0/Download/panvk-jank/probe-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-jank
dumpsys SurfaceFlinger --frametimeline -jank </dev/null 2>&1 | head -80 > $o/ft-jank.txt
dumpsys SurfaceFlinger --frametimeline -jank </dev/null 2>&1 | wc -l > $o/ft-jank-lines.txt
dumpsys gfxinfo com.android.systemui reset </dev/null > /dev/null 2>&1
sleep 3
dumpsys gfxinfo com.android.systemui </dev/null 2>&1 | head -60 > $o/gfx-sysui.txt
dumpsys SurfaceFlinger --timestats -dump -maxlayers 3 </dev/null 2>&1 | head -60 > $o/timestats.txt
chmod -R a+r $o
'
echo su_exit=$?
