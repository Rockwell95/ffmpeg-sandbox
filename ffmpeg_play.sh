#!/usr/bin/env bash

ACCEL=false
AUDIO_ONLY=false
KLV=false
while getopts ":akns:" opt; do
  case $opt in
    a) ACCEL=true
    ;;
    n) AUDIO_ONLY=true
    ;;
    k) KLV=true
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

if $AUDIO_ONLY
then
  ffmpeg \
  -f s16le \
  -sample_rate 44100 \
  -use_wallclock_as_timestamps true \
  -i ${SOURCE} \
  -af aresample=async=1 \
  -acodec libmp3lame \
  -start_number 0 \
  -hls_time 5000ms \
  -hls_list_size 0 \
  -f hls /home/dmancini/Downloads/Audio/Audio.m3u8
elif $KLV
then
  echo "Dumping KLV"
  ffmpeg \
  -use_wallclock_as_timestamps 1 \
  -i ${SOURCE} \
  -noautoscale \
  -use_wallclock_as_timestamps true \
  -map 0:1 \
  -f data /home/dmancini/Downloads/klv.bin
elif $ACCEL
then
  # -filter:v format=nv12\|vaapi,hwupload,scale_vaapi=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:-1:-1:color=black \
  echo "Using hardware acceleration..."
  ffmpeg \
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
    -loglevel trace \
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
    -f hls /home/dmancini/Downloads/Unaccelerated/Unaccelerated.m3u8 2>&1 | tee ffmpeg.log
fi
 
# NOTES:
# Play a still image on a loop example: ffmpeg -vaapi_device /dev/dri/renderD128 -re -loop 1 -i 1200px-RCA_Indian_Head_Test_Pattern.svg.png -r 30 -g 10 -vcodec mpeg2_vaapi -vf "format=nv12|vaapi,scale=1280:720:force_original_aspect_ratio=1,pad=1280:720:(ow-iw)/2:(oh-ih)/2:color=black,hwupload" -f mpegts udp://localhost:9003
# Input for still image: "udp://localhost:9003?reuse=1&fifo_size=5000000&overrun_nonfatal=1"