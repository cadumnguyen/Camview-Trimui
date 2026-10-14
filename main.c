// CamView 2 - NVR RTSP viewer for Trimui Brick / Brick Pro (SDL2 + FFmpeg)
#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font8x8_basic.h"

typedef struct { char name[16], host[128], port[8], user[64], pass[64]; int channels, subtype; } Profile;
typedef struct {
    Profile p[2]; int active;
    char tmpl[512];
    int fill, grid;
    int btn_quit, btn_ok, btn_mode, btn_fill, btn_prev, btn_next, btn_profile, btn_settings, btn_menu;
} Config;
static Config cfg = {
    .p = { {"NHA", "192.168.1.108", "554", "admin", "", 8, 1},
           {"NGOAI MANG", "ten-mien.ddns.net", "8554", "admin", "", 8, 1} },
    .active = 0,
    .tmpl = "rtsp://%u:%p@%h:%P/cam/realmonitor?channel=%c&subtype=%s",
    .fill = 0, .grid = 0,
    .btn_quit = 0, .btn_ok = 1, .btn_mode = 2, .btn_fill = 3, .btn_prev = 4, .btn_next = 5,
    .btn_profile = 6, .btn_settings = 7, .btn_menu = 8 };
static char cfg_path[512] = "nvr.conf";
#define P() (&cfg.p[cfg.active])

static void clamp_profile(Profile *p) {
    if (p->channels < 1) p->channels = 1;
    if (p->channels > 64) p->channels = 64;
    p->subtype = p->subtype ? 1 : 0;
}
static void load_config(const char *file) {
    snprintf(cfg_path, sizeof cfg_path, "%s", file);
    FILE *f = fopen(file, "r");
    if (!f) { fprintf(stderr, "Khong mo duoc %s (dung gia tri mac dinh)\n", file); return; }
    char line[700];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0; char *k = line, *v = eq + 1;
        int pi = -1;
        if (k[0] == 'p' && (k[1] == '0' || k[1] == '1') && k[2] == '_') { pi = k[1] - '0'; k += 3; }
        else if (!strcmp(k, "host") || !strcmp(k, "port") || !strcmp(k, "user") || !strcmp(k, "pass") ||
                 !strcmp(k, "channels") || !strcmp(k, "subtype")) pi = 0;   // tương thích file cũ
        if (pi >= 0) {
            Profile *p = &cfg.p[pi];
#define S(name, dst) if (!strcmp(k, name)) snprintf(dst, sizeof dst, "%s", v)
            S("name", p->name); else S("host", p->host); else S("port", p->port);
            else S("user", p->user); else S("pass", p->pass);
            else if (!strcmp(k, "channels")) p->channels = atoi(v);
            else if (!strcmp(k, "subtype")) p->subtype = atoi(v);
            continue;
        }
#define I(name, dst) if (!strcmp(k, name)) dst = atoi(v)
        if (!strcmp(k, "url_template")) snprintf(cfg.tmpl, sizeof cfg.tmpl, "%s", v);
        else I("active", cfg.active); else I("fill", cfg.fill); else I("grid", cfg.grid);
        else I("btn_quit", cfg.btn_quit); else I("btn_ok", cfg.btn_ok); else I("btn_reconnect", cfg.btn_ok);
        else I("btn_mode", cfg.btn_mode); else I("btn_fill", cfg.btn_fill);
        else I("btn_prev", cfg.btn_prev); else I("btn_next", cfg.btn_next);
        else I("btn_profile", cfg.btn_profile); else I("btn_settings", cfg.btn_settings); else I("btn_menu", cfg.btn_menu);
    }
    fclose(f);
    cfg.active = cfg.active ? 1 : 0;
    clamp_profile(&cfg.p[0]); clamp_profile(&cfg.p[1]);
}
static void save_config(void) {
    FILE *f = fopen(cfg_path, "w");
    if (!f) { fprintf(stderr, "Khong ghi duoc %s\n", cfg_path); return; }
    fprintf(f, "# CamView - file nay duoc app tu ghi khi bam Luu trong man hinh Cai dat\n");
    fprintf(f, "active=%d\ngrid=%d\nfill=%d\nurl_template=%s\n", cfg.active, cfg.grid, cfg.fill, cfg.tmpl);
    for (int i = 0; i < 2; i++) {
        Profile *p = &cfg.p[i];
        fprintf(f, "p%d_name=%s\np%d_host=%s\np%d_port=%s\np%d_user=%s\np%d_pass=%s\np%d_channels=%d\np%d_subtype=%d\n",
                i, p->name, i, p->host, i, p->port, i, p->user, i, p->pass, i, p->channels, i, p->subtype);
    }
    fprintf(f, "btn_quit=%d\nbtn_ok=%d\nbtn_mode=%d\nbtn_fill=%d\nbtn_prev=%d\nbtn_next=%d\nbtn_profile=%d\nbtn_settings=%d\nbtn_menu=%d\n",
            cfg.btn_quit, cfg.btn_ok, cfg.btn_mode, cfg.btn_fill, cfg.btn_prev, cfg.btn_next, cfg.btn_profile, cfg.btn_settings, cfg.btn_menu);
    fclose(f);
}

// ---- URL -------------------------------------------------------------
static void pct(const char *in, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o < 190; in++) {
        unsigned char c = *in;
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '-' || c == '.' || c == '_' || c == '~') out[o++] = c;
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    out[o] = 0;
}
static void build_url(char *out, size_t n, int ch) {
    char eu[200], ep[200], num[16];
    pct(P()->user, eu); pct(P()->pass, ep);
    size_t o = 0; out[0] = 0;
    for (const char *t = cfg.tmpl; *t && o < n - 1; ) {
        const char *r = NULL;
        if (*t == '%' && t[1]) {
            switch (t[1]) {
                case 'u': r = eu; break;
                case 'p': r = ep; break;
                case 'h': r = P()->host; break;
                case 'P': r = P()->port; break;
                case 'c': snprintf(num, sizeof num, "%d", ch); r = num; break;
                case 's': snprintf(num, sizeof num, "%d", P()->subtype); r = num; break;
                case '%': r = "%"; break;
            }
        }
        if (r) { size_t l = strlen(r); if (o + l < n - 1) { memcpy(out + o, r, l); o += l; } t += 2; }
        else out[o++] = *t++;
        out[o] = 0;
    }
}

// ---- Slot: mỗi slot = 1 luồng RTSP + 1 thread giải mã --------------
typedef struct {
    SDL_Thread *th; SDL_mutex *mtx;
    volatile int stop, connected, newframe;
    int channel, maxw;
    char url[1024];
    uint8_t *buf; int w, h;
    SDL_Texture *tex; int tw, thh;
} Slot;
static Slot slots[4];

static int interrupt_cb(void *p) { return ((Slot *)p)->stop; }

static int cam_thread(void *arg) {
    Slot *s = arg;
    while (!s->stop) {
        s->connected = 0;
        AVFormatContext *fc = avformat_alloc_context();
        fc->interrupt_callback.callback = interrupt_cb;
        fc->interrupt_callback.opaque = s;
        AVDictionary *o = NULL;
        av_dict_set(&o, "rtsp_transport", "tcp", 0);
        av_dict_set(&o, "timeout", "5000000", 0);
        av_dict_set(&o, "allowed_media_types", "video", 0);
        av_dict_set(&o, "fflags", "nobuffer", 0);
        av_dict_set(&o, "max_delay", "500000", 0);
        av_dict_set(&o, "analyzeduration", "1000000", 0);
        av_dict_set(&o, "probesize", "500000", 0);
        int r = avformat_open_input(&fc, s->url, NULL, &o);
        av_dict_free(&o);
        if (r < 0) {
            char e[128]; av_strerror(r, e, sizeof e);
            fprintf(stderr, "[ch%d] open failed: %s\n", s->channel, e);
            for (int i = 0; i < 20 && !s->stop; i++) SDL_Delay(100);
            continue;
        }
        if (avformat_find_stream_info(fc, NULL) < 0) { avformat_close_input(&fc); continue; }
        int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (vi < 0) { fprintf(stderr, "[ch%d] no video stream\n", s->channel); avformat_close_input(&fc); SDL_Delay(1500); continue; }
        AVCodecParameters *par = fc->streams[vi]->codecpar;
        const AVCodec *dec = avcodec_find_decoder(par->codec_id);
        if (!dec) { fprintf(stderr, "[ch%d] no decoder for codec %d\n", s->channel, par->codec_id); avformat_close_input(&fc); SDL_Delay(3000); continue; }
        AVCodecContext *cc = avcodec_alloc_context3(dec);
        avcodec_parameters_to_context(cc, par);
        cc->thread_count = 2;
        cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
        cc->flags2 |= AV_CODEC_FLAG2_FAST;
        if (avcodec_open2(cc, dec, NULL) < 0) { avcodec_free_context(&cc); avformat_close_input(&fc); SDL_Delay(3000); continue; }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        struct SwsContext *sws = NULL;
        int first = 1;
        while (!s->stop && av_read_frame(fc, pkt) >= 0) {
            if (pkt->stream_index == vi && avcodec_send_packet(cc, pkt) >= 0) {
                while (avcodec_receive_frame(cc, fr) >= 0) {
                    int dw = fr->width; if (dw > s->maxw) dw = s->maxw;
                    int dh = fr->height * dw / fr->width;
                    dw &= ~1; dh &= ~1;
                    if (dw < 2 || dh < 2) continue;
                    sws = sws_getCachedContext(sws, fr->width, fr->height, fr->format,
                                               dw, dh, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
                    if (!sws) continue;
                    SDL_LockMutex(s->mtx);
                    if (s->w != dw || s->h != dh) { s->buf = realloc(s->buf, (size_t)dw * dh * 4); s->w = dw; s->h = dh; }
                    uint8_t *dst[4] = { s->buf, NULL, NULL, NULL };
                    int ls[4] = { dw * 4, 0, 0, 0 };
                    sws_scale(sws, (const uint8_t *const *)fr->data, fr->linesize, 0, fr->height, dst, ls);
                    s->newframe = 1; s->connected = 1;
                    SDL_UnlockMutex(s->mtx);
                    if (first) { fprintf(stderr, "[ch%d] streaming %dx%d -> %dx%d\n", s->channel, fr->width, fr->height, dw, dh); first = 0; }
                }
            }
            av_packet_unref(pkt);
        }
        s->connected = 0;
        sws_freeContext(sws);
        av_frame_free(&fr); av_packet_free(&pkt);
        avcodec_free_context(&cc); avformat_close_input(&fc);
        if (!s->stop) { fprintf(stderr, "[ch%d] stream lost, retry\n", s->channel); for (int i = 0; i < 15 && !s->stop; i++) SDL_Delay(100); }
    }
    return 0;
}

static void slot_start(Slot *s, int ch, int maxw) {
    s->channel = ch; s->maxw = maxw; s->stop = 0; s->connected = 0; s->newframe = 0;
    SDL_LockMutex(s->mtx); s->w = s->h = 0; SDL_UnlockMutex(s->mtx);
    build_url(s->url, sizeof s->url, ch);
    fprintf(stderr, "[ch%d] connect %s:%s subtype=%d\n", ch, P()->host, P()->port, P()->subtype);
    s->th = SDL_CreateThread(cam_thread, "cam", s);
}
static void slot_stop(Slot *s) {
    if (s->th) { s->stop = 1; SDL_WaitThread(s->th, NULL); s->th = NULL; }
    s->connected = 0;
}

// ===================== Giao diện =====================
enum { ST_VIEW, ST_SETTINGS, ST_KEYBOARD };
enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_X, A_Y, A_L, A_R, A_START, A_SELECT, A_MENU };
static int state = ST_VIEW, mode = 0, cur = 1, running = 1;
static char toast[96]; static Uint32 toast_until = 0;
static void set_toast(const char *s, int ms) { snprintf(toast, sizeof toast, "%s", s); toast_until = SDL_GetTicks() + ms; }

static void apply(void) {
    for (int i = 0; i < 4; i++) slot_stop(&slots[i]);
    int n = P()->channels;
    if (cur > n) cur = 1;
    if (cur < 1) cur = 1;
    if (mode == 0) slot_start(&slots[0], cur, 1280);
    else {
        int base = ((cur - 1) / 4) * 4;
        for (int i = 0; i < 4; i++) if (base + i + 1 <= n) slot_start(&slots[i], base + i + 1, 640);
    }
}
static void step(int dir) {
    int n = P()->channels;
    if (mode == 0) { cur += dir; if (cur > n) cur = 1; if (cur < 1) cur = n; }
    else { cur += 4 * dir; if (cur > n) cur = 1; if (cur < 1) cur = ((n - 1) / 4) * 4 + 1; }
    apply();
}

// ---- vẽ chữ bằng font 8x8 ----
static SDL_Renderer *ren; static int W = 1024, H = 768;
static void txt(int x, int y, int sc, const char *t, Uint8 R, Uint8 G, Uint8 B) {
    static SDL_Rect rects[6000]; int n = 0;
    for (int i = 0; t[i] && n < 5900; i++) {
        unsigned char ch = (unsigned char)t[i] & 127;
        for (int row = 0; row < 8; row++) {
            unsigned char bits = font8x8_basic[ch][row];
            for (int col = 0; col < 8; col++) if ((bits >> col) & 1 && n < 6000)
                rects[n++] = (SDL_Rect){ x + (i * 8 + col) * sc, y + row * sc, sc, sc };
        }
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, R, G, B, 255);
    SDL_RenderFillRects(ren, rects, n);
}
static int tw(const char *t, int sc) { return (int)strlen(t) * 8 * sc; }
static void rect(SDL_Rect r, Uint8 R, Uint8 G, Uint8 B, Uint8 A) {
    SDL_SetRenderDrawBlendMode(ren, A < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, R, G, B, A); SDL_RenderFillRect(ren, &r);
}
static void txt_bg(int x, int y, int sc, const char *t) {
    rect((SDL_Rect){ x - 6, y - 6, tw(t, sc) + 12, 8 * sc + 12 }, 0, 0, 0, 170);
    txt(x, y, sc, t, 255, 255, 255);
}
static SDL_Rect fit(SDL_Rect t, int w, int h, int fill) {
    if (fill || w <= 0 || h <= 0) return t;
    float sc = fminf((float)t.w / w, (float)t.h / h);
    SDL_Rect d; d.w = (int)(w * sc); d.h = (int)(h * sc); d.x = t.x + (t.w - d.w) / 2; d.y = t.y + (t.h - d.h) / 2;
    return d;
}

// ---- Màn hình Cài đặt ----
enum { F_PROFILE, F_NAME, F_HOST, F_PORT, F_USER, F_PASS, F_CHANNELS, F_STREAM, F_SAVE, F_CANCEL, NF };
static Profile wp[2]; static int sidx = 0, sel = 0;

// ---- Bàn phím ảo ----
static const char *KROWS[6] = { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm.-_", ":/?=&%+~#!", "$^*()[]{}," };
static const char *KACT[5] = { "SHIFT", "SPACE", "XOA", "OK", "HUY" };
static char kbuf[128], *ktarget = NULL; static int ksize = 0, kr = 1, kc = 0, kshift = 0; static const char *ktitle = "";

static void open_settings(void) {
    for (int i = 0; i < 4; i++) slot_stop(&slots[i]);
    wp[0] = cfg.p[0]; wp[1] = cfg.p[1]; sidx = cfg.active; sel = F_HOST; state = ST_SETTINGS;
}
static void close_settings(int save) {
    if (save) {
        cfg.p[0] = wp[0]; cfg.p[1] = wp[1]; cfg.active = sidx;
        clamp_profile(&cfg.p[0]); clamp_profile(&cfg.p[1]);
        save_config(); cur = 1;
        set_toast("DA LUU", 1500);
    }
    state = ST_VIEW; apply();
}
static void open_keyboard(const char *title, char *target, int size) {
    ktitle = title; ktarget = target; ksize = size;
    snprintf(kbuf, sizeof kbuf, "%s", target); kr = 1; kc = 0; kshift = 0; state = ST_KEYBOARD;
}
static void kb_append(char c) { size_t l = strlen(kbuf); if ((int)l < ksize - 1 && l < sizeof kbuf - 1) { kbuf[l] = c; kbuf[l + 1] = 0; } }
static void kb_back(void) { size_t l = strlen(kbuf); if (l) kbuf[l - 1] = 0; }
static void kb_done(int ok) { if (ok) snprintf(ktarget, ksize, "%s", kbuf); state = ST_SETTINGS; }
static void kb_press(void) {
    if (kr < 6) { char c = KROWS[kr][kc]; if (kshift && c >= 'a' && c <= 'z') c -= 32; kb_append(c); }
    else switch (kc) { case 0: kshift = !kshift; break; case 1: kb_append(' '); break; case 2: kb_back(); break; case 3: kb_done(1); break; case 4: kb_done(0); break; }
}

static void settings_change(int dir) {   // trái/phải
    Profile *p = &wp[sidx];
    if (sel == F_PROFILE) sidx = !sidx;
    else if (sel == F_CHANNELS) { p->channels += dir; if (p->channels < 1) p->channels = 64; if (p->channels > 64) p->channels = 1; }
    else if (sel == F_STREAM) p->subtype = !p->subtype;
}
static void settings_ok(void) {
    Profile *p = &wp[sidx];
    switch (sel) {
        case F_PROFILE: case F_CHANNELS: case F_STREAM: settings_change(1); break;
        case F_NAME: open_keyboard("TEN HO SO", p->name, sizeof p->name); break;
        case F_HOST: open_keyboard("DIA CHI (IP HOAC TEN MIEN)", p->host, sizeof p->host); break;
        case F_PORT: open_keyboard("CONG (PORT)", p->port, sizeof p->port); break;
        case F_USER: open_keyboard("TAI KHOAN", p->user, sizeof p->user); break;
        case F_PASS: open_keyboard("MAT KHAU", p->pass, sizeof p->pass); break;
        case F_SAVE: close_settings(1); break;
        case F_CANCEL: close_settings(0); break;
    }
}

static void handle(int a) {
    if (a == A_NONE) return;
    if (state == ST_VIEW) {
        switch (a) {
            case A_LEFT: case A_L: step(-1); break;
            case A_RIGHT: case A_R: step(1); break;
            case A_UP: case A_DOWN: case A_X: mode = !mode; apply(); break;
            case A_Y: cfg.fill = !cfg.fill; break;
            case A_OK: apply(); break;
            case A_START: open_settings(); break;
            case A_SELECT: {
                cfg.active = !cfg.active; cur = 1; apply(); save_config();
                char m[96]; snprintf(m, sizeof m, "HO SO: %s", P()->name); set_toast(m, 2000); break; }
            case A_BACK: case A_MENU: running = 0; break;
        }
    } else if (state == ST_SETTINGS) {
        switch (a) {
            case A_UP: sel = (sel + NF - 1) % NF; break;
            case A_DOWN: sel = (sel + 1) % NF; break;
            case A_LEFT: settings_change(-1); break;
            case A_RIGHT: settings_change(1); break;
            case A_OK: settings_ok(); break;
            case A_START: close_settings(1); break;
            case A_BACK: case A_MENU: close_settings(0); break;
        }
    } else {
        switch (a) {
            case A_UP: { int old = kr; kr = (kr + 6) % 7; if (kr == 6) kc /= 2; else if (old == 6) kc *= 2; break; }
            case A_DOWN: { int old = kr; kr = (kr + 1) % 7; if (kr == 6) kc /= 2; else if (old == 6) kc *= 2; break; }
            case A_LEFT: kc = (kc + (kr == 6 ? 5 : 10) - 1) % (kr == 6 ? 5 : 10); break;
            case A_RIGHT: kc = (kc + 1) % (kr == 6 ? 5 : 10); break;
            case A_OK: kb_press(); break;
            case A_BACK: kb_back(); break;
            case A_X: kshift = !kshift; break;
            case A_Y: kb_append(' '); break;
            case A_START: kb_done(1); break;
            case A_MENU: kb_done(0); break;
        }
    }
}

// ---- vẽ ----
static void draw_view(void) {
    int n = mode ? 4 : 1;
    for (int i = 0; i < n; i++) {
        Slot *s = &slots[i];
        if (!s->th) continue;
        SDL_Rect tile = mode ? (SDL_Rect){ (i % 2) * W / 2 + 1, (i / 2) * H / 2 + 1, W / 2 - 2, H / 2 - 2 } : (SDL_Rect){ 0, 0, W, H };
        SDL_LockMutex(s->mtx);
        if (s->newframe && s->w > 0) {
            if (!s->tex || s->tw != s->w || s->thh != s->h) {
                if (s->tex) SDL_DestroyTexture(s->tex);
                s->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, s->w, s->h);
                if (!s->tex) fprintf(stderr, "CreateTexture: %s\n", SDL_GetError());
                s->tw = s->w; s->thh = s->h;
            }
            if (s->tex) SDL_UpdateTexture(s->tex, NULL, s->buf, s->w * 4);
            s->newframe = 0;
        }
        int has = s->w > 0 && s->tex;
        SDL_UnlockMutex(s->mtx);
        if (has) { SDL_Rect d = fit(tile, s->tw, s->thh, cfg.fill); SDL_RenderCopy(ren, s->tex, NULL, &d); }
        else if (!s->connected) {
            const char *m = "DANG KET NOI...";
            int sc = mode ? 2 : 3;
            txt(tile.x + (tile.w - tw(m, sc)) / 2, tile.y + tile.h / 2 - 4 * sc, sc, m, 150, 150, 150);
        }
        if (mode) { SDL_SetRenderDrawColor(ren, 60, 60, 60, 255); SDL_RenderDrawRect(ren, &tile); }
        char label[16]; snprintf(label, sizeof label, "CH%d", s->channel);
        txt_bg(tile.x + 38, tile.y + 14, mode ? 3 : 4, label);
        SDL_Rect dot = { tile.x + 10, tile.y + 14, 20, 20 };
        if (s->connected) rect(dot, 0, 220, 0, 255); else rect(dot, 230, 0, 0, 255);
    }
}
static void draw_settings(void) {
    rect((SDL_Rect){ 0, 0, W, H }, 18, 22, 30, 255);
    txt(40, 28, 4, "CAI DAT", 255, 255, 255);
    Profile *p = &wp[sidx];
    char val[160], pw[80];
    for (int i = 0; i < NF; i++) {
        const char *lab = ""; val[0] = 0;
        switch (i) {
            case F_PROFILE: lab = "Ho so"; snprintf(val, sizeof val, "< %d: %s >", sidx + 1, p->name); break;
            case F_NAME: lab = "Ten ho so"; snprintf(val, sizeof val, "%s", p->name); break;
            case F_HOST: lab = "Dia chi"; snprintf(val, sizeof val, "%s", p->host); break;
            case F_PORT: lab = "Cong"; snprintf(val, sizeof val, "%s", p->port); break;
            case F_USER: lab = "Tai khoan"; snprintf(val, sizeof val, "%s", p->user); break;
            case F_PASS: lab = "Mat khau"; { size_t l = strlen(p->pass); if (l > 70) l = 70; memset(pw, '*', l); pw[l] = 0; snprintf(val, sizeof val, "%s", pw); } break;
            case F_CHANNELS: lab = "So kenh"; snprintf(val, sizeof val, "< %d >", p->channels); break;
            case F_STREAM: lab = "Luong"; snprintf(val, sizeof val, "< %s >", p->subtype ? "PHU (nhe)" : "CHINH (nang)"); break;
            case F_SAVE: lab = ">> LUU VA KET NOI"; break;
            case F_CANCEL: lab = ">> HUY"; break;
        }
        SDL_Rect row = { 24, 92 + i * 58, W - 48, 50 };
        if (i == sel) rect(row, 40, 90, 160, 255);
        else rect(row, 28, 34, 46, 255);
        txt(row.x + 16, row.y + 13, 3, lab, 255, 255, 255);
        if (val[0]) txt(row.x + 290, row.y + 13, 3, val, 255, 220, 120);
    }
    txt(40, H - 70, 2, "LEN/XUONG CHON  TRAI/PHAI DOI  A SUA  B HUY  START LUU", 170, 170, 170);
}
static void draw_keyboard(void) {
    rect((SDL_Rect){ 0, 0, W, H }, 18, 22, 30, 255);
    txt(40, 24, 3, ktitle, 255, 255, 255);
    rect((SDL_Rect){ 40, 70, W - 80, 56 }, 40, 46, 60, 255);
    char shown[160]; snprintf(shown, sizeof shown, "%s_", kbuf);
    int sc = 3; int maxc = (W - 100) / (8 * sc);
    const char *sp = strlen(shown) > (size_t)maxc ? shown + strlen(shown) - maxc : shown;
    txt(52, 86, sc, sp, 255, 220, 120);
    int kw = 90, kh = 66, x0 = (W - kw * 10) / 2, y0 = 150;
    for (int r = 0; r < 7; r++) {
        int nc = r == 6 ? 5 : 10, w = r == 6 ? kw * 2 : kw;
        for (int c = 0; c < nc; c++) {
            SDL_Rect k = { x0 + c * w + 3, y0 + r * (kh + 4), w - 6, kh };
            int on = (r == kr && c == kc);
            rect(k, on ? 40 : 38, on ? 120 : 46, on ? 200 : 62, 255);
            char t[8];
            if (r < 6) { char ch = KROWS[r][c]; if (kshift && ch >= 'a' && ch <= 'z') ch -= 32; t[0] = ch; t[1] = 0; }
            else snprintf(t, sizeof t, "%s", (c == 0 && kshift) ? "shift*" : KACT[c]);
            txt(k.x + (k.w - tw(t, 3)) / 2, k.y + (k.h - 24) / 2, 3, t, 255, 255, 255);
        }
    }
    txt(40, H - 40, 2, "A CHON  B XOA  X SHIFT  Y CACH  START XONG  MENU HUY", 170, 170, 170);
}

static int map_button(int b) {
    if (b == cfg.btn_quit) return A_BACK;
    if (b == cfg.btn_ok) return A_OK;
    if (b == cfg.btn_mode) return A_X;
    if (b == cfg.btn_fill) return A_Y;
    if (b == cfg.btn_prev) return A_L;
    if (b == cfg.btn_next) return A_R;
    if (b == cfg.btn_profile) return A_SELECT;
    if (b == cfg.btn_settings) return A_START;
    if (b == cfg.btn_menu) return A_MENU;
    return A_NONE;
}

int main(int argc, char **argv) {
    setvbuf(stderr, NULL, _IONBF, 0);
    load_config(argc > 1 ? argv[1] : "nvr.conf");
    mode = cfg.grid ? 1 : 0;
    fprintf(stderr, "CamView2 start: profile %d (%s) %s:%s channels=%d\n", cfg.active + 1, P()->name, P()->host, P()->port, P()->channels);
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("CamView", 0, 0, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
    if (!win) win = SDL_CreateWindow("CamView", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }
    SDL_GetRendererOutputSize(ren, &W, &H);

    for (int i = 0; i < 4; i++) slots[i].mtx = SDL_CreateMutex();
    apply();
    set_toast("START: CAI DAT   SELECT: DOI HO SO", 4000);

    int ax = 0, ay = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                int a = A_NONE;
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: a = A_MENU; break;
                    case SDLK_LEFT: a = A_LEFT; break; case SDLK_RIGHT: a = A_RIGHT; break;
                    case SDLK_UP: a = A_UP; break; case SDLK_DOWN: a = A_DOWN; break;
                    case SDLK_RETURN: a = A_OK; break; case SDLK_BACKSPACE: a = A_BACK; break;
                    case SDLK_F1: a = A_START; break; case SDLK_F2: a = A_SELECT; break;
                    case SDLK_f: a = A_Y; break; case SDLK_TAB: a = A_X; break;
                }
                handle(a);
            } else if (e.type == SDL_JOYBUTTONDOWN) {
                fprintf(stderr, "joy button %d\n", e.jbutton.button);
                handle(map_button(e.jbutton.button));
            } else if (e.type == SDL_JOYHATMOTION) {
                int v = e.jhat.value;
                handle(v & SDL_HAT_LEFT ? A_LEFT : v & SDL_HAT_RIGHT ? A_RIGHT : v & SDL_HAT_UP ? A_UP : v & SDL_HAT_DOWN ? A_DOWN : A_NONE);
            } else if (e.type == SDL_JOYAXISMOTION && e.jaxis.axis < 2) {
                int st = e.jaxis.value > 16000 ? 1 : (e.jaxis.value < -16000 ? -1 : 0);
                int *cur_st = e.jaxis.axis == 0 ? &ax : &ay;
                if (st != *cur_st) {
                    *cur_st = st;
                    if (st) handle(e.jaxis.axis == 0 ? (st > 0 ? A_RIGHT : A_LEFT) : (st > 0 ? A_DOWN : A_UP));
                }
            }
        }
        if (!running) break;

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        if (state == ST_VIEW) draw_view();
        else if (state == ST_SETTINGS) draw_settings();
        else draw_keyboard();
        if (state == ST_VIEW && SDL_GetTicks() < toast_until) txt_bg((W - tw(toast, 3)) / 2, H - 56, 3, toast);
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    for (int i = 0; i < 4; i++) slot_stop(&slots[i]);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
