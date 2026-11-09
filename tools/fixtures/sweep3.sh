#!/bin/bash
cd /root/mali-g2-project/tools
L=work/lab/libvulkan_panfrost.so; S=build/shaders/tiles_sample.comp.spv
run() { local name=$1 drv=$2; shift 2
  echo "$name $(env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 MALI_G2_TILES_TIMING=1 MALI_G2_TILES_PASSES=17 "$@" timeout 120 build/bin/vulkan_tiles $drv $S 1920 1308 41 37 build/shaders/tiles_depth.vert.spv build/shaders/tiles.frag.spv 2>&1 | grep -oE 'TILES_(PASS|FAIL)|render_median_us=[0-9.]+' | paste -sd' ')"; }
for round in 1 2 3; do
  echo "--- round $round"
  for d in /vendor/lib64/hw/vulkan.mali.so $L; do
    n=$(basename $d)
    run "$n rgba8" $d
    run "$n rgba16f" $d MALI_G2_TILES_RT_FORMAT=97
    run "$n rgba16f+d32s8" $d MALI_G2_TILES_RT_FORMAT=97 MALI_G2_TILES_DEPTH_FORMAT=130
    run "$n rgba8+d24s8" $d MALI_G2_TILES_DEPTH_FORMAT=129
  done
done
