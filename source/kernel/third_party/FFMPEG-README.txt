FFmpeg 8.1.2 (ffmpeg-rockchip fork) goes in this folder.
It ships as a separate zip because of the 30 MB upload limit:
unzip "NoovexOS-64-v9.11-ffmpeg.zip" inside source/kernel/third_party/ so that
source/kernel/third_party/ffmpeg/configure exists. build.sh builds it once into build/ffmpeg.
