#!/usr/bin/env bash

ACCEL=false
while getopts ":as:" opt; do
  case $opt in
    a) ACCEL=true
    ;;
    s) SOURCE="$OPTARG"
    ;;
    :) echo "\"-${OPTARG}\" requires an argument"
    exit 1
    ;;
    \?) echo "Invalid option -$OPTARG" >&2
    exit 1
    ;;
  esac
done

if $ACCEL
then
  # -filter:v format=nv12\|vaapi,hwupload,scale_vaapi=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:-1:-1:color=black \
  echo "Using hardware acceleration..."
  ffmpeg \
  -use_wallclock_as_timestamps 1 \
  -vaapi_device /dev/dri/renderD128 \
  -i ${SOURCE} \
  -y \
  -acodec copy \
  -noautoscale \
  -vf "format=nv12|vaapi,scale=1920:1080:force_original_aspect_ratio=1,pad=1920:1080:(ow-iw)/2:(oh-ih)/2:color=black,hwupload" \
  -vcodec h264_vaapi \
  -profile:v main \
  -level 4.1 \
  -start_number 0 \
  -hls_time 5000ms \
  -hls_list_size 0 \
  -r 30 -g 150 \
  -f hls /home/dmancini/Downloads/Accelerated/Accelerated.m3u8

  # ffmpeg \
  # -init_hw_device vaapi=intelgpu:/dev/dri/renderD128 \
  # -hwaccel vaapi \
  # -hwaccel_output_format vaapi \
  # -hwaccel_device intelgpu \
  # -filter_hw_device intelgpu \
  # -i ${SOURCE} \
  # -y \
  # -acodec copy \
  # -vcodec h264_vaapi \
  # -filter:v format=nv12|vaapi,hwupload \
  # -use_wallclock_as_timestamps true \
  # -profile:v main \
  # -level 3.0 \
  # -start_number 0 \
  # -hls_time 5000ms \
  # -hls_list_size 0 \
  # -r 30 -g 150 \
  # -f hls /var/cache/nginx/9dde3c9e-ae98-11ec-af5f-67ba4a21992e/9dde3c9e-ae98-11ec-af5f-67ba4a21992e.m3u8


else
  echo "Not using hardware acceleration..."
  # echo ${SOURCE}
  # udp://239.255.0.1:9093?reuse=1\&fifo_size=5000000\&overrun_nonfatal=1
  # ffmpeg -i ${SOURCE} -y -acodec copy -use_wallclock_as_timestamps true -profile:v main -level 3.0 -start_number 0 -hls_time 5000ms -hls_list_size 0 -r 30 -g 150 -aspect 16/9 -f hls /home/dmancini/Downloads/Unaccelerated/Unaccelerated.m3u8
  ffmpeg \
    -use_wallclock_as_timestamps 1 \
    -i ${SOURCE} \
    -y \
    -noautoscale \
    -acodec copy \
    -use_wallclock_as_timestamps true \
    -profile:v main \
    -level 4.1 \
    -start_number 0 \
    -hls_time 5000ms \
    -hls_list_size 0 \
    -r 30 \
    -g 150 \
    -filter:v scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:-1:-1:color=black \
    -f hls /home/dmancini/Downloads/Unaccelerated/Unaccelerated.m3u8
fi
 
# NOTES:
# Play a still image on a loop example: ffmpeg -re -loop 1 -i 1200px-RCA_Indian_Head_Test_Pattern.svg.png -r 10 -vcodec h264 -f mpegts udp://localhost:9
# Input for still image: "udp://localhost:9?reuse=1&fifo_size=5000000&overrun_nonfatal=1"