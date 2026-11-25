#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/inflight-access.txt" 2>&1
/system/bin/su -c "setprop debug.mesa.panvk.kbase.max.inflight $(cat /storage/emulated/0/Download/panvk-takeover/inflight-value.txt); echo inflight=\$(getprop debug.mesa.panvk.kbase.max.inflight) diag=\$(getprop debug.mesa.panvk.g2.diagnostics)"
echo su_exit=$?
