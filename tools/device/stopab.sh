#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-logs/stopab.txt 2>&1
/system/bin/su -c '
setprop debug.mesa.panvk.g2.vt.no.frag.wait 0
setprop debug.mesa.panvk.kbase.max.inflight 1
echo "knob1=$(getprop debug.mesa.panvk.g2.vt.no.frag.wait) knob2=$(getprop debug.mesa.panvk.kbase.max.inflight) diag=$(getprop debug.mesa.panvk.g2.diagnostics)"
'
echo su_exit=$?
