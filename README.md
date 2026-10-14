# CamView 2 - Xem dau ghi KBVision/Dahua (RTSP) tren Trimui Brick / Brick Pro

## Build (GitHub Actions)
Upload toan bo thu muc nay len repo GitHub (kem .github/workflows/build.yml va build_ffmpeg.sh),
tab Actions -> build-tg5040 (lan dau mat ~5-10 phut vi phai build FFmpeg) -> tai artifact CamView-tg5040.

## Cai dat
- NextUI/MinUI : SD:/Tools/tg5040/CamView.pak/{camview,launch.sh,nvr.conf}
- Stock/CrossMix: SD:/Apps/CamView/{camview,launch.sh,config.json,nvr.conf}
Sua nvr.conf (host, user, pass, channels).

## Nut bam (mac dinh)
- L1 / R1 hoac D-pad trai/phai : kenh truoc / sau (o che do luoi: trang truoc / sau)
- X hoac D-pad len/xuong       : doi giua 1 kenh <-> luoi 2x2
- Y : fit <-> fill | A : ket noi lai | B / Select / Start / Menu : thoat
- Cham xanh = dang co hinh, cham do = chua ket noi
- Nut nao khong dung: mo camview.log, xem dong "joy button N" roi sua btn_* trong nvr.conf

## Meo
- Luon dung subtype=1 (luong phu). Luong chinh 4MP/H.265 se giat vi Brick giai ma bang CPU.
- Mat khau co ky tu dac biet (@ : /) da duoc ma hoa URL tu dong. Neu van 401, thu doi sang mat khau don gian.
- Dau ghi khac hang: sua url_template trong nvr.conf.
