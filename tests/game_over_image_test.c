/* The production red flash must fill exactly one packed RGB555 frame. */
#include "port/gpu_len.h"
#include "../src/shared/scene/game_over_image.inc.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
FsImgBuffers* gFsImageWorkspacePtr;
void Psyz_PadsOverride(int p, char* f, int n) { (void)p; (void)f; (void)n; }
int main(void)
{
    /* An extra frame catches the old LP64 writer without writing outside the
     * allocation. It must remain untouched, like adjacent native RAM. */
    struct Storage { FsImgBuffers image; u32 after[sizeof(FsImgBuffers) / sizeof(u32)]; };
    struct Storage* storage = malloc(sizeof(*storage));
    CHECK(storage != NULL);
    memset(storage, 0xa5, sizeof(*storage));
    gFsImageWorkspacePtr = &storage->image;
    _gameOverFillRedImage();
    for (size_t strip = 0; strip < FILE_SYSTEM_IMAGE_STRIP_COUNT; ++strip)
        for (size_t word = 0; word < FILE_SYSTEM_IMAGE_STRIP_WORDS; ++word)
            CHECK(storage->image.strips[strip][word] == 0x001f001f);
    for (size_t word = 0; word < sizeof(storage->after) / sizeof(storage->after[0]); ++word)
        CHECK(storage->after[word] == 0xa5a5a5a5);
    free(storage);
    puts("Game Over flash: all 76800 RGB555 pixels red, adjacent frame untouched OK");
    return 0;
}
