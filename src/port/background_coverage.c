#include "port/backgrounds.h"
#include <psyz/video.h>

static int requestedWidescreen;
static unsigned nativeFrames;

void peBackgroundRequestNativeFrame(int buffer_index)
{
    if(buffer_index>=0 && buffer_index<2)nativeFrames|=1u<<buffer_index;
}
int peBackgroundConsumeNativeFrame(int buffer_y)
{
    if(buffer_y!=0 && buffer_y!=272)return 0;
    unsigned bit=1u<<(buffer_y==272),result=nativeFrames&bit;
    nativeFrames&=~bit;return result!=0;
}

void peBackgroundSetWidescreen(int enabled)
{
    requestedWidescreen = enabled != 0;
    Psyz_VideoSetWidescreenDisplayOnly(1);
    /* Drop previous-room coverage immediately; the presenter re-establishes
     * it before drawing. LoadImage backdrops are not squeezed by PsyZ, so
     * merely enabling its widescreen flag would misalign unmodified rooms. */
    Psyz_VideoSetWidescreen(0);
}

int peBackgroundGetWidescreen(void)
{
    return requestedWidescreen;
}

int peBackgroundSetCoverage(int buffer_y, int left, int right)
{
    /* 16:9 at 240 lines needs 426 2/3 pixels. Use an even, centred minimum
     * and stay inside PsyZ's supported room margin. Partial artwork is 4:3. */
    int wide = requestedWidescreen && left >= -96 && left <= -54 &&
               right >= 374 && right <= 416 && right - left >= 428;
    if (Psyz_VideoSetWidescreen(wide) != 0) wide = 0;
    Psyz_VideoSetWidescreenPicture(buffer_y, wide ? left : 0, wide ? right : 320);
    return wide;
}
