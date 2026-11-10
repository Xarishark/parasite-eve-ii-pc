#include "port/backgrounds.h"
#include "background_capture_target.h"
#include "port/background_pack.h"
#include "port/gpu_state.h"
#include "port/foreground_stage.h"
#include "port/background_foreground.h"
#include <psyz/gpu.h>
#include <psyz/video.h>
#include <SDL3/SDL.h>
#include <libgpu.h>
#include <libetc.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { u_long addr,len,words[3]; } AtlasSelection;
typedef struct { DR_TPAGE mode; AtlasSelection selection; SPRT_16 sprite; } BackgroundTile;
static BackgroundTile tilePackets[480];
static DR_TPAGE restorePacket;
static DR_AREA opaqueClipPacket,opaqueRestorePacket;

static int selectAtlas(const u_long* words,int available,void* unused)
{
    (void)unused;
    if(available<3)return -1;
    uint64_t key=(uint64_t)(uint32_t)words[1]|((uint64_t)(uint32_t)words[2]<<32);
    Psyz_GpuSelectSpriteAtlas(key,(uint32_t)words[0]&0xffffff);
    return 3;
}

static uint16_t nativeRows[PE_BACKGROUND_PIXELS];
static uint64_t previousKey,observedKey;
static PeBackgroundPack currentPack;
static int packAvailable;
static PeForegroundPack foregroundPack;
static int foregroundAvailable,packLocation[4];
static int materialPresented;
static int nativeFrameRequired;
static char packRoot[4096];
static int configured;
static int packConfigurationSet,packEnabled=1,packDirty;

int peBackgroundConfigurePack(const char* path,int enabled)
{
    if(!path || strlen(path)>=sizeof packRoot || strpbrk(path,"\r\n") || (enabled!=0 && enabled!=1))return 0;
    /* Re-selecting the same folder explicitly reloads edited/replaced artwork. */
    strcpy(packRoot,path);packEnabled=enabled;packDirty=1;
    /* Stop claiming the previous pack's side coverage immediately. Keep
     * the user's widescreen preference for rooms with valid wider art. */
    peBackgroundSetCoverage(0,0,320);peBackgroundSetCoverage(272,0,320);
    packConfigurationSet=1;
    return 1;
}

static struct { int y; PeBackgroundFrameInfo info; } frameInfo[2]={{.y=-1},{.y=-1}};
enum { GPU_AUDIT_MAX_SPRITES=8192 };
typedef struct {uint32_t words[4];PeGpuState state;} GpuSpriteAudit;
static GpuSpriteAudit* gpuAuditSprites;
static uint32_t* gpuAuditCommands;
static unsigned gpuAuditCommandWords;
enum {GPU_AUDIT_MAX_COMMAND_WORDS=262144};
static PeGpuState gpuAuditState;
static PeForegroundStage gpuAuditStage;
static int gpuAuditEnabled=-1,gpuAuditStarted,gpuAuditComplete,gpuAuditY;
static unsigned gpuAuditCount;
static unsigned foregroundDrawn,drawFrameIndex;
static void observeGpuSprite(const void* packet,unsigned offset,const uint32_t* words,unsigned count,const PeGpuState* state,void* context)
{
    if(count+1>GPU_AUDIT_MAX_COMMAND_WORDS-gpuAuditCommandWords) {gpuAuditComplete=0;return;}
    gpuAuditCommands[gpuAuditCommandWords++]=count;
    memcpy(gpuAuditCommands+gpuAuditCommandWords,words,count*4);gpuAuditCommandWords+=count;
    peForegroundStageObserve(packet,offset,words,count,state,&gpuAuditStage);
    (void)context;
    if(((words[0]>>24)&~3u)!=0x64)return;
    if(gpuAuditCount==GPU_AUDIT_MAX_SPRITES) {gpuAuditComplete=0;return;}
    GpuSpriteAudit* sprite=&gpuAuditSprites[gpuAuditCount++];
    memcpy(sprite->words,words,sizeof sprite->words);sprite->state=*state;
}
/* PE2's buffers are [0,239] and [272,511]. PsyZ's draw classifier
 * expects two contiguous display heights. Extend only the draw-time range;
 * drain all commands before restoring the real 240-line presentation range.
 * Raw GP1 avoids changing libgpu's cached DISPENV or the game's timing. */
static unsigned beginWideRender(int enabled,DISPENV* saved)
{
    if(!enabled || !Psyz_VideoGetWidescreen())return 240;
    GetDispEnv(saved);
    if(saved->isrgb24 || saved->disp.w!=320 || saved->disp.h!=240 ||
       (saved->screen.h && saved->screen.h!=240) || saved->screen.y<0 || saved->screen.y>2)return 240;
    DrawSync(0);Psyz_GpuDisplayCommand(0x07000000u|(256u<<10));
    return 256;
}
static void endWideRender(unsigned height,const DISPENV* saved)
{
    if(height!=256)return;
    DrawSync(0);
    unsigned start=saved->screen.y+(GetVideoMode()==MODE_PAL?0x13u:0x10u);
    Psyz_GpuDisplayCommand(0x07000000u|start|((start+240u)<<10));
}
void peBackgroundSubmitOt(const void* head,int allow_staging)
{
    PeBackgroundFrameInfo info;int foregroundReady=materialPresented && foregroundAvailable && packAvailable && packEnabled &&
        Psyz_VideoGetInternalResolution()>1 && peBackgroundGetFrameInfo(gpuAuditY,&info) && info.key==foregroundPack.background_key;
    for(unsigned i=0;i<4 && foregroundReady;i++)if((unsigned)info.location[i]!=foregroundPack.location[i])foregroundReady=0;
    PeGpuState before;int canReplace=0;
    if((gpuAuditEnabled==1 || foregroundReady) && gpuAuditComplete) {
        if(!gpuAuditStarted) {
            DRAWENV environment;GetDrawEnv(&environment);
            gpuAuditComplete=peGpuStateFromEnv(&gpuAuditState,&environment);gpuAuditStarted=1;
        }
        before=gpuAuditState;
        if(gpuAuditComplete && !peGpuWalkOt(&gpuAuditState,head,gpuAuditEnabled==1?observeGpuSprite:NULL,NULL))gpuAuditComplete=0;
        canReplace=gpuAuditComplete && foregroundReady;
    }
    DISPENV savedDisplay;unsigned renderHeight=beginWideRender(materialPresented,&savedDisplay);
    if(canReplace)foregroundDrawn+=peForegroundSubmitOt(head,&before,&foregroundPack,renderHeight,allow_staging);
    else DrawOTag((OT_TYPE*)head);
    endWideRender(renderHeight,&savedDisplay);
}
static int writeGpuAudit(const char* root,int buffer_y,const PeBackgroundFrameInfo* info)
{
    char path[4352];snprintf(path,sizeof path,"%s/gpu-state.json",root);
    FILE* file=fopen(path,"w");if(!file)return 0;
    fprintf(file,"{\"format\":\"pe2-gpu-state-audit-v1\",\"complete\":%s,\"buffer_y\":%d,\"key\":\"%016" PRIx64 "\",\"location\":[%d,%d,%d,%d,%d,%d],\"sprites\":[",
            gpuAuditStarted && gpuAuditComplete && gpuAuditY==buffer_y?"true":"false",buffer_y,info->key,
            info->location[0],info->location[1],info->location[2],info->location[3],info->location[4],info->location[5]);
    for(unsigned i=0;i<gpuAuditCount;i++) {
        const GpuSpriteAudit* sprite=&gpuAuditSprites[i];const PeGpuState* s=&sprite->state;
        const uint32_t* w=sprite->words;
        fprintf(file,"%s{\"command\":%u,\"position\":[%d,%d],\"size\":[%u,%u],\"uv\":[%u,%u],\"clut\":%u,\"clip\":[%d,%d,%d,%d],\"offset\":[%d,%d],\"draw_mode\":%u,\"texture_window\":%u,\"mask_mode\":%u}",
                i?",":"",w[0],(int)(int16_t)w[1],(int)(int16_t)(w[1]>>16),w[3]&65535,w[3]>>16,
                w[2]&255,(w[2]>>8)&255,w[2]>>16,s->clip_left,s->clip_top,
                s->clip_right-s->clip_left+1,s->clip_bottom-s->clip_top+1,s->offset_x,s->offset_y,
                s->draw_mode,s->texture_window,s->mask_mode);
    }
    fprintf(file,"],\"foreground_replacements\":%u,\"staging_free_pages\":\"%08" PRIx32 "\"}\n",foregroundDrawn,~gpuAuditStage.occupied);int failed=ferror(file);if(fclose(file))failed=1;
    if(failed) {remove(path);return 0;}
    snprintf(path,sizeof path,"%s/gpu-commands.bin",root);file=fopen(path,"wb");if(!file)return 0;
    for(unsigned i=0;i<gpuAuditCommandWords;i++)for(unsigned b=0;b<4;b++)fputc((gpuAuditCommands[i]>>(8*b))&255,file);
    failed=ferror(file);if(fclose(file))failed=1;
    if(failed)remove(path);return !failed;
}
static int frameSlot(int y)
{
    for(int i=0;i<2;i++)if(frameInfo[i].y==y)return i;
    int slot=frameInfo[0].y<0?0:1;
    frameInfo[slot].y=y;memset(&frameInfo[slot].info,0,sizeof frameInfo[slot].info);
    return slot;
}
void peBackgroundBeginFrame(int y)
{
    nativeFrameRequired=peBackgroundConsumeNativeFrame(y);
    if(nativeFrameRequired)peBackgroundSetCoverage(y,0,320);
    drawFrameIndex++;
    int slot=frameSlot(y);memset(&frameInfo[slot].info,0,sizeof frameInfo[slot].info);
    if(gpuAuditEnabled<0) {
        const char* enabled=getenv("PE2_DUMP_GPU_STATE");gpuAuditEnabled=enabled && !strcmp(enabled,"1");
        if(gpuAuditEnabled) {
            gpuAuditSprites=malloc(GPU_AUDIT_MAX_SPRITES*sizeof(*gpuAuditSprites));
            gpuAuditCommands=malloc(GPU_AUDIT_MAX_COMMAND_WORDS*4);
        }
    }
    peForegroundStageBegin(&gpuAuditStage,NULL);
    gpuAuditStarted=0;gpuAuditComplete=!gpuAuditEnabled || (gpuAuditSprites!=NULL && gpuAuditCommands!=NULL);gpuAuditCommandWords=0;gpuAuditCount=0;gpuAuditY=y;foregroundDrawn=0;materialPresented=0;
}
int peBackgroundGetFrameInfo(int y,PeBackgroundFrameInfo* info)
{
    if(!info)return 0;
    for(int i=0;i<2;i++)if(frameInfo[i].y==y && frameInfo[i].info.key) {*info=frameInfo[i].info;return 1;}
    return 0;
}

static int location[6],locationDirty=1;
void peBackgroundSetLocation(int stage,int area,int view,int room_state,int warp,int variant)
{
    int next[6]={stage,area,view,room_state,warp,variant};
    if(memcmp(location,next,sizeof location)) {memcpy(location,next,sizeof location);locationDirty=1;}
}

static void recordLocation(uint64_t key)
{
    const char* root=getenv("PE2_DUMP_BACKGROUNDS");
    if(!root || !*root)return;
    char path[4352];
    if(!SDL_CreateDirectory(root))return;
    int n=snprintf(path,sizeof path,"%s/observations.jsonl",root);
    if(n<0 || n>=(int)sizeof path)return;
    FILE* file=fopen(path,"a");
    if(file) {
        fprintf(file,"{\"key\":\"%016" PRIx64 "\",\"stage\":%d,\"area\":%d,\"view\":%d,\"room_state\":%d,\"warp\":%d,\"variant\":%d}\n",
                key,location[0],location[1],location[2],location[3],location[4],location[5]);
        fclose(file);
    }
}

static void dumpNative(uint64_t key)
{
    const char* root=getenv("PE2_DUMP_BACKGROUNDS");
    if(!root || !*root)return;
    char path[4352];
    if(!SDL_CreateDirectory(root) || snprintf(path,sizeof path,"%s/%016" PRIx64 ".rgb555",root,key)>=(int)sizeof path)return;
    FILE* file=fopen(path,"rb");
    if(file) { fclose(file);return; }
    file=fopen(path,"wb");
    if(!file)return;
    for(unsigned i=0;i<PE_BACKGROUND_PIXELS;i++) {
        fputc(nativeRows[i]&255,file);fputc(nativeRows[i]>>8,file);
    }
    int failed=ferror(file);if(fclose(file))failed=1;
    if(failed)remove(path);
    else fprintf(stderr,"[background] captured %016" PRIx64 "\n",key);
}

static int materialIsOpaque(const char* path,const PeBackgroundPack* pack)
{
    SDL_Surface* image=SDL_LoadPNG(path);
    if(!image)return 0;
    SDL_Surface* rgba=SDL_ConvertSurface(image,SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(image);
    if(!rgba)return 0;
    int valid=rgba->w==16*68 && rgba->h==(int)((pack->cells+15)/16)*136;
    for(unsigned cell=0;cell<pack->cells && valid;cell++) {
        unsigned x=cell%16*68,y=cell/16*68;
        for(unsigned row=0;row<68 && valid;row++) {
            const unsigned char* pixels=(const unsigned char*)rgba->pixels+(y+row)*rgba->pitch+x*4;
            for(unsigned column=0;column<68;column++)if(pixels[column*4+3]!=255) {valid=0;break;}
        }
    }
    SDL_DestroySurface(rgba);return valid;
}

static void preparePack(uint64_t key)
{
    if(previousKey==key && !memcmp(packLocation,location,sizeof packLocation))return;
    previousKey=key;
    memcpy(packLocation,location,sizeof packLocation);
    /* One live background's atlas: room/view changes cannot accumulate GPU memory.
     * Missing/animated unmodified sources do not force an idle GPU wait. */
    if(packAvailable)Psyz_GpuResetSpriteAtlases();
    packAvailable=foregroundAvailable=0;
    if(!packEnabled || !*packRoot)return;
    char path[4352];
    int n=snprintf(path,sizeof path,"%s/%016" PRIx64 ".pe2bg",packRoot,key);
    if(n<0 || n>=(int)sizeof path)return;
    FILE* file=fopen(path,"rb");
    if(!file)return;
    unsigned char metadata[PE_BACKGROUND_META_BYTES+1];
    size_t bytes=fread(metadata,1,sizeof metadata,file);
    int failed=ferror(file);fclose(file);
    if(failed || !peBackgroundParsePack(&currentPack,metadata,bytes,key)) {
        fprintf(stderr,"[background] invalid metadata for %016" PRIx64 "; using original\n",key);return;
    }
    n=snprintf(path,sizeof path,"%s/%016" PRIx64 ".png",packRoot,key);
    if(n<0 || n>=(int)sizeof path)return;
    /* Verify atlas extent against metadata before the shared GPU loader sees it. */
    unsigned char pngHeader[24];
    file=fopen(path,"rb");
    int valid=0;
    if(file) {
        size_t got=fread(pngHeader,1,sizeof pngHeader,file);
        if(!fseek(file,0,SEEK_END)) {
            long encoded=ftell(file);
            valid=encoded>=24 && encoded<=64*1024*1024 &&
                  peBackgroundCheckPngHeader(pngHeader,got,&currentPack);
        }
        fclose(file);
    }
    if(!valid || !materialIsOpaque(path,&currentPack) || Psyz_GpuLoadSpriteAtlas(key,path)!=1) {
        fprintf(stderr,"[background] unavailable atlas for %016" PRIx64 "; using original\n",key);return;
    }
    packAvailable=1;
    fprintf(stderr,"[background] loaded %016" PRIx64 " span %d..%d\n",key,currentPack.left,currentPack.right);
    n=snprintf(path,sizeof path,"%s/%016" PRIx64 "-%d-%d-%d-%d.pe2fg",packRoot,key,location[0],location[1],location[2],location[3]);
    if(n<0 || n>=(int)sizeof path)return;
    file=fopen(path,"rb");if(!file)return;
    unsigned char foregroundMetadata[PE_FOREGROUND_PACK_HEADER+PE_FOREGROUND_MAX_SOURCES*PE_FOREGROUND_PACK_ENTRY+1];
    bytes=fread(foregroundMetadata,1,sizeof foregroundMetadata,file);failed=ferror(file);fclose(file);
    if(failed || !peForegroundParsePack(&foregroundPack,foregroundMetadata,bytes,key,location)) {
        fprintf(stderr,"[foreground] invalid metadata; using original sprites\n");return;
    }
    n=snprintf(path,sizeof path,"%s/%016" PRIx64 ".fg.png",packRoot,foregroundPack.atlas_key);
    if(n<0 || n>=(int)sizeof path)return;
    PeBackgroundPack dimensions={.cells=foregroundPack.cells};valid=0;file=fopen(path,"rb");
    if(file) {
        size_t got=fread(pngHeader,1,sizeof pngHeader,file);
        if(!fseek(file,0,SEEK_END)) {long encoded=ftell(file);valid=encoded>=24 && encoded<=64*1024*1024 && peBackgroundCheckPngHeader(pngHeader,got,&dimensions);}
        fclose(file);
    }
    if(!valid || !materialIsOpaque(path,&dimensions) || Psyz_GpuLoadSpriteAtlas(foregroundPack.atlas_key,path)!=1) {
        fprintf(stderr,"[foreground] unavailable atlas; using original sprites\n");return;
    }
    foregroundAvailable=1;
    fprintf(stderr,"[foreground] loaded %u sources, %u cells\n",foregroundPack.count,foregroundPack.cells);
}

/* Re-present an unmodified MDEC image in the wide display plane. There is
 * deliberately no scenery in the side columns. The reference plane keeps
 * native VRAM authoritative, including opaque RGB555 zero backgrounds. */
static void prepareNativePack(uint64_t key)
{
    if(packAvailable)return;
    if(Psyz_VideoSetWidescreen(1)!=0)return;
    Psyz_VideoSetWidescreen(0);
    enum { columns=28, cells=columns*15, rows=(cells+15)/16, width=16*68, height=rows*136 };
    SDL_Surface* atlas=SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32);
    if(!atlas)return;
    SDL_memset(atlas->pixels,0,(size_t)atlas->pitch*height);
    for(unsigned cell=0;cell<cells;cell++) {
        int x=(int)(cell%columns)*16-64,y=(int)(cell/columns)*16;
        for(int plane=0;plane<2;plane++)for(int v=0;v<68;v++)for(int u=0;u<68;u++) {
            int px=(u<2?0:u>65?63:u-2)/4;
            int py=(v<2?0:v>65?63:v-2)/4;
            unsigned char* dst=(unsigned char*)atlas->pixels+
                (cell/16*68+v+plane*rows*68)*atlas->pitch+(cell%16*68+u)*4;
            uint16_t word=x>=0 && x<320?nativeRows[(y+py)*320+x+px]:0;
            for(int c=0;c<3;c++)dst[c]=((word>>(c*5))&31)*255/31;
            dst[3]=!plane || (x>=0 && x<320)?255:0;
        }
    }
    /* The public atlas API takes a filename. Use a private temporary file,
     * remove it immediately after upload, and never install a generated pack. */
    const char* directory=SDL_GetPrefPath("parasite-eve","parasite-eve-ii");
    char path[4352];
    int n=directory?snprintf(path,sizeof path,"%snative-wide-%" PRIu64 ".png",directory,SDL_GetTicksNS()):-1;
    int loaded=0;
    if(n>0 && n<(int)sizeof path) {
        if(SDL_SavePNG(atlas,path))loaded=Psyz_GpuLoadSpriteAtlas(key,path)==1;
        remove(path); /* Also remove a partial file after a failed save. */
    }
    SDL_DestroySurface(atlas);
    if(loaded) {
        currentPack=(PeBackgroundPack){.key=key,.left=-64,.right=384,.cells=cells};
        packAvailable=1;
    }
}

void peBackgroundDraw(const void* source,int columns,int buffer_y,int offset_y)
{
    materialPresented=0;
    if(!configured) {
        const char* root=getenv("PE2_HD_BACKGROUNDS");
        if(!packConfigurationSet)peBackgroundConfigurePack(root?root:"",1);
        Psyz_GpuRegisterCommandHandler(0xf8,selectAtlas,NULL);
        configured=1;
    }
    if(packDirty) {
        if(packAvailable)Psyz_GpuResetSpriteAtlases();
        packAvailable=foregroundAvailable=0;previousKey=0;packDirty=0;
    }
    /* Cropped transitions and movies do not claim static full-frame coverage. */
    if(!source || offset_y || (!peBackgroundGetWidescreen() && !(packEnabled && *packRoot) && !getenv("PE2_DUMP_BACKGROUNDS") && !getenv("PE2_DUMP_FRAME") && !getenv("PE2_SURVEY_TARGET") && !getenv("PE2_CAPTURE_FRAME_COUNT"))) {
        peBackgroundSetCoverage(buffer_y,0,320);return;
    }
    uint64_t key=peBackgroundCopyNative(nativeRows,source,columns);
    int slot=frameSlot(buffer_y);
    frameInfo[slot].info.key=key;memcpy(frameInfo[slot].info.location,location,sizeof location);
    if(observedKey!=key || locationDirty) {recordLocation(key);locationDirty=0;}
    if(observedKey!=key) {dumpNative(key);observedKey=key;}
    /* PsyZ's material plane is display-only above native resolution. At 1x,
     * enabling wide without that plane would expose an unsqueezed LoadImage. */
    if(nativeFrameRequired || Psyz_VideoGetInternalResolution()<2) {peBackgroundSetCoverage(buffer_y,0,320);return;}
    preparePack(key);
    if(!packAvailable && peBackgroundGetWidescreen())prepareNativePack(key);
    if(!packAvailable) { peBackgroundSetCoverage(buffer_y,0,320);return; }
    int wide=peBackgroundSetCoverage(buffer_y,currentPack.left,currentPack.right);
    DRAWENV saved;
    GetDrawEnv(&saved);
    DISPENV display;GetDispEnv(&display);
    if((buffer_y!=0 && buffer_y!=272) || saved.clip.x || saved.clip.y!=buffer_y || saved.clip.w!=320 || saved.clip.h!=240 ||
       display.isrgb24 || display.disp.w!=320 || display.disp.h!=240 ||
       (display.screen.h && display.screen.h!=240) || display.screen.y<0 || display.screen.y>2) {
        peBackgroundSetCoverage(buffer_y,0,320);return;
    }
    unsigned cell=0,used=0;
    void* first=NULL;
    void* previous=NULL;
    for(int y=0;y<240;y+=16)for(int x=currentPack.left;x<currentPack.right;x+=16,cell++) {
        if(!wide && (x<0 || x>=320))continue;
        BackgroundTile* tile=&tilePackets[used++];
        int sourceX=x<0 || x>=320?0:x;
        setDrawTPage(&tile->mode,0,0,getTPage(2,0,sourceX&~255,buffer_y&~255));
        setlen(&tile->selection,3);
        tile->selection.words[0]=0xf8800000u|cell;
        tile->selection.words[1]=(uint32_t)key;
        tile->selection.words[2]=(uint32_t)(key>>32);
        setSprt16(&tile->sprite);setRGB0(&tile->sprite,128,128,128);
        setXY0(&tile->sprite,x-saved.ofs[0],y+buffer_y-saved.ofs[1]);
        tile->sprite.u0=sourceX&255;tile->sprite.v0=(buffer_y&255)+y;tile->sprite.clut=0;
        if(previous)catPrim(previous,&tile->mode);else first=&tile->mode;
        catPrim(&tile->mode,&tile->selection);catPrim(&tile->selection,&tile->sprite);
        previous=&tile->sprite;
    }
    setDrawTPage(&restorePacket,saved.dfe,saved.dtd,saved.tpage);
    if(previous)catPrim(previous,&restorePacket);
    termPrim(&restorePacket);
    if(!wide && first) {
        /* A direct LoadImage background is opaque even at RGB555 zero. The
         * normal texture path would turn those pixels into black pinholes.
         * Use PsyZ's existing material-only plane; a 321-pixel scissor
         * keeps WidescreenDraw false at the original frame height, without
         * needlessly marking unrelated CLUT rows as drawn. Neither buffer
         * gets squeezed. The
         * temporary switch is completely drained/restored before room OTs. */
        RECT broad={saved.clip.x,saved.clip.y,saved.clip.w+1,saved.clip.h},original=saved.clip;
        SetDrawArea(&opaqueClipPacket,&broad);SetDrawArea(&opaqueRestorePacket,&original);
        catPrim(&opaqueClipPacket,first);first=&opaqueClipPacket;
        termPrim(&opaqueRestorePacket);
        DrawSync(0);Psyz_VideoSetWidescreen(1);
    }
    /* One FIFO submission preserves atlas selection per sprite and avoids a
     * separate GPU submission/transfer allocation for every single tile. */
    DISPENV savedDisplay;unsigned renderHeight=beginWideRender(wide && first,&savedDisplay);
    if(first)DrawOTag(first);
    DrawSync(0);
    endWideRender(renderHeight,&savedDisplay);
    if(!wide && first) {
        Psyz_VideoSetWidescreen(0);
        /* Restoring E4h inside the same list would classify queued vertices
         * using the restored narrow area before its flush. Drain first. */
        DrawPrim(&opaqueRestorePacket);DrawSync(0);peBackgroundSetCoverage(buffer_y,0,320);
    }
    if(first)materialPresented=1;
}

/* Opt-in settled survey or transient checkpoint. Ordinary gameplay skips both. */
void peBackgroundEndFrame(int buffer_y,int field_frame,unsigned game_tick,unsigned vsync_count,
                          const PeBackgroundSimulationState* simulation)
{
    static int initialized,enabled,matchingFrames,settleFrames=30;
    static PeBackgroundCaptureTarget surveyTarget,checkpointTarget;
    static int checkpointTargetPresent;
    static unsigned checkpointFrame;
    static char root[4096];
    if(!initialized) {
        const char* target=getenv("PE2_SURVEY_TARGET");
        const char* output=getenv("PE2_SURVEY_OUTPUT");
        const char* settle=getenv("PE2_SURVEY_SETTLE_FRAMES");
        const char* checkpoint=getenv("PE2_CAPTURE_FRAME_COUNT");
        const char* uiCheckpoint=getenv("PE2_CAPTURE_UI_FRAME");
        if(!checkpoint)checkpoint=uiCheckpoint;
        const char* checkpointOutput=getenv("PE2_CAPTURE_OUTPUT");
        const char* captureTarget=getenv("PE2_CAPTURE_TARGET");
        if(settle) {char* end;long value=strtol(settle,&end,10);if(*settle && !*end && value>=30 && value<=36000)settleFrames=(int)value;}
        if(target && output && strlen(output)<sizeof root &&
           peBackgroundParseCaptureTarget(target,&surveyTarget)) {
            strcpy(root,output);enabled=1;
        }
        /* A transient checkpoint is deliberately a different evidence format.
         * It must never satisfy the settled-view survey's 30-frame gate. */
        if(!target && checkpoint && checkpointOutput && *checkpointOutput && strlen(checkpointOutput)<sizeof root) {
            char* end;unsigned long long value=strtoull(checkpoint,&end,10);
            if(*checkpoint && strlen(checkpoint)<=10 && strspn(checkpoint,"0123456789")==strlen(checkpoint) && !*end && value>0 && value<=UINT32_MAX) {
                if(captureTarget && !peBackgroundParseCaptureTarget(captureTarget,&checkpointTarget)) {
                    fprintf(stderr,"[capture] invalid PE2_CAPTURE_TARGET; checkpoint disabled\n");
                } else {
                    checkpointTargetPresent=captureTarget!=NULL;
                    checkpointFrame=(unsigned)value;strcpy(root,checkpointOutput);enabled=uiCheckpoint?3:2;
                }
            }
        }
        initialized=1;
    }
    if(!enabled)return;
    PeBackgroundFrameInfo info={0};
    if(enabled==2 || enabled==3) {
        if(!simulation || (enabled==3?simulation->ui_frame:simulation->frame_count)!=checkpointFrame ||
           (enabled!=3 && !peBackgroundGetFrameInfo(buffer_y,&info)) ||
           (checkpointTargetPresent && !peBackgroundCaptureMatchesTarget(&checkpointTarget,&info)))return;
        if(enabled==3)memcpy(info.location,location,sizeof location);
        matchingFrames=0;
    } else {
        if(!field_frame || !simulation || !peBackgroundGetFrameInfo(buffer_y,&info) ||
           !peBackgroundCaptureMatchesTarget(&surveyTarget,&info)) {matchingFrames=0;return;}
        if(++matchingFrames<settleFrames)return;
    }
    DrawSync(0);
    /* PE2 displays the opposite buffer while drawing. This diagnostic exits
     * after capture: present the completed draw buffer so pixels, OT state,
     * native VRAM and reported source all describe the very same frame. */
    DISPENV savedDisplay,captureDisplay;GetDispEnv(&savedDisplay);captureDisplay=savedDisplay;
    captureDisplay.disp.y=buffer_y;PutDispEnv(&captureDisplay);DrawSync(0);
    int w=0,h=0;
    unsigned char* rgb=Psyz_VideoAllocRenderedFrame(&w,&h);
    if(!rgb)exit(2);
    size_t nonzero=0;
    for(size_t i=0;i<(size_t)w*h*3;i++)nonzero+=rgb[i]!=0;
    if(!nonzero) {free(rgb);PutDispEnv(&savedDisplay);return;} /* Never accept the initial blackout. */
    uint16_t* vram=malloc(1024*512*2);
    if(!vram || !SDL_CreateDirectory(root))exit(2);
    RECT all={0,0,1024,512};StoreImage(&all,(u_long*)vram);DrawSync(0);
    char path[4352];
    snprintf(path,sizeof path,"%s/frame.ppm",root);
    FILE* file=fopen(path,"wb");if(!file)exit(2);
    fprintf(file,"P6\n%d %d\n255\n",w,h);fwrite(rgb,3,(size_t)w*h,file);
    int failed=ferror(file);if(fclose(file))failed=1;
    snprintf(path,sizeof path,"%s/vram.bin",root);file=fopen(path,"wb");if(!file)exit(2);
    for(unsigned i=0;i<1024*512;i++) {fputc(vram[i]&255,file);fputc(vram[i]>>8,file);}
    failed|=ferror(file);if(fclose(file))failed=1;
    free(rgb);free(vram);if(failed)exit(2);
    if(gpuAuditEnabled==1 && !writeGpuAudit(root,buffer_y,&info))exit(2);
    snprintf(path,sizeof path,"%s/report.json",root);file=fopen(path,"w");if(!file)exit(2);
    fprintf(file,"{\"format\":\"%s\",\"matched_frames\":%d,\"key\":\"%016" PRIx64 "\",\"location\":[%d,%d,%d,%d,%d,%d],\"width\":%d,\"height\":%d,\"buffer_y\":%d,\"foreground_replacements\":%u,\"draw_frame\":%u,\"game_tick\":%u,\"vsync_count\":%u,\"simulation_state\":{\"anim_frame\":%u,\"frame_count\":%u,\"loop_count\":%u,\"loop_ticks\":%u,\"random_lcg\":%u,\"random_bios\":%u},\"player_root\":",
            enabled==3?"pe2-ui-frame-checkpoint-v1":enabled==2?"pe2-frame-checkpoint-v1":"pe2-static-view-survey-v3",matchingFrames,info.key,info.location[0],info.location[1],info.location[2],info.location[3],info.location[4],info.location[5],w,h,buffer_y,foregroundDrawn,drawFrameIndex,game_tick,vsync_count,
            simulation->anim_frame,simulation->frame_count,simulation->loop_count,simulation->loop_ticks,simulation->random_lcg,simulation->random_bios);
    if(simulation->player_root_valid) {
        fputc('[',file);
        for(unsigned i=0;i<12;i++)fprintf(file,"%s%" PRId32,i?",":"",simulation->player_root[i]);
        fputc(']',file);
    } else fputs("null",file);
    fprintf(file,",\"ui_frame\":%u",simulation->ui_frame);
    fprintf(file,",\"player_pose\":{\"parts\":%u,\"local\":\"%016" PRIx64 "\",\"composed\":\"%016" PRIx64 "\",\"view\":\"%016" PRIx64 "\"},",
        simulation->player_parts,simulation->player_pose_hash,simulation->player_composed_hash,simulation->view_hash);
    fprintf(file,"\"models_complete\":%s,\"models\":[",simulation->models_complete?"true":"false");
    for(unsigned i=0;i<simulation->model_count;i++) {
        const PeBackgroundModelState* model=&simulation->models[i];
        fprintf(file,"%s{\"parts\":%u,\"flags\":%u,\"root\":[%" PRId32 ",%" PRId32 ",%" PRId32 "],\"local\":\"%016" PRIx64 "\",\"composed\":\"%016" PRIx64 "\"}",
            i?",":"",model->parts,model->flags,model->root_translation[0],model->root_translation[1],model->root_translation[2],model->local_hash,model->composed_hash);
    }
    fputs("]}\n",file);
    failed=ferror(file);if(fclose(file))failed=1;
    if(failed) {remove(path);exit(2);}
    fprintf(stderr,"[survey] captured %d/%d/%d room %d key %016" PRIx64 "\n",info.location[0],info.location[1],info.location[2],info.location[3],info.key);
    exit(0);
}
