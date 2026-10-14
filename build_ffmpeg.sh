#!/bin/bash
# Build FFmpeg tối giản (chỉ RTSP + H.264/H.265 giải mã) thành thư viện tĩnh.
# Chạy trong Docker toolchain tg5040 (CROSS_COMPILE được toolchain đặt sẵn).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
PREFIX="$ROOT/ffmpeg-install"
cd "$ROOT/FFmpeg"
make distclean >/dev/null 2>&1 || true
CROSS_ARGS=""
if [ -n "$CROSS_COMPILE" ]; then
  CROSS_ARGS="--enable-cross-compile --cross-prefix=$CROSS_COMPILE --arch=aarch64 --target-os=linux"
fi
./configure --prefix="$PREFIX" $CROSS_ARGS $FF_EXTRA \
  --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-debug \
  --disable-avdevice --disable-avfilter --disable-postproc \
  --enable-network --enable-static --disable-shared --enable-pic \
  --enable-protocol=tcp,udp,rtp,file \
  --enable-demuxer=rtsp,rtp,sdp \
  --enable-decoder=h264,hevc,mjpeg \
  --enable-parser=h264,hevc,mjpeg
make -j"$(nproc)"
make install
