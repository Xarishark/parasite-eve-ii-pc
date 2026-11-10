/* A requested camera can be absent from an arriving scene stream. Exercise
 * the production wait path with no selected header and with a stale header. */
#include "../src/shared/scene/scene_image.inc.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
CdCmdQueue gCdCmdQueue;
DisplayState gDisplayState;
GameSession* gGameSession;
u8 *gFsActorBuffer0Base,*gFsActorBuffer1Base,*gFsActorBuffer2Base;
FsImgBuffers* gFsImageWorkspacePtr;
void* gMemActiveAuxHeap;
u8 gFsChunkLoadPolicy;
s8 gFsImageHeaderYShiftRows,gFsImageXPageOffset;
void memCopyBytes(const void* source,void* destination,u32 size) {memcpy(destination,source,size);}
void fsBeginImageColumns(const FsImageColumn* columns) {(void)columns;abort();}
u8 fsUploadImageChunk(const FsImageChunk* chunk,u8 limit) {(void)chunk;(void)limit;abort();}
u8 fsUploadImageStrips(s32 mode) {(void)mode;abort();}
int DecDCTvlc2(u_long* source,u_long* output,DECDCTTAB table) {(void)source;(void)output;(void)table;abort();}
void DecDCTvlcBuild(u_short* table) {(void)table;abort();}
int DecDCTvlcSize2(int size) {(void)size;abort();}
static void _mdecImageStripCallback(void) { abort(); }
static int resets,restores;
void gpuResetAndInvalidateModelBuffers(void) {++resets;}
void tmdResetAuxHeapAndRestoreBuffers(void) {++restores;}
void Psyz_PadsOverride(int p,char* f,int n) {(void)p;(void)f;(void)n;}
int main(void)
{
    GameSession session={0};gGameSession=&session;session.location.view=2;
    StreamSceneImageHeader stale={.bufferKind=STREAM_SCENE_BUFFER_DECODE};
    for(int cached=0;cached<2;cached++) {
        memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);
        gCdCmdQueue.imageDecodePending=1;
        gCdCmdQueue.imageDecodeStep=CD_COMMAND_IMAGE_WAIT_HEADER;
        gCdCmdQueue.decodeBufferBytes=1234;
        gCdCmdQueue.nextDecodeBufferBytes=5678;
        _gMdecSceneImageHeader=cached?&stale:NULL;
        _gMdecSceneWaitPolls=0;
        for(int i=0;i<90;i++) {
            _mdecStepSceneImageDecode();
            CHECK(gCdCmdQueue.imageDecodePending==1);
        }
        _mdecStepSceneImageDecode();
        CHECK(gCdCmdQueue.imageDecodePending==0);
        CHECK(gCdCmdQueue.imageLoadStatus==CD_COMMAND_IMAGE_COMPLETE);
        CHECK(gCdCmdQueue.decodeBufferBytes==1234);
        CHECK(_gMdecSceneWaitPolls==0);
    }
    CHECK(resets==2 && restores==2);
    /* A matching header arriving on poll 91 starts decoding; it must not be
     * immediately discarded by the missing-header timeout on that same poll. */
    u8 payload[16]={0};
    memset(&gCdCmdQueue,0,sizeof gCdCmdQueue);
    gCdCmdQueue.decodeBuffer=payload;
    gCdCmdQueue.imageDecodePending=1;
    gCdCmdQueue.imageDecodeStep=CD_COMMAND_IMAGE_WAIT_HEADER;
    gCdCmdQueue.sceneImageHeaders[0].viewId=2;
    gCdCmdQueue.sceneImageHeaders[0].bufferKind=STREAM_SCENE_BUFFER_DECODE;
    _gMdecSceneWaitPolls=90;
    _mdecStepSceneImageDecode();
    CHECK(gCdCmdQueue.imageDecodePending==1);
    CHECK(gCdCmdQueue.imageDecodeStep==CD_COMMAND_IMAGE_START);
    CHECK(resets==2 && restores==2);
    puts("Missing scene header: cold/stale cache timeouts preserve decode capacity");
    return 0;
}
