/* Host implementations of the PSY-Q calls PsyZ does not provide. */
#include "port/pad_trace.h"
#include "port/backgrounds.h"
#include <inttypes.h>
#include "port/memory_card.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
/* Define before SDK headers redirect open() to PS1 device/path translation. */
static FILE* pePadOpenControlFifo(const char* path)
{
    struct stat info;
    int fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) return NULL;
    if (fstat(fd, &info) != 0 || !S_ISFIFO(info.st_mode) || fcntl(fd, F_SETFL, 0) < 0) {
        close(fd);
        return NULL;
    }
    FILE* stream = fdopen(fd, "r");
    if (!stream) close(fd);
    return stream;
}
#endif
#include <psyz/video.h>
/* SDL overlay header also imports stdbool, which conflicts with retail bool.
 * Match its C99 boolean ABI without importing SDL into game headers. */
extern void Psyz_PauseHostClock_SDL3(_Bool paused);
#include "main/fs.h"
#include "main/display.h"
#include "main/gamemain.h"
#include <stdio.h>
#include <string.h>

#include <psyz.h>
#include <libcd.h>
#include <libgte.h>
#include "port/psyq_compat.h"

/* libcd: the host disc image is always present and always a CD-ROM. */
int CdDiskReady(int mode)
{
    (void)mode;
    return CdlComplete;
}

int CdGetDiskType(void)
{
    return CdlCdromFormat;
}

/* The host CD layer needs no interrupt pump. */

/* libapi heap 3: the host allocator replaces the PlayStation arena. The arena
 * arguments are accepted and ignored. */
void InitHeap3(unsigned long* head, unsigned long size)
{
    (void)head;
    (void)size;
}

void* malloc3(unsigned long size)
{
    return malloc(size);
}

void free3(void* ptr)
{
    free(ptr);
}

/* libgte MatrixNormal: scale each row of the 3x3 to unit length (4096 = 1.0).
 * NOTE: approximates the SDK routine, whose exact fixed-point behaviour has not
 * been verified against hardware or an emulator trace. */
void MatrixNormal(MATRIX* m, MATRIX* n)
{
    for (int r = 0; r < 3; r++) {
        double x = m->m[r][0], y = m->m[r][1], z = m->m[r][2];
        double len = sqrt(x * x + y * y + z * z);
        for (int c = 0; c < 3; c++) {
            double v = len > 0.0 ? (double)m->m[r][c] * 4096.0 / len : 0.0;
            n->m[r][c] = (short)lround(v);
        }
    }
}

/* v1 = (m * v0) >> 12, exact in 64 bits (the SDK splits the vector to stay in
 * 32 bits; results agree unless the SDK's own intermediate overflows). */
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1)
{
    long long in[3] = { v0->vx, v0->vy, v0->vz };
    int out[3];
    for (int i = 0; i < 3; i++) {
        long long sum = 0;
        for (int j = 0; j < 3; j++) sum += (long long)m->m[i][j] * in[j];
        out[i] = (int)(sum >> 12);
    }
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

/* v1 = (transpose(m) * v0) >> 12 */
VECTOR* ApplyTransposeMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1)
{
    long long in[3] = { v0->vx, v0->vy, v0->vz };
    int out[3];
    for (int i = 0; i < 3; i++) {
        long long sum = 0;
        for (int j = 0; j < 3; j++) sum += (long long)m->m[j][i] * in[j];
        out[i] = (int)(sum >> 12);
    }
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

SVECTOR* ApplyMatrixSV(MATRIX* m, SVECTOR* v0, SVECTOR* v1)
{
    long long in[3] = { v0->vx, v0->vy, v0->vz };
    short out[3];
    for (int i = 0; i < 3; i++) {
        long long sum = 0;
        for (int j = 0; j < 3; j++) sum += (long long)m->m[i][j] * in[j];
        out[i] = (short)(sum >> 12);
    }
    v1->vx = out[0]; v1->vy = out[1]; v1->vz = out[2];
    return v1;
}

MATRIX* RotMatrixZXY(SVECTOR* r, MATRIX* m)
{
    int sx = rsin(r->vx), cx = rcos(r->vx), sy = rsin(r->vy), cy = rcos(r->vy), sz = rsin(r->vz), cz = rcos(r->vz);
    int szsx = (sz * sx) >> 12, czsx = (cz * sx) >> 12;

    m->m[0][0] = ((cz * cy) >> 12) - ((szsx * sy) >> 12);
    m->m[0][1] = -((sz * cx) >> 12);
    m->m[0][2] = ((cz * sy) >> 12) + ((szsx * cy) >> 12);
    m->m[1][0] = ((sz * cy) >> 12) + ((czsx * sy) >> 12);
    m->m[1][1] = (cz * cx) >> 12;
    m->m[1][2] = ((sz * sy) >> 12) - ((czsx * cy) >> 12);
    m->m[2][0] = -((cx * sy) >> 12);
    m->m[2][1] = sx;
    m->m[2][2] = (cx * cy) >> 12;
    return m;
}

MATRIX* ScaleMatrixL(MATRIX* m, VECTOR* v)
{
    int scales[3] = { v->vx, v->vy, v->vz };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) m->m[i][j] = (short)(((long long)m->m[i][j] * scales[i]) >> 12);
    return m;
}

void gteMIMefunc(SVECTOR* out, const SVECTOR* delta, int count, int weight)
{
    for (int i = 0; i < count; i++) {
        long long v[3] = { out[i].vx + (((long long)delta[i].vx * weight) >> 12),
                           out[i].vy + (((long long)delta[i].vy * weight) >> 12),
                           out[i].vz + (((long long)delta[i].vz * weight) >> 12) };
        for (int k = 0; k < 3; k++) v[k] = v[k] > 32767 ? 32767 : v[k] < -32768 ? -32768 : v[k];
        out[i].vx = (short)v[0]; out[i].vy = (short)v[1]; out[i].vz = (short)v[2];
    }
}

/* ---- PSY-Q calls PsyZ declares but does not implement ----------------------
 * Controller extras and SPU reverb retain compatibility shims here.
 * Memory-card storage lives in memory_card.c. */

#include <libgpu.h>
#include <libmcrd.h>
#include <libspu.h>
#include <libetc.h>
#include <psyz/gte.h>

/* The emulated controller is a digital pad (ID 4): InfoModeCurID = 1 reports its type; no extended ID. */
int PadInfoMode(int port, int term, int offs)
{
    (void)port; (void)offs;
    return term == 1 ? 4 : 0;
}
int PadSetMainMode(int port, int offs, int lock) { (void)port; (void)offs; (void)lock; return 1; }

long SpuReserveReverbWorkArea(long on_off) { return on_off; }

unsigned long SetSp(unsigned long new_sp) { (void)new_sp; return 0; }
void SetMem(unsigned long n) { (void)n; }

/* Drawing is synchronous on the host, so a draw never needs to be suspended. */
u_long* BreakDraw(void) { return (u_long*)0; }
void ContinueDraw(u_long* inst_ot, u_long* cont_ot) { (void)inst_ot; (void)cont_ot; }

int StoreImage2(RECT* rect, u_long* p) { return StoreImage(rect, p); }

/* DR_STP: GPU "set mask bit" command (E6h); bit 0 sets the mask on draw, bit 1 checks it. */
void SetDrawStp(DR_STP* p, int stp)
{
    setlen(p, 2);
    p->code[0] = 0xE6000000u | (stp ? 1u : 0u);
    p->code[1] = 0;
}

/* GTE rotation matrix times m, result in m (SDK MulRotMatrix). */
MATRIX* MulRotMatrix(MATRIX* m)
{
    short r[3][3];
    u32   c[5];
    for (int i = 0; i < 5; i++) c[i] = Psyz_GteCtrlRead(i);
    r[0][0] = (short)(c[0] & 0xFFFF); r[0][1] = (short)(c[0] >> 16);
    r[0][2] = (short)(c[1] & 0xFFFF); r[1][0] = (short)(c[1] >> 16);
    r[1][1] = (short)(c[2] & 0xFFFF); r[1][2] = (short)(c[2] >> 16);
    r[2][0] = (short)(c[3] & 0xFFFF); r[2][1] = (short)(c[3] >> 16);
    r[2][2] = (short)c[4];
    short out[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            long long sum = 0;
            for (int k = 0; k < 3; k++) sum += (long long)r[i][k] * m->m[k][j];
            out[i][j] = (short)(sum >> 12);
        }
    memcpy(m->m, out, sizeof out);
    return m;
}

/* m2 = m0 * m1 (3x3 only; translation untouched). */
MATRIX* MulMatrix0(MATRIX* m0, MATRIX* m1, MATRIX* m2)
{
    short out[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            long long sum = 0;
            for (int k = 0; k < 3; k++) sum += (long long)m0->m[i][k] * m1->m[k][j];
            out[i][j] = (short)(sum >> 12);
        }
    memcpy(m2->m, out, sizeof out);
    return m2;
}

MATRIX* RotMatrix_gte(SVECTOR* r, MATRIX* m) { return RotMatrix(r, m); }

/* v1 = RT * v0 + TR (SVECTOR in and out); *flag receives the GTE FLAG word. */
void RotTransSV(SVECTOR* v0, SVECTOR* v1, int* flag)
{
    Psyz_GteDataWrite(0, (u32)(u16)v0->vx | ((u32)(u16)v0->vy << 16));
    Psyz_GteDataWrite(1, (u32)(u16)v0->vz);
    Psyz_GteCommand(0x4A480012);
    v1->vx = (short)Psyz_GteDataRead(9);
    v1->vy = (short)Psyz_GteDataRead(10);
    v1->vz = (short)Psyz_GteDataRead(11);
    if (flag) *flag = Psyz_GteReadflg();
}

/* libpress / MDEC bitstream decoding is part of the movie phase. */

void LoadAverageCol(unsigned char* c0, unsigned char* c1, int w0, int w1, unsigned char* out)
{
    int i;
    for (i = 0; i < 3; i++) {
        int v = (c0[i] * w0 + c1[i] * w1) >> 12;
        out[i] = v < 0 ? 0 : v > 255 ? 255 : (unsigned char)v;
    }
}

unsigned char gHostScratchpad[0x400] __attribute__((aligned(0x400)));

unsigned char gHostRam[0x200000] __attribute__((aligned(0x1000)));
unsigned int gHostGpuStatus[2] = { 0x1C000000u, 0 }; /* ready bits set */
void* _freep; /* libapi heap3 search cursor; unused with the host allocator */

/* libgpu non-blocking transfers: the host GPU queue is drained by DrawSync, so the blocking forms
 * followed by an idle report are equivalent. */
int LoadImage2(RECT* rect, u_long* p) { return LoadImage(rect, p); }
int MoveImage2(RECT* rect, int x, int y) { return MoveImage(rect, x, y); }
int IsIdleGPU(int max_count) { (void)max_count; return 0; }

/* Opt-in headless continuation after a finite trace. Only controller samples
 * are supplied; the existing game loop and normal task scheduler do the work. */
static void pePadControlRead(uint64_t index, char* frame, size_t length)
{
    static uint32_t remaining;
    static uint16_t buttons;
    static FILE* commands;
    if (!commands) {
        const char* fifo = getenv("PE2_PAD_CONTROL_FIFO");
        commands = stdin;
        if (fifo) {
#ifndef _WIN32
            if (!(commands = pePadOpenControlFifo(fifo))) {
                fprintf(stderr, "[pad] control FIFO unavailable: %s\n", fifo);
                exit(1);
            }
            /* Retain the write end so short-lived external writers cannot
             * signal EOF between batches. Only the controller supplies bytes. */
#else
            fprintf(stderr, "[pad] control FIFO is unsupported on this host\n");
            exit(1);
#endif
        }
    }
    if (!remaining) {
        char line[128];
        Psyz_PauseHostClock_SDL3(true);
        fprintf(stderr, "[pad] control waiting at frame %llu\n", (unsigned long long)index);
        fflush(stderr);
        for (;;) {
            errno = 0;
            if (fgets(line, sizeof line, commands)) break;
            if (ferror(commands) && errno == EINTR) {
                clearerr(commands);
                continue;
            }
            fprintf(stderr, "[pad] control %s\n", ferror(commands) ? "read failed" : "closed");
            exit(ferror(commands) ? 1 : 0);
        }
        if (!strchr(line, '\n') && !feof(commands)) {
            fprintf(stderr, "[pad] control command too long\n");
            exit(1);
        }
        int result = pePadControlParse(line, &remaining, &buttons);
        if (result <= 0) {
            fprintf(stderr, "[pad] control %s\n", result == 0 ? "complete" : "invalid command");
            exit(result == 0 ? 0 : 1);
        }
        Psyz_PauseHostClock_SDL3(false);
    }
    memset(frame, 0, length);
    frame[1] = 0x41;
    frame[2] = (char)~buttons;
    frame[3] = (char)(~buttons >> 8);
    --remaining;
}

/* Scripted input for automated runs. Each development feature lives in its own function so
 * Psyz_PadsOverride stays readable; the shared pad-poll tick count is file scope. */
static int pePadTickCount;

/* PE2_FAST=1 lifts the frame limiter while an FMV plays, so development runs skip the intro. */
static void pePadFastMovieVsync(int port)
{
    static int fastEnabled = -1, fastNow;
    if (fastEnabled < 0) fastEnabled = getenv("PE2_FAST") != NULL;
    if (fastEnabled && port == 0) {
        int movie = gCdCmdQueue.movieStep >= 1 && gCdCmdQueue.movieStep <= 5;
        if (movie != fastNow) {
            fastNow = movie;
            Psyz_VideoSetVsyncMode(movie ? PSYZ_VSYNC_LIMITLESS : PSYZ_VSYNC_AUTO);
        }
    }
}

/* PE2_SKIPINTRO=1: the game's own "skip the title intro" switch, so development runs reach the title directly. */
static void pePadSkipIntro(int port, int tick)
{
    if (port == 0 && tick < 300 && getenv("PE2_SKIPINTRO")) {
        gGameMainPersistentState.skipTitleIntro = 1;
    }
}

static void pePadTraceTick(int port, int tick)
{
    if (port == 0 && getenv("PE2_TRACE") && (tick % 60) == 0) {
        fprintf(stderr, "[trace] tick=%d movieStep=%d demo=%d\n", tick, gCdCmdQueue.movieStep, gDisplayState.demoScene);
    }
}

/* development: PE2_DUMP_VRAM=<tick>:<file> writes the raw 1024x512 VRAM once (tools/port/export_vram.py) */
static void pePadDumpVram(int tick)
{
    static int dumpTick = -2;
    static char dumpPath[512];
    if (dumpTick == -2) {
        const char* spec = getenv("PE2_DUMP_VRAM");
        dumpTick = spec && sscanf(spec, "%d:%511s", &dumpTick, dumpPath) == 2 ? dumpTick : -1;
    }
    if (dumpTick >= 0 && tick == dumpTick) {
        static u_short vram[1024 * 512];
        RECT all = { 0, 0, 1024, 512 };
        StoreImage(&all, (u_long*)(void*)vram);
        DrawSync(0);
        FILE* f = fopen(dumpPath, "wb");
        if (f) { fwrite(vram, sizeof vram, 1, f); fclose(f); }
    }
}

/* Capture the actual presenter, including widescreen bars, without a desktop window. */
static void pePadDumpFrame(int tick)
{
    static int frameTick = -2;
    static char framePath[512];
    if (frameTick == -2) {
        const char* spec = getenv("PE2_DUMP_FRAME");
        frameTick = spec && sscanf(spec, "%d:%511s", &frameTick, framePath) == 2 ? frameTick : -1;
    }
    if (frameTick < 0 || tick != frameTick) return;
    int w=0,h=0;
    unsigned char* rgb=Psyz_VideoAllocRenderedFrame(&w,&h);
    FILE* file=rgb?fopen(framePath,"wb"):NULL;
    if(file) { fprintf(file,"P6\n%d %d\n255\n",w,h);fwrite(rgb,3,(size_t)w*h,file);fclose(file); }
    if(rgb) {
        DISPENV display;GetDispEnv(&display);
        PeBackgroundFrameInfo info={0};
        int known=peBackgroundGetFrameInfo(display.disp.y,&info);
        char metadata[520];snprintf(metadata,sizeof metadata,"%s.json",framePath);
        FILE* sidecar=fopen(metadata,"w");
        if(sidecar) {
            fprintf(sidecar,"{\"tick\":%d,\"width\":%d,\"height\":%d,\"buffer_y\":%d,\"known_source\":%s,\"key\":\"%016" PRIx64 "\",\"location\":[%d,%d,%d,%d,%d,%d]}\n",
                    tick,w,h,display.disp.y,known?"true":"false",info.key,
                    info.location[0],info.location[1],info.location[2],info.location[3],info.location[4],info.location[5]);
            fclose(sidecar);
        }
    }
    free(rgb);
}

/* development: PE2_WARP=<tick>:stage,area,view,roomState,warp,variant[;<tick>:...] changes stage mid-game */
static void pePadWarp(int tick)
{
    enum { MAX_WARPS = 8 };
    static struct { int tick; unsigned key[6]; } warps[MAX_WARPS];
    static int warpCount = -1;
    if (warpCount < 0) {
        warpCount = 0;
        const char* spec = getenv("PE2_WARP");
        while (spec && *spec && warpCount < MAX_WARPS) {
            int used = 0;
            unsigned* k = warps[warpCount].key;
            if (sscanf(spec, "%d:%u,%u,%u,%u,%u,%u%n", &warps[warpCount].tick, &k[0], &k[1], &k[2], &k[3], &k[4], &k[5],
                       &used) != 7) break;
            warpCount++;
            spec += used;
            if (*spec == ';') spec++;
        }
    }
    for (int i = 0; i < warpCount; i++) {
        if (tick != warps[i].tick) continue;
        extern void peDebugWarp(const GameLocationKey*);
        const unsigned* k = warps[i].key;
        GameLocationKey key = { (u8)k[2], (u8)k[3], (u8)k[1], (u8)k[0], (u8)k[4], (u8)k[5], { 0, 0 } };
        peDebugWarp(&key);
    }
}

/* PE2_PAD_HOLD: hold a button mask over a tick interval (PS1 button bits, active low in the pad frame). */
static void pePadHoldButtons(char* frame, int tick)
{
    static unsigned holdMask;
    static int holdStart, holdEnd, initialized;
    if (!initialized) {
        holdMask = getenv("PE2_PAD_HOLD") ? strtoul(getenv("PE2_PAD_HOLD"), NULL, 16) & 0xffff : 0;
        holdStart = getenv("PE2_PAD_FROM") ? atoi(getenv("PE2_PAD_FROM")) : 0;
        holdEnd = getenv("PE2_PAD_UNTIL") ? atoi(getenv("PE2_PAD_UNTIL")) : 0;
        initialized = 1;
    }
    if (tick >= holdStart && holdEnd > tick) {
        frame[2] &= ~(holdMask & 0xff);
        frame[3] &= ~((holdMask >> 8) & 0xff);
    }
}

/* PE2_AUTOPRESS=<first tick> taps Start, then Cross, in turn, until PE2_AUTOPRESS_UNTIL. */
static void pePadAutopress(char* frame, int tick)
{
    static int enabled = -1, startTick, endTick;
    if (enabled < 0) {
        enabled = getenv("PE2_AUTOPRESS") != NULL;
        startTick = enabled ? atoi(getenv("PE2_AUTOPRESS")) : 0;
        endTick = getenv("PE2_AUTOPRESS_UNTIL") ? atoi(getenv("PE2_AUTOPRESS_UNTIL")) : 0;
    }
    if (!enabled || tick < startTick || (endTick && tick >= endTick)) return;
    /* pePadTickCount has already advanced past this sample, matching the original evaluation order. */
    int phase = ((pePadTickCount - startTick) / 20) % 6;
    if (phase == 0) frame[2] &= ~(1 << 3);
    if (phase == 3) frame[3] &= ~(1 << 6);
}

/* PE2_PAD_RECORD/REPLAY traces; PE2_PAD_CONTROL continues headlessly after a replay ends. */
static void pePadRecordReplay(char* frame, int len)
{
    static FILE *record, *replay;
    static int traceInitialized, controlEnabled, replayFinished;
    static uint64_t padFrame;
    if (!traceInitialized) {
        const char *recordPath = getenv("PE2_PAD_RECORD"), *replayPath = getenv("PE2_PAD_REPLAY");
        if (recordPath && replayPath && strcmp(recordPath, replayPath) == 0) {
            fprintf(stderr, "[pad] recording and replay paths must differ\n"); exit(1);
        }
        if (replayPath && (!(replay = fopen(replayPath, "rb")) || !pePadTraceHeader(replay, 0))) {
            fprintf(stderr, "[pad] cannot read replay: %s\n", replayPath); exit(1);
        }
        if (recordPath && (!(record = fopen(recordPath, "wb")) || !pePadTraceHeader(record, 1))) {
            fprintf(stderr, "[pad] cannot write recording: %s\n", recordPath); exit(1);
        }
        const char* control = getenv("PE2_PAD_CONTROL");
        controlEnabled = control && strcmp(control, "1") == 0;
        if (controlEnabled) {
            const char* headless = getenv("PSYZ_HEADLESS");
            if (!replay || !headless || strcmp(headless, "1") != 0) {
                fprintf(stderr, "[pad] control requires PE2_PAD_REPLAY and PSYZ_HEADLESS=1\n");
                exit(1);
            }
        }
        traceInitialized = 1;
    }
    if (replay && !replayFinished) {
        int result = pePadTraceRead(replay, padFrame, frame, (size_t)len);
        if (result <= 0) {
            fprintf(stderr, "[pad] replay %s at frame %llu\n", result == 0 ? "complete" : "invalid", (unsigned long long)padFrame);
            if (result < 0 || !controlEnabled) exit(result == 0 ? 0 : 1);
            replayFinished = 1;
        }
    }
    if (replayFinished) pePadControlRead(padFrame, frame, (size_t)len);
    if (record && !pePadTraceWrite(record, padFrame, frame, (size_t)len)) {
        fprintf(stderr, "[pad] recording failed at frame %llu\n", (unsigned long long)padFrame); exit(1);
    }
    padFrame++;
}

void Psyz_PadsOverride(int port, char* frame, int len)
{
    pePadFastMovieVsync(port);
    int currentTick = pePadTickCount++;
    pePadSkipIntro(port, currentTick);
    pePadTraceTick(port, currentTick);
    if (port == 0) {
        pePadDumpVram(currentTick);
        pePadDumpFrame(currentTick);
        pePadWarp(currentTick);
    }
    if (port != 0 || len < 4) return;
    peMemoryCardAdvanceIo();
    pePadHoldButtons(frame, currentTick);
    pePadAutopress(frame, currentTick);
    /* Replays replace the complete sample after development overrides, so
     * keyboard/controller input and autoplay cannot alter a recorded route. */
    pePadRecordReplay(frame, len);
}

MATRIX* gHostPlayerCoordMtx;
