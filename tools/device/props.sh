#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/props-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
getprop | grep -Ei "mesa|panvk|pan_|vulkan|egl|hwui.renderer|debug.vulkan|gpu" > $out/props.txt
for d in /sys/class/devfreq/*; do echo "== $d"; cat $d/name $d/governor $d/cur_freq $d/min_freq $d/max_freq $d/available_frequencies $d/available_governors 2>/dev/null; done > $out/devfreq.txt
ls /sys/class/misc/mali0/device/ > $out/mali-sysfs.txt 2>&1
for f in core_mask core_availability_policy power_policy dvfs_period gpuinfo scheduling/serialize_jobs csg_scheduling_period idle_hysteresis_time mcu_shader_pwroff_timeout; do echo "== $f"; cat /sys/class/misc/mali0/device/$f; done > $out/mali-knobs.txt 2>&1
ls /sys/kernel/gpu/ /sys/devices/platform/*gpu*/ > $out/gpu-dirs.txt 2>&1
'
echo su_exit=$?
