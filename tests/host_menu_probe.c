/* Render the real settings painter without opening a desktop window. */
#include "../src/port/host_ui.c"
void Psyz_PadsOverride(int port,char* frame,int length) {(void)port;(void)frame;(void)length;}
/* These actions are outside the aspect-selection probe. Fail if reached. */
int peBackgroundConfigurePack(const char* path,int enabled) {(void)path;(void)enabled;abort();}
PeDisc* peDiscOpen(const char* path) {(void)path;abort();}
void peDiscClose(PeDisc* disc) {(void)disc;abort();}
int peDiscNumber(PeDisc* disc) {(void)disc;abort();}
const char* peDiscError(void) {abort();}
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    Psyz_VideoSetInternalResolution(4);applyWidescreen(1);
    if(Psyz_VideoGetInternalResolution()!=4)return 5;
    /* Aspect selection must work independently of experimental art packs. */
    s_overlayState.page=0;s_overlayState.row=1;
    applyWidescreen(0);Psyz_VideoSetAspectMode(PSYZ_ASPECT_DISPLAY);
    changeRow(1,1);
    if(peBackgroundGetWidescreen() || Psyz_VideoGetAspectMode()!=PSYZ_ASPECT_SQUARE)return 5;
    changeRow(1,1);
    if(!peBackgroundGetWidescreen() || Psyz_VideoGetAspectMode()!=PSYZ_ASPECT_DISPLAY)return 5;
    changeRow(1,1);
    if(peBackgroundGetWidescreen() || Psyz_VideoGetAspectMode()!=PSYZ_ASPECT_DISPLAY)return 5;
    changeRow(-1,1);
    if(!peBackgroundGetWidescreen())return 5;
    Psyz_VideoSetInternalResolution(2);
    unsigned minimum=Psyz_VideoGetInternalResolution(); /* Software stays native. */
    Psyz_VideoSetInternalResolution(1);applyWidescreen(1);
    if(Psyz_VideoGetInternalResolution()!=minimum)return 5;
    s_overlayState.row=0;changeRow(-1,1);
    if(Psyz_VideoGetInternalResolution()!=minimum)return 5;
    applyWidescreen(0);changeRow(-1,1);
    if(Psyz_VideoGetInternalResolution()!=1)return 5;
    applyWidescreen(1);
    strcpy(hdPackPath,"/local/backgrounds/gallery-pack");hdEnabled=1;
    Psyz_VideoSetInternalResolution(4);
    s_overlayState.page=0;s_overlayState.row=9;
    OverlaySurface surface={0};OverlayLayout layout={.scale=1};
    if(!OverlayFontReady() || !OverlaySurfaceResize(&surface,680,OverlayDesignHeight(VISIBLE_ROWS)))return 2;
    paintPanel(&surface,&layout);
    FILE* file=fopen(argv[1],"wb");if(!file)return 2;
    fprintf(file,"P6\n%d %d\n255\n",surface.w,surface.h-1);
    for(int y=0;y<surface.h-1;y++)for(int x=0;x<surface.w;x++)
        fwrite(surface.pixels+((size_t)y*surface.w+x)*4,1,3,file);
    int failed=ferror(file);if(fclose(file))failed=1;
    OverlaySurfaceRelease(&surface);
    return failed?2:0;
}
