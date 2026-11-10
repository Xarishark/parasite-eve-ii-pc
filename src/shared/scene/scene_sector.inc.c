/* Shared production scene-sector intake; also compiled by its focused test. */
#include "gameplay/scene_runtime.h"
#include "main/fs.h"
#include "main/mem.h"
#include "main/stream.h"
#include <libcd.h>

/// Next payload byte in the selected borrowed decode, actor or external buffer.
///
/// Actor destinations skip the active VLC/timing prefixes. Both transferred
/// and reused payloads advance this cursor; the complete sector payload must
/// fit its selected buffer until the read completes.
static u8* _gStreamScenePayloadWriteCursor;

/// Phases of scene-image sector intake.
enum { STREAM_SCENE_READ_HEADER  = 0,
       STREAM_SCENE_READ_PAYLOAD = 1 };

/// Transfer policy for a selected scene payload.
enum { STREAM_SCENE_PAYLOAD_SKIP = 0,
       STREAM_SCENE_PAYLOAD_COPY = 1 };

/// Header/payload phase and retained destination of the current scene-image read.
///
/// Selecting a scene resets only the phase. A nonempty image header replaces
/// the destination with its bufferKind halfword; it remains valid across the
/// following payload sectors and selects the matching cached-header slot.
/// Buffer kinds must be 0..4. Neither field owns the selected storage.
typedef struct {
    u16 phase;      // Sector intake phase (0 header, 1 payload)
    u16 bufferKind; // Payload destination (0 decode, 1/2/3 actor buffers 0/1/2, 4 external); retained during payload intake
} _StreamSceneReadState;
STATIC_ASSERT_SIZEOF(_StreamSceneReadState, 4);

/// Scene-sector callback state, reset to the header phase when a scene is selected.
static _StreamSceneReadState _gStreamSceneReadState;

/// Payload sectors still to consume after the header sector.
///
/// Nonempty serialized counts must be 1..32768. Zero completes the payload;
/// completion stores -1 until the next header replaces it. Halfword decrement
/// casts preserve the unsigned arithmetic view before signed storage.
static s16 _gStreamScenePayloadSectorsRemaining;

/// Whether the current payload is copied (1) or only consumed for reuse (0).
///
/// Cleared when forceReload is zero and a reusable payload already exists.
/// Skipping preserves destination selection, cursor advance and completion.
static s16 _gStreamScenePayloadTransferEnabled;

/// Retained forceReload halfword for the current serialized image.
///
/// Zero permits payload reuse; nonzero forces transfer and prevents marking
/// the completed payload reusable. Only the zero/nonzero distinction is used.
static s16 _gStreamScenePayloadForceReload;

s32 streamReadSceneImageSector(s32 unused0, s32 unused1)
{
    /// Selects the payload cursor after any actor-buffer VLC/timing prefix.
    ///
    /// bufferKind is 0..4, evaluated once. queue must be a stable live pointer:
    /// it is read repeatedly, and decodeCapacityBytes only for decode storage.
    /// Updates the shared write cursor and, for kind 0, the next decode capacity.
    /// Captures no caller locals; borrows preallocated buffers without bounds checks.
#define STREAM_SELECT_PAYLOAD_DESTINATION(queue, bufferKind, decodeCapacityBytes)                       \
    switch (bufferKind) {                                                                               \
        case STREAM_SCENE_BUFFER_DECODE:                                                                \
            _gStreamScenePayloadWriteCursor = (queue)->decodeBuffer;                                    \
            (queue)->nextDecodeBufferBytes  = (decodeCapacityBytes);                                    \
        default:                                                                                        \
            break;                                                                                      \
        case STREAM_SCENE_BUFFER_ACTOR_0:                                                               \
            _gStreamScenePayloadWriteCursor = gFsActorBuffer0Base;                                      \
            if ((queue)->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_0) {          \
                _gStreamScenePayloadWriteCursor = gFsActorBuffer0Base + STREAM_VLC_TABLE_BYTES;         \
            }                                                                                           \
            if ((queue)->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_0) { \
                _gStreamScenePayloadWriteCursor += (queue)->sceneStream->data.scene.timingBufferBytes;  \
            }                                                                                           \
            break;                                                                                      \
        case STREAM_SCENE_BUFFER_ACTOR_1:                                                               \
            _gStreamScenePayloadWriteCursor = gFsActorBuffer1Base;                                      \
            if ((queue)->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_1) {          \
                _gStreamScenePayloadWriteCursor = gFsActorBuffer1Base + STREAM_VLC_TABLE_BYTES;         \
            }                                                                                           \
            if ((queue)->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_1) { \
                _gStreamScenePayloadWriteCursor += (queue)->sceneStream->data.scene.timingBufferBytes;  \
            }                                                                                           \
            break;                                                                                      \
        case STREAM_SCENE_BUFFER_ACTOR_2:                                                               \
            _gStreamScenePayloadWriteCursor = gFsActorBuffer2Base;                                      \
            if ((queue)->sceneStream->data.scene.vlcBufferKind == STREAM_VLC_BUFFER_ACTOR_2) {          \
                _gStreamScenePayloadWriteCursor = gFsActorBuffer2Base + STREAM_VLC_TABLE_BYTES;         \
            }                                                                                           \
            if ((queue)->sceneStream->control.scene.timingBufferKind == STREAM_TIMING_BUFFER_ACTOR_2) { \
                _gStreamScenePayloadWriteCursor += (queue)->sceneStream->data.scene.timingBufferBytes;  \
            }                                                                                           \
            break;                                                                                      \
        case STREAM_SCENE_BUFFER_EXTERNAL:                                                              \
            _gStreamScenePayloadWriteCursor = (queue)->externalScenePayloadBuffer;                      \
            break;                                                                                      \
    }

    StreamSceneImageHeader header;
    CdCmdQueue*            queue;

    queue = &gCdCmdQueue;
    switch (_gStreamSceneReadState.phase) {
        case STREAM_SCENE_READ_HEADER:
            // CD transfers count 32-bit words, including on LP64 hosts.
            // Read the serialized header before choosing or reusing its payload buffer.
            CdGetSector(&header, sizeof(header) / sizeof(u32));
            _gStreamScenePayloadTransferEnabled = STREAM_SCENE_PAYLOAD_COPY;
            _gStreamScenePayloadForceReload     = header.forceReload;
            if ((_gStreamScenePayloadForceReload == 0) && ((s16)queue->scenePayloadReusable != 0)) {
                _gStreamScenePayloadTransferEnabled = STREAM_SCENE_PAYLOAD_SKIP;
            }
            if ((s16)header.sectorCount != 0) {
                // The member-address form keeps its halfword address folded across the copy.
                queue->scenePayloadLoading            = 1;
                *(&_gStreamSceneReadState.bufferKind) = header.bufferKind;
                if (_gStreamScenePayloadTransferEnabled != 0) {
                    memCopyBytes(&header, &queue->sceneImageHeaders[*(&_gStreamSceneReadState.bufferKind)], sizeof(header));
                }
                STREAM_SELECT_PAYLOAD_DESTINATION(queue, *(&_gStreamSceneReadState.bufferKind), header.nextDecodeBufferBytes);
#undef STREAM_SELECT_PAYLOAD_DESTINATION
                if (_gStreamScenePayloadTransferEnabled != 0) {
                    CdGetSector(_gStreamScenePayloadWriteCursor, SECTOR_SIZE - sizeof(header) / sizeof(u32));
                }
                _gStreamScenePayloadWriteCursor     += SECTOR_SIZE * sizeof(u32) - sizeof(header);
                _gStreamSceneReadState.phase         = STREAM_SCENE_READ_PAYLOAD;
                _gStreamScenePayloadSectorsRemaining = header.sectorCount - 1;
            }
            break;
        case STREAM_SCENE_READ_PAYLOAD:
            if (_gStreamScenePayloadSectorsRemaining > 0) {
                if (_gStreamScenePayloadTransferEnabled != 0) {
                    CdGetSector(_gStreamScenePayloadWriteCursor, SECTOR_SIZE);
                }
                _gStreamScenePayloadWriteCursor     += SECTOR_SIZE * sizeof(u32);
                _gStreamScenePayloadSectorsRemaining = (u16)_gStreamScenePayloadSectorsRemaining - 1;
            }
            if (_gStreamScenePayloadSectorsRemaining == 0) {
                _gStreamScenePayloadSectorsRemaining = (u16)_gStreamScenePayloadSectorsRemaining - 1;
                if (gCdCmdQueue.sceneEnded == 0) {
                    gCdCmdQueue.scenePayloadAvailable = 1;
                }
                gCdCmdQueue.scenePayloadLoading = 0;
                if (_gStreamScenePayloadForceReload == 0) {
                    gCdCmdQueue.scenePayloadReusable = 1;
                }
                _gStreamSceneReadState.phase = STREAM_SCENE_READ_HEADER;
            }
            break;
    }
    return 0;
}

