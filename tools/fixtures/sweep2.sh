#!/bin/bash
cd /root/mali-g2-project/tools
L=work/lab/libvulkan_panfrost.so
V="build/shaders/tiles.vert.spv build/shaders/tiles.frag.spv"; S=build/shaders/tiles_sample.comp.spv
run() { local name=$1 drv=$2; shift 2
  echo "$name $(env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 MALI_G2_TILES_TIMING=1 MALI_G2_TILES_PASSES=${PASSES:-17} "$@" timeout 120 build/bin/vulkan_tiles $drv $S ${W:-1920} ${H:-1080} 41 37 $V 2>&1 | grep -oE 'TILES_(PASS|FAIL)|render_median_us=[0-9.]+' | paste -sd' ')"; }
for round in 1 2 3; do
  echo "--- round $round  ${W:-1920}x${H:-1080} passes=${PASSES:-17}"
  run vendor_plain /vendor/lib64/hw/vulkan.mali.so
  run vendor_lor /vendor/lib64/hw/vulkan.mali.so MALI_G2_TILES_LOR_BARRIER=1
  run ours_plain $L
  run ours_lor $L MALI_G2_TILES_LOR_BARRIER=1
  run ours_lor_nowait $L MALI_G2_TILES_LOR_BARRIER=1 PANVK_G2_VT_NO_FRAG_WAIT=1
done
