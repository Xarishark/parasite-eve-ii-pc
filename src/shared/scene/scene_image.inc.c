/* Shared production scene-image decoder, isolated for sanitizer fixtures. */
#include "main/stage.h"
#include "main/fs.h"
#include "../../main/fs.h"
#include "main/display.h"
#include "main/session.h"
#include "main/mem.h"
#include "main/stream.h"
#include "main/tmd.h"
#include <libpress.h>

static void _mdecImageStripCallback(void);

/// Polls spent waiting for a scene-image header or its decoded output.
///
/// Both waits time out on poll 91. Decode start and timeout clear the counter;
/// standalone requests also clear it. Scene requests do not reset a header wait.
static s32 _gMdecSceneWaitPolls;

/// Current MDEC image-output strip index (0..19 for the observed multiplier 1).
///
/// Requests and bookkeeping completion reset it; the output callback advances
/// it only when chaining another fixed-size DMA transfer into the shared workspace.
static u16 _gMdecImageStripIndex;

/// Observed image-output layout: one native 16-pixel strip per DMA transfer.
enum { MDEC_IMAGE_NATIVE_STRIP_WIDTH_MULTIPLIER = 1 };

/// Multiplier of the strip width used to derive the image-output transfer count.
///
/// Both decode starts store 1, yielding twenty strips. The DMA word count stays
/// fixed; no other multiplier or image layout is established by the writers.
static u16 _gMdecImageStripWidthMultiplier;

/// Borrowed word-aligned BS input for the next image's VLC expansion.
///
/// A standalone request supplies words directly; a scene request selects a byte
/// offset within its payload. Keep the complete input readable until VLC consumes
/// it in the decode-start state; expanded commands go to the auxiliary workspace.
static u_long* _gMdecImageBitstream;

/// Borrowed scene payload byte base for the selected image header's offsets.
///
/// Selects decode, actor or external storage after any VLC/timing reservations.
/// Image/list/chunk/timing extents must fit the reserved payload and remain live
/// through their consumption. This is compressed input storage, not VLC output.
static u8* _gMdecScenePayloadBase;

/// Borrowed cached scene-image header selected by the requested view ID.
///
/// Points into the resident CD queue's five-header cache after successful lookup.
/// Failed lookup retains the prior pointer; timeout also reads it. The selected
/// header and corresponding payload must remain valid through decode completion.
static const StreamSceneImageHeader* _gMdecSceneImageHeader;

void mdecRequestSceneImageDecode(const u8* viewId)
{
    u16         headerIndex;
    u16         headerFound;
    s16         bufferKind;
    s32         requestedView;
    s32         imageDataOffset;
    CdCmdQueue* queue;

    queue         = &gCdCmdQueue;
    headerIndex   = 0;
    headerFound   = 0;
    requestedView = *viewId;
    for (; headerIndex < ARRAY_SIZE(queue->sceneImageHeaders); headerIndex++) {
        if (requestedView == queue->sceneImageHeaders[headerIndex].viewId) {
            headerFound = 1;
            break;
        }
    }
    if (!headerFound || queue->scenePayloadLoading != 0) {
        queue->imageDecodePending = 1;
        queue->imageLoadStatus    = CD_COMMAND_IMAGE_PENDING;
        queue->imageDecodeStep    = CD_COMMAND_IMAGE_WAIT_HEADER;
        return;
    }

    // Payload bytes follow any VLC and timing reservations in a shared actor arena.
    _gMdecSceneImageHeader = &queue->sceneImageHeaders[headerIndex];
    bufferKind             = _gMdecSceneImageHeader->bufferKind;
    switch (bufferKind) {
        case STREAM_SCENE_BUFFER_DECODE:
            _gMdecScenePayloadBase = queue->decodeBuffer;
            break;
        case STREAM_SCENE_BUFFER_ACTOR_0:
            _gMdecScenePayloadBase = gFsActorBuffer0Base;
            if (queue->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_0) {
                _gMdecScenePayloadBase = gFsActorBuffer0Base + STREAM_VLC_TABLE_BYTES;
            }
            if (queue->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_0) {
                _gMdecScenePayloadBase = _gMdecScenePayloadBase + queue->sceneStream->data.scene.timingBufferBytes;
            }
            gGameSession->field_7C = 0;
            break;
        case STREAM_SCENE_BUFFER_ACTOR_1:
            _gMdecScenePayloadBase = gFsActorBuffer1Base;
            if (queue->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_1) {
                _gMdecScenePayloadBase = gFsActorBuffer1Base + STREAM_VLC_TABLE_BYTES;
            }
            if (queue->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_1) {
                _gMdecScenePayloadBase = _gMdecScenePayloadBase + queue->sceneStream->data.scene.timingBufferBytes;
            }
            gGameSession->field_7E = 0;
            break;
        case STREAM_SCENE_BUFFER_ACTOR_2:
            _gMdecScenePayloadBase = gFsActorBuffer2Base;
            if (queue->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_2) {
                _gMdecScenePayloadBase = gFsActorBuffer2Base + STREAM_VLC_TABLE_BYTES;
            }
            if (queue->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_2) {
                _gMdecScenePayloadBase = _gMdecScenePayloadBase + queue->sceneStream->data.scene.timingBufferBytes;
            }
            gGameSession->field_80 = 0;
            break;
        case STREAM_SCENE_BUFFER_EXTERNAL:
            _gMdecScenePayloadBase = queue->externalScenePayloadBuffer;
            break;
    }
    imageDataOffset           = _gMdecSceneImageHeader->imageDataOffset;
    _gMdecImageStripIndex     = 0;
    queue->imageDecodePending = 1;
    queue->imageLoadStatus    = CD_COMMAND_IMAGE_PENDING;
    queue->imageDecodeStep    = CD_COMMAND_IMAGE_START;
    _gMdecImageBitstream      = (u_long*)(_gMdecScenePayloadBase + imageDataOffset);
}

/// Releases image-decode bookkeeping and restores model buffers when requested.
///
/// Also used on the scene decoder's timeout path. This marks the image complete
/// and invalidates scene-payload reuse; it does not stop MDEC or remove its callback.
static inline void _mdecFinishImageDecode(void)
{
    CdCmdQueue* queue = &gCdCmdQueue;

    if (gDisplayState.keepGraphics == 0) {
        tmdResetAuxHeapAndRestoreBuffers();
    }
    queue->imageLoadStatus      = CD_COMMAND_IMAGE_COMPLETE;
    queue->imageDecodePending   = 0;
    _gMdecImageStripIndex       = 0;
    queue->imageDecodeStep      = CD_COMMAND_IMAGE_START;
    queue->scenePayloadReusable = 0;
}

/// Steps a cached scene image decode, its optional image uploads and timing copy.
///
/// Retries header selection from the live view, starts VLC/MDEC, then waits for
/// all twenty output strips before consuming byte-offset payload records. Header
/// and output waits finish after 91 polls as well. Requires aligned, in-bounds
/// payload offsets and live image, VLC, timing and auxiliary workspaces. The VLC
/// expansion is unrestricted; this function does not check its output capacity.
/// A missing-header timeout has no payload capacity to commit. Output timeouts
/// retain the selected header and do not stop MDEC DMA themselves.
static void _mdecStepSceneImageDecode(void)
{
    enum {
        MDEC_SCENE_TIMEOUT_POLLS         = 91,
        MDEC_VLC_UNLIMITED_OUTPUT        = 0,
        STREAM_SCENE_STRIP_X_SHIFT_PAGES = -8,
        STREAM_SCENE_CHUNK_Y_SHIFT_ROWS  = -3,
    };
    CdCmdQueue* queue;
    u16         imageIndex;
    s32         stripUploadResult;

    queue = &gCdCmdQueue;
    switch (queue->imageDecodeStep) {
        case CD_COMMAND_IMAGE_WAIT_HEADER:
            mdecRequestSceneImageDecode(&gGameSession->location.view);
            if (queue->imageDecodeStep == CD_COMMAND_IMAGE_WAIT_HEADER &&
                (u32)++_gMdecSceneWaitPolls >= MDEC_SCENE_TIMEOUT_POLLS) {
                _gMdecSceneWaitPolls = 0;
                gpuResetAndInvalidateModelBuffers();
                /* No header was selected for this view. The cached pointer may
                 * be null (cold entry) or belong to an earlier payload. Neither
                 * can authorize a decode-buffer capacity update. */
                _mdecFinishImageDecode();
            }
            break;
        case CD_COMMAND_IMAGE_START:
            gpuResetAndInvalidateModelBuffers();
            queue->mdecOutputPending = 1;
            if (queue->sceneVlcTableMode == STREAM_SCENE_VLC_IMAGE_BUFFER) {
                DecDCTvlcBuild((u16*)((u8*)gFsImageWorkspacePtr + FILE_SYSTEM_IMAGE_VLC_OFFSET));
                queue->rebuildImageVlcTable = 0;
                queue->vlcTable             = (u16*)((u8*)gFsImageWorkspacePtr + FILE_SYSTEM_IMAGE_VLC_OFFSET);
            }
            DecDCTReset(0);
            DecDCTvlcSize2(MDEC_VLC_UNLIMITED_OUTPUT);
            DecDCTvlc2(_gMdecImageBitstream, gMemActiveAuxHeap,
                       queue->vlcTable);
            _gMdecImageStripWidthMultiplier = MDEC_IMAGE_NATIVE_STRIP_WIDTH_MULTIPLIER;
            DecDCToutCallback(_mdecImageStripCallback);
            DecDCTin(gMemActiveAuxHeap, queue->imageMdecMode);
            queue->imageMdecMode = MDEC_IMAGE_MODE_RGB16;
            DecDCTout(gFsImageWorkspacePtr->strips[0], FILE_SYSTEM_IMAGE_STRIP_WORDS);
            _gMdecSceneWaitPolls = 0;
            queue->imageDecodeStep++;
            /* fallthrough */
        case CD_COMMAND_IMAGE_WAIT_OUTPUT:
            if (queue->mdecOutputPending == 0) {
                // Upload the payload's optional strip lists and image chunks.
                for (imageIndex = 0; imageIndex < ARRAY_SIZE(_gMdecSceneImageHeader->stripListOffsets); imageIndex++) {
                    if (_gMdecSceneImageHeader->stripListOffsets[imageIndex] != 0) {
                        if (_gMdecSceneImageHeader->relocateStripLists[imageIndex] != 0) {
                            gFsChunkLoadPolicy  = FILE_SYSTEM_CHUNK_LOAD_RELOCATE_IMAGES;
                            gFsImageXPageOffset = STREAM_SCENE_STRIP_X_SHIFT_PAGES;
                        }
                        fsBeginImageColumns((FsImageColumn*)(_gMdecScenePayloadBase + _gMdecSceneImageHeader->stripListOffsets[imageIndex]));
                        // Both upload polls are retained; a retry restarts this column stream.
                        while (fsUploadImageStrips(FILE_SYSTEM_IMAGE_STRIPS_RESIDENT_INPUT) != FILE_SYSTEM_IMAGE_STRIPS_COMPLETE) {
                            stripUploadResult = fsUploadImageStrips(FILE_SYSTEM_IMAGE_STRIPS_RESIDENT_INPUT);
                            if (stripUploadResult == FILE_SYSTEM_IMAGE_STRIPS_COMPLETE) {
                                break;
                            }
                            if (stripUploadResult == FILE_SYSTEM_IMAGE_STRIPS_RETRY) {
                                fsBeginImageColumns((FsImageColumn*)(_gMdecScenePayloadBase + _gMdecSceneImageHeader->stripListOffsets[imageIndex]));
                            }
                        }
                        gFsChunkLoadPolicy  = FILE_SYSTEM_CHUNK_LOAD_NORMAL;
                        gFsImageXPageOffset = 0;
                    }
                }
                for (imageIndex = 0; imageIndex < ARRAY_SIZE(_gMdecSceneImageHeader->imageChunkOffsets); imageIndex++) {
                    if (_gMdecSceneImageHeader->imageChunkOffsets[imageIndex] != 0) {
                        if (_gMdecSceneImageHeader->relocateImageChunks[imageIndex] != 0) {
                            gFsChunkLoadPolicy       = FILE_SYSTEM_CHUNK_LOAD_RELOCATE_IMAGES;
                            gFsImageHeaderYShiftRows = STREAM_SCENE_CHUNK_Y_SHIFT_ROWS;
                        }
                        while (fsUploadImageChunk((const FsImageChunk*)(_gMdecScenePayloadBase + _gMdecSceneImageHeader->imageChunkOffsets[imageIndex]), 1)) {
                        }
                        gFsChunkLoadPolicy       = FILE_SYSTEM_CHUNK_LOAD_NORMAL;
                        gFsImageHeaderYShiftRows = 0;
                    }
                }
                queue->imageLayout = FILE_SYSTEM_IMAGE_STRIPS;
                if (_gMdecSceneImageHeader->bufferKind == STREAM_SCENE_BUFFER_DECODE) {
                    queue->decodeBufferBytes = queue->nextDecodeBufferBytes;
                }
                // Refresh timing data before releasing the completed decode operation.
                if (queue->sceneStream->control.scene.timingBufferKind != STREAM_TIMING_BUFFER_NONE) {
                    memCopyBytes(&_gMdecScenePayloadBase[_gMdecSceneImageHeader->timingDataOffset], queue->timingBuffer,
                                 _gMdecSceneImageHeader->timingDataBytes);
                }
                _mdecFinishImageDecode();
            } else if ((u32)++_gMdecSceneWaitPolls >= MDEC_SCENE_TIMEOUT_POLLS) {
                _gMdecSceneWaitPolls = 0;
                gpuResetAndInvalidateModelBuffers();
                if (_gMdecSceneImageHeader->bufferKind == STREAM_SCENE_BUFFER_DECODE) {
                    queue->decodeBufferBytes = queue->nextDecodeBufferBytes;
                }
                _mdecFinishImageDecode();
            }
            break;
    }
}

