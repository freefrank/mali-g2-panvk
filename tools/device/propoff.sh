#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/propoff-access.txt" 2>&1
/system/bin/su -c 'setprop debug.mesa.panvk.g2.diagnostics 0; setprop debug.mesa.panvk.debug sync; echo diag=$(getprop debug.mesa.panvk.g2.diagnostics) debug=$(getprop debug.mesa.panvk.debug)'
echo su_exit=$?
