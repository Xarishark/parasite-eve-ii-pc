/* Test the production scene-sector reader with serialized 2048-byte sectors. */
#include "../src/shared/scene/scene_sector.inc.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
CdCmdQueue gCdCmdQueue;
u8* gFsActorBuffer0Base;
u8* gFsActorBuffer1Base;
u8* gFsActorBuffer2Base;
static unsigned char sectors[3][2048];
static unsigned sectorIndex, sectorCursor, reads;
void memCopyBytes(const void* source, void* destination, u32 size) { memcpy(destination, source, size); }
int CdGetSector(void* destination, int words)
{
    if (sectorIndex == 0 && sectorCursor == 0) CHECK(words == 15);
    CHECK(words >= 0 && sectorCursor + (unsigned)words * 4 <= 2048);
    memcpy(destination, sectors[sectorIndex] + sectorCursor, (unsigned)words * 4);
    sectorCursor += (unsigned)words * 4;
    ++reads;
    return 1;
}
static void exercise(int kind, int reuse, int force)
{
    const size_t timingBytes = 3860, payloadBytes = sizeof sectors - sizeof(StreamSceneImageHeader);
    const size_t prefix = kind >= 1 && kind <= 3 ? STREAM_VLC_TABLE_BYTES + timingBytes : 0;
    const size_t arenaBytes = 16 + prefix + payloadBytes + 16;
    unsigned char* arena = malloc(arenaBytes);
    CHECK(arena != NULL);
    memset(arena, 0xA5, arenaBytes);
    unsigned char* destination = arena + 16 + prefix;
    for (size_t i = 0; i < sizeof sectors; ++i) ((unsigned char*)sectors)[i] = (unsigned char)(i * 7 + 3);
    StreamSceneImageHeader header = {.imageDataOffset = 64, .timingDataOffset = 1024, .nextDecodeBufferBytes = 8192, .sectorCount = 3, .viewId = 7, .bufferKind = kind, .forceReload = force, .timingDataBytes = 120};
    memcpy(sectors, &header, sizeof header);
    memset(&gCdCmdQueue, 0, sizeof gCdCmdQueue);
    StreamSlot slot = {0};
    slot.data.scene.vlcBufferKind = kind;
    slot.control.scene.timingBufferKind = kind + 1;
    slot.data.scene.timingBufferBytes = timingBytes;
    gCdCmdQueue.sceneStream = &slot;
    gCdCmdQueue.decodeBuffer = arena + 16;
    gCdCmdQueue.externalScenePayloadBuffer = arena + 16;
    gCdCmdQueue.scenePayloadReusable = reuse;
    gFsActorBuffer0Base = gFsActorBuffer1Base = gFsActorBuffer2Base = arena + 16;
    _gStreamSceneReadState.phase = STREAM_SCENE_READ_HEADER;
    const int skip = reuse && !force;
    reads = 0;
    for (sectorIndex = 0; sectorIndex < 3; ++sectorIndex) {
        sectorCursor = 0;
        CHECK(streamReadSceneImageSector(0, 0) == 0);
        CHECK(sectorCursor == (skip ? (sectorIndex == 0 ? 60 : 0) : 2048));
        CHECK(gCdCmdQueue.scenePayloadLoading == (sectorIndex != 2));
        CHECK(gCdCmdQueue.scenePayloadAvailable == (sectorIndex == 2));
    }
    CHECK(reads == (skip ? 1 : 4));
    CHECK(_gStreamScenePayloadWriteCursor == destination + payloadBytes);
    CHECK(_gStreamSceneReadState.phase == STREAM_SCENE_READ_HEADER);
    if (skip) {
        for (size_t i = 0; i < arenaBytes; ++i) CHECK(arena[i] == 0xA5);
        CHECK(gCdCmdQueue.sceneImageHeaders[kind].sectorCount == 0);
    } else {
        CHECK(memcmp(&gCdCmdQueue.sceneImageHeaders[kind], &header, sizeof header) == 0);
        CHECK(memcmp(destination, (unsigned char*)sectors + sizeof header, payloadBytes) == 0);
        for (size_t i = 0; i < 16 + prefix; ++i) CHECK(arena[i] == 0xA5);
        for (size_t i = arenaBytes - 16; i < arenaBytes; ++i) CHECK(arena[i] == 0xA5);
        if (kind == STREAM_SCENE_BUFFER_DECODE) CHECK(gCdCmdQueue.nextDecodeBufferBytes == 8192);
    }
    free(arena);
}
int main(void)
{
    _Static_assert(sizeof(StreamSceneImageHeader) == 60, "serialized scene header size");
    for (int kind = 0; kind <= 4; ++kind) exercise(kind, 0, 0);
    exercise(STREAM_SCENE_BUFFER_ACTOR_1, 1, 0);
    exercise(STREAM_SCENE_BUFFER_ACTOR_1, 1, 1);
    puts("scene sectors: full header, contiguous payload, five buffers, VLC/timing prefixes and reuse OK");
    return 0;
}
