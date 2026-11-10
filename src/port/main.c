#include "port/disc_assets.h"
#include "port/asset_bind.h"
#include "port/host_ui.h"
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#endif
#include <psyz.h>
#include <psyz/cd.h>
#include <psyz/video.h>
#include <libgpu.h>
#include <libetc.h>
#include "gamemain.h"

/* Crash report: where it faulted and the call chain (also the link register, since a call through a
 * NULL function pointer leaves no frame of its own). */
extern void* gHostCurrentTaskCallback;
#ifndef _WIN32
static void crashHandler(int sig, siginfo_t* info, void* ctx)
{
    void* frames[32];
    int count;
#if defined(__APPLE__) && defined(__arm64__)
    ucontext_t* uc = ctx;
    fprintf(stderr, "\nCRASH signal %d addr=%p pc=%p lr=%p\n", sig, info->si_addr,
            (void*)uc->uc_mcontext->__ss.__pc, (void*)uc->uc_mcontext->__ss.__lr);
    frames[0] = (void*)uc->uc_mcontext->__ss.__lr;
    fprintf(stderr, "last task callback:\n");
    {
        void* cb[1] = { gHostCurrentTaskCallback };
        backtrace_symbols_fd(cb, 1, 2);
    }
    count = backtrace(frames + 1, 30) + 1;
#else
    (void)ctx;
    fprintf(stderr, "\nCRASH signal %d addr=%p\n", sig, info->si_addr);
    count = backtrace(frames, 32);
#endif
    backtrace_symbols_fd(frames, count, 2);
    _exit(128 + sig);
}
#endif

/* Port entry. Disc mounting goes here once the runtime loader exists; then the
 * original start-up path runs on the host stack (no SetSp). */
int main(int argc, char** argv) {
#ifndef _WIN32
    struct sigaction sa = { .sa_sigaction = crashHandler, .sa_flags = SA_SIGINFO };
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#endif
    /* Disc image: first argument, or PE2_DISC. Game data is read from it at run time. */
    const char* disc = NULL;
    int startup = peHostStartup(argc, argv, &disc);
    if (startup <= 0) return startup < 0 ? 2 : 0;
    if (Psyz_CdSetDiskPath(disc) != 0) {
        peHostReportError("Cannot mount the disc image \"%s\". Check that the file exists and that it is a CUE sheet or BIN "
                          "image of the North American Parasite Eve II (disc 1). Choose another image with --host-menu.", disc);
        return 2;
    }
    Psyz_SetTitle("Parasite Eve II");
    /* Game tables the original source embedded at build time are filled from the disc here. */
    {
        extern int peAssetsDump(const char*);
        struct PeDisc* assets = peDiscOpen(disc);
        if (!assets) {
            peHostReportError("Cannot read the game files from \"%s\": %s.", disc, peDiscError());
            return 2;
        }
        int loaded = peAssetsLoad(assets);
        if (loaded <= 0) {
            peHostReportError("Cannot load the game tables from \"%s\"; the image may be damaged.", disc);
            peDiscClose(assets);
            return 2;
        }
        fprintf(stderr, "[assets] %d arrays loaded from the disc\n", loaded);
        if (getenv("PE2_DISC2_ASSETS")) { /* development: load disc 2's arrays now, to compare the dump */
            struct PeDisc* second = peDiscOpen(getenv("PE2_DISC2_ASSETS"));
            if (second) { fprintf(stderr, "[assets] %d arrays loaded from disc 2\n", peAssetsLoad(second)); peDiscClose(second); }
        }
        if (getenv("PE2_DUMP_ASSETS")) peAssetsDump(getenv("PE2_DUMP_ASSETS"));
        peDiscClose(assets);
    }
    if (getenv("PE2_DEBUG_PORT")) { /* development: PsyZ HTTP debug server on 127.0.0.1 (/screenshot, /vram, /input) */
        extern int Psyz_DebugServer(int);
        int debugPort = atoi(getenv("PE2_DEBUG_PORT"));
        if (debugPort <= 0 || debugPort > 65535) {
            fprintf(stderr, "[debug] ignoring invalid PE2_DEBUG_PORT: %s\n", getenv("PE2_DEBUG_PORT"));
        } else {
            fprintf(stderr, "[debug] server on port %d\n", Psyz_DebugServer(debugPort));
        }
    }
    ResetGraph(0);
    gameMainRun();
    return 0;
}
