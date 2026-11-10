/* Production RGB555 fill, shared with the isolated image-boundary test. */
#include "common.h"
#include "main/fs.h"

static inline void _gameOverFillRedImage(void)
{
    enum { GAME_OVER_RED_FLASH_PIXEL = 0x001F };
    u32* imageWords;
    u16  wordIndex;
    u32  redPixelPair;

    // Two RGB555 pixels occupy each word; all twenty strips are contiguous.
    imageWords   = gFsImageWorkspacePtr->strips[0];
    redPixelPair = (GAME_OVER_RED_FLASH_PIXEL << 16) | GAME_OVER_RED_FLASH_PIXEL;
    wordIndex    = 0;
    do {
        *imageWords++ = redPixelPair;
        wordIndex++;
    } while ((u32)wordIndex <= ARRAY_SIZE(gFsImageWorkspacePtr->strips) * ARRAY_SIZE(gFsImageWorkspacePtr->strips[0]) - 1);
}

