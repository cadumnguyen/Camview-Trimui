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

typedef struct {
    char host[128], port[8], user[64], pass[64], tmpl[512];
    int channels, subtype, fill, grid;
    int btn_quit, btn_reconnect, btn_mode, btn_fill, btn_prev, btn_next;
} Config;
static Config cfg = {"192.168.1.108", "554", "admin", "", 
    "rtsp://%u:%p@%h:%P/cam/realmonitor?channel=%c&subtype=%s",
    4, 1, 0, 0, 0, 1, 2, 3, 4, 5};

static void load_config(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { fprintf(stderr, "Khong mo duoc %s\n", file); return; }
    char line[700];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0; char *k = line, *v = eq + 1;
#define S(name, dst) if (!strcmp(k, name)) snprintf(dst, sizeof dst, "%s", v)
#define I(name, dst) if (!strcmp(k, name)) dst = atoi(v)
        S("host", cfg.host); else S("port", cfg.port); else S("user", cfg.user);
        else S("pass", cfg.pass); else S("url_template", cfg.tmpl);
        else I("channels", cfg.channels); else I("subtype", cfg.subtype);
        else I("fill", cfg.fill); else I("grid", cfg.grid);
        else I("btn_quit", cfg.btn_quit); else I("btn_reconnect", cfg.btn_reconnect);
        else I("btn_mode", cfg.btn_mode); else I("btn_fill", cfg.btn_fill);
        else I("btn_prev", cfg.btn_prev); else I("btn_next", cfg.btn_next);
    }
    fclose(f);
    if (cfg.channels < 1) cfg.channels = 1;
    if (cfg.channels > 64) cfg.channels = 64;
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
    pct(cfg.user, eu); pct(cfg.pass, ep);
    size_t o = 0; out[0] = 0;
    for (const char *t = cfg.tmpl; *t && o < n - 1; ) {
        const char *r = NULL;
        if (*t == '%' && t[1]) {
            switch (t[1]) {
                case 'u': r = eu; break;
                case 'p': r = ep; break;
                case 'h': r = cfg.host; break;
                case 'P': r = cfg.port; break;
                case 'c': snprintf(num, sizeof num, "%d", ch); r = num; break;
                case 's': snprintf(num, sizeof num, "%d", cfg.subtype); r = num; break;
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
    fprintf(stderr, "[ch%d] connect %s:%s subtype=%d\n", ch, cfg.host, cfg.port, cfg.subtype);
    s->th = SDL_CreateThread(cam_thread, "cam", s);
}
static void slot_stop(Slot *s) {
    if (s->th) { s->stop = 1; SDL_WaitThread(s->th, NULL); s->th = NULL; }
    s->connected = 0;
}

// ---- Điều khiển --------------------------------------------------------
static int mode = 0, cur = 1;   // mode 0 = 1 kênh, 1 = lưới 2x2
static void apply(void) {
    for (int i = 0; i < 4; i++) slot_stop(&slots[i]);
    if (mode == 0) slot_start(&slots[0], cur, 1280);
    else {
        int base = ((cur - 1) / 4) * 4;
        for (int i = 0; i < 4; i++) if (base + i + 1 <= cfg.channels) slot_start(&slots[i], base + i + 1, 640);
    }
}
static void step(int dir) {
    if (mode == 0) { cur += dir; if (cur > cfg.channels) cur = 1; if (cur < 1) cur = cfg.channels; }
    else {
        cur += 4 * dir;
        if (cur > cfg.channels) cur = 1;
        if (cur < 1) cur = ((cfg.channels - 1) / 4) * 4 + 1;
    }
    apply();
}

// ---- Vẽ chữ 5x7 tối giản (số, C, H) --------------------------------------
static const unsigned char FONT[12][7] = {
 {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
 {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
 {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
 {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
 {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
 {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11}};
static void draw_text(SDL_Renderer *r, int x, int y, int sc, const char *t) {
    int n = (int)strlen(t);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
    SDL_Rect bg = {x - sc * 2, y - sc * 2, n * 6 * sc + sc * 3, 7 * sc + sc * 4};
    SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (int i = 0; i < n; i++) {
        int g = -1; char c = t[i];
        if (c >= '0' && c <= '9') g = c - '0'; else if (c == 'C') g = 10; else if (c == 'H') g = 11;
        if (g < 0) continue;
        for (int row = 0; row < 7; row++) for (int col = 0; col < 5; col++)
            if (FONT[g][row] & (0x10 >> col)) { SDL_Rect p = {x + (i * 6 + col) * sc, y + row * sc, sc, sc}; SDL_RenderFillRect(r, &p); }
    }
}

static SDL_Rect fit(SDL_Rect t, int w, int h, int fill) {
    if (fill || w <= 0 || h <= 0) return t;
    float sc = fminf((float)t.w / w, (float)t.h / h);
    SDL_Rect d; d.w = (int)(w * sc); d.h = (int)(h * sc); d.x = t.x + (t.w - d.w) / 2; d.y = t.y + (t.h - d.h) / 2;
    return d;
}

int main(int argc, char **argv) {
    setvbuf(stderr, NULL, _IONBF, 0);
    load_config(argc > 1 ? argv[1] : "nvr.conf");
    mode = cfg.grid ? 1 : 0;
    fprintf(stderr, "CamView2 start: %s:%s channels=%d subtype=%d\n", cfg.host, cfg.port, cfg.channels, cfg.subtype);
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("CamView", 0, 0, 1024, 768, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
    if (!win) win = SDL_CreateWindow("CamView", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, SDL_WINDOW_SHOWN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }
    int W = 1024, H = 768; SDL_GetRendererOutputSize(ren, &W, &H);

    for (int i = 0; i < 4; i++) slots[i].mtx = SDL_CreateMutex();
    apply();

    int running = 1, axis_state = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_LEFT: step(-1); break;
                    case SDLK_RIGHT: step(1); break;
                    case SDLK_UP: case SDLK_DOWN: mode = !mode; apply(); break;
                    case SDLK_SPACE: apply(); break;
                    case SDLK_f: cfg.fill = !cfg.fill; break;
                }
            } else if (e.type == SDL_JOYBUTTONDOWN) {
                int b = e.jbutton.button;
                fprintf(stderr, "joy button %d\n", b);
                if (b == cfg.btn_quit || b == 6 || b == 7 || b == 8) running = 0;
                else if (b == cfg.btn_reconnect) apply();
                else if (b == cfg.btn_mode) { mode = !mode; apply(); }
                else if (b == cfg.btn_fill) cfg.fill = !cfg.fill;
                else if (b == cfg.btn_prev) step(-1);
                else if (b == cfg.btn_next) step(1);
            } else if (e.type == SDL_JOYHATMOTION) {
                if (e.jhat.value & SDL_HAT_LEFT) step(-1);
                else if (e.jhat.value & SDL_HAT_RIGHT) step(1);
                else if (e.jhat.value & (SDL_HAT_UP | SDL_HAT_DOWN)) { mode = !mode; apply(); }
            } else if (e.type == SDL_JOYAXISMOTION && e.jaxis.axis == 0) {
                int st = e.jaxis.value > 16000 ? 1 : (e.jaxis.value < -16000 ? -1 : 0);
                if (st != axis_state) { axis_state = st; if (st) step(st); }
            }
        }

        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        int n = mode ? 4 : 1;
        for (int i = 0; i < n; i++) {
            Slot *s = &slots[i];
            if (!s->th) continue;
            SDL_Rect tile = mode ? (SDL_Rect){ (i % 2) * W / 2 + 1, (i / 2) * H / 2 + 1, W / 2 - 2, H / 2 - 2 } : (SDL_Rect){0, 0, W, H};

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
            if (mode) { SDL_SetRenderDrawColor(ren, 60, 60, 60, 255); SDL_RenderDrawRect(ren, &tile); }

            char label[16]; snprintf(label, sizeof label, "CH%d", s->channel);
            draw_text(ren, tile.x + 34, tile.y + 12, mode ? 3 : 4, label);
            SDL_Rect dot = {tile.x + 10, tile.y + 12, 16, 16};
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            if (s->connected) SDL_SetRenderDrawColor(ren, 0, 220, 0, 255); else SDL_SetRenderDrawColor(ren, 230, 0, 0, 255);
            SDL_RenderFillRect(ren, &dot);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    for (int i = 0; i < 4; i++) slot_stop(&slots[i]);
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
