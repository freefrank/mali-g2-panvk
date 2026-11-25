#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/tib-access.txt 2>&1
/system/bin/su -c "setprop debug.mesa.panvk.g2.tib.size $(cat /storage/emulated/0/Download/panvk-takeover/tib-value.txt); echo tib=\$(getprop debug.mesa.panvk.g2.tib.size) diag=\$(getprop debug.mesa.panvk.g2.diagnostics)"
echo su_exit=$?
