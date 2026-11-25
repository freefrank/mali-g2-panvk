#!/system/bin/sh
# gpupin.sh: reads the request from /storage/emulated/0/Download/panvk-pin/req ("restore" or a
# frequency in Hz), pins min_freq=max_freq to it (original saved once), writes state.
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-pin
exec >/storage/emulated/0/Download/panvk-pin/access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-pin
D=/sys/class/devfreq/gpufreq_core
req=$(cat $o/req)
[ -f $o/orig ] || echo "$(cat $D/min_freq) $(cat $D/max_freq)" > $o/orig
set -- $(cat $o/orig); omin=$1; omax=$2
if [ "$req" = restore ]; then
  echo $omax > $D/max_freq; echo $omin > $D/min_freq; rm -f $o/orig
else
  echo $omax > $D/max_freq; echo $req > $D/min_freq; echo $req > $D/max_freq
fi
{ echo "req=$req"; echo "avail=$(cat $D/available_frequencies)"; echo "min=$(cat $D/min_freq) max=$(cat $D/max_freq) cur=$(cat $D/cur_freq) gov=$(cat $D/governor)"; } > $o/state.txt
'
echo su_exit=$?
