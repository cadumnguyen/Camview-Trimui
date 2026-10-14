# CamView 2 - Xem dau ghi KBVision/Dahua (RTSP) tren Trimui Brick / Brick Pro

## Build (GitHub Actions)
Upload cac file thuong len repo (main.c, Makefile, font8x8_basic.h, build_ffmpeg.sh, launch.sh, config.json, nvr.conf)
va giu workflow .github/workflows/main.yml. Tab Actions -> tai artifact CamView-tg5040.
(Workflow can chay "bash ./build_ffmpeg.sh" vi file upload qua web khong co quyen thuc thi.)

## Cai dat
- NextUI/MinUI : SD:/Tools/tg5040/CamView.pak/{camview,launch.sh,nvr.conf}
- Stock/CrossMix: SD:/Apps/CamView/{camview,launch.sh,config.json,nvr.conf}

## Nut bam (mac dinh)
Man hinh xem:
- L1/R1 hoac D-pad trai/phai : kenh truoc/sau (che do luoi: trang truoc/sau)
- X hoac D-pad len/xuong     : 1 kenh <-> luoi 2x2
- Y : fit <-> fill | A : ket noi lai
- SELECT : doi nhanh ho so (Nha <-> Ngoai mang)
- START  : mo man hinh Cai dat
- B / Menu : thoat
Man hinh Cai dat: len/xuong chon dong, trai/phai doi gia tri, A sua, START luu va ket noi, B huy.
Ban phim ao: D-pad di chuyen, A chon phim, B xoa, X shift, Y dau cach, START xong, Menu huy.

## Xem tu ngoai mang
Chuyen tiep cong (port forward) tren router: cong ngoai (vd 8554) -> IP_dau_ghi:554.
Dien dia chi/DDNS + cong ngoai vao Ho so 2. Nen tao tai khoan chi co quyen XEM, mat khau manh, dung cong la.
Neu router bao IP WAN khac IP tren whatismyip thi nha mang dung CGNAT (khong port forward duoc).

## Meo
- Dung luong phu (subtype=1). Luong chinh 4MP/H.265 se giat vi Brick giai ma bang CPU.
- Dau ghi khac hang: sua url_template trong nvr.conf.
