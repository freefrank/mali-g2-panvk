#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/sffps2.txt 2>&1
/system/bin/su -c 'old=$(pidof io.github.freefrank.lostodyssey); n=0; while [ $n -lt 900 ]; do cur=$(pidof io.github.freefrank.lostodyssey); [ -n "$cur" ] && [ "$cur" != "$old" ] && break; sleep 1; n=$((n+1)); done; sleep 60; dumpsys SurfaceFlinger --timestats -clear -enable >/dev/null; for i in 1 2 3 4; do sleep 5; cat /sys/class/devfreq/gpufreq_core/cur_freq; done; dumpsys SurfaceFlinger --timestats -dump | grep -A40 "layerName = .*SurfaceView\[io.github" | grep -E "totalFrames|averageFPS"; dumpsys SurfaceFlinger --timestats -disable >/dev/null; for f in /proc/$(pidof io.github.freefrank.lostodyssey)/map_files/*; do l=$(readlink $f); case "$l" in *panfrost*) echo $l; break;; esac; done'
echo su_exit=$?
