#include "port/backgrounds.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1); } } while(0)
static int effective, leftEdge, rightEdge, bufferY, unsupported;
void Psyz_VideoSetWidescreenDisplayOnly(int enabled) { CHECK(enabled==1); }
int Psyz_VideoSetWidescreen(int enabled) { effective=unsupported?0:enabled;return unsupported?-1:0; }
void Psyz_VideoSetWidescreenPicture(int y,int left,int right) { bufferY=y;leftEdge=left;rightEdge=right; }
int main(void)
{
    peBackgroundRequestNativeFrame(-1);peBackgroundRequestNativeFrame(2);
    CHECK(!peBackgroundConsumeNativeFrame(0) && !peBackgroundConsumeNativeFrame(272));
    peBackgroundRequestNativeFrame(1);peBackgroundRequestNativeFrame(1);
    CHECK(!peBackgroundConsumeNativeFrame(256) && !peBackgroundConsumeNativeFrame(0));
    CHECK(peBackgroundConsumeNativeFrame(272) && !peBackgroundConsumeNativeFrame(272));
    peBackgroundRequestNativeFrame(0);peBackgroundRequestNativeFrame(1);
    CHECK(peBackgroundConsumeNativeFrame(0) && peBackgroundConsumeNativeFrame(272));
    peBackgroundSetWidescreen(1);
    CHECK(peBackgroundGetWidescreen()==1 && effective==0);
    CHECK(peBackgroundSetCoverage(272,0,320)==0);
    CHECK(effective==0 && leftEdge==0 && rightEdge==320 && bufferY==272);
    CHECK(peBackgroundSetCoverage(0,-64,384)==1 && effective==1);
    CHECK(leftEdge==-64 && rightEdge==384);
    CHECK(peBackgroundSetCoverage(272,-32,384)==0 && effective==0);
    CHECK(peBackgroundSetCoverage(0,-97,384)==0);
    CHECK(peBackgroundSetCoverage(0,-64,417)==0);
    CHECK(peBackgroundSetCoverage(0,320,0)==0);
    CHECK(peBackgroundSetCoverage(0,-54,374)==1);
    peBackgroundSetWidescreen(0);
    CHECK(peBackgroundGetWidescreen()==0 && effective==0);
    CHECK(peBackgroundSetCoverage(0,-64,384)==0);
    peBackgroundSetWidescreen(1);unsupported=1;
    CHECK(peBackgroundSetCoverage(0,-64,384)==0 && effective==0);
    CHECK(leftEdge==0 && rightEdge==320);
    puts("Background coverage: missing, partial and unsupported artwork stay original 4:3");
    return 0;
}
