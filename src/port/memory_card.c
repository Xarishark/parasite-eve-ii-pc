/* Host storage for the retail memory-card dialog. Each card is a directory of
 * unmodified save-file bytes (15 x 8192-byte blocks), not a raw PS1 card image.
 * Override the per-user default with PE2_MEMORY_CARD_DIR for isolated replays. */
#include "port/memory_card.h"
#include <libmcrd.h>
/* The game's open modes, taken before the Windows CRT headers below redefine
 * O_RDONLY and O_WRONLY as 0 and 1. */
enum { CARD_READ = O_RDONLY, CARD_WRITE = O_WRONLY };
_Static_assert(CARD_READ == 1 && CARD_WRITE == 2, "libmcrd open modes");
#include <SDL3/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
/* windows.h clashes with the PS1 types in libmcrd.h, so the one call is declared by hand. */
__declspec(dllimport) int __stdcall MoveFileExA(const char* from, const char* to, unsigned long flags);
#define PE_MOVEFILE_REPLACE_EXISTING 0x1
#define PE_MOVEFILE_WRITE_THROUGH 0x8
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define mkdir(path, mode) _mkdir(path)
#define lstat(path, st) stat(path, st)
#define fsync(fd) _commit(fd)
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef ssize_t
typedef long long ssize_t;
#endif
/* Windows has no fnmatch: the save directory pattern only uses '*' and '?'. */
static int fnmatch(const char* pattern, const char* name, int flags)
{
    (void)flags;
    if (!*pattern) return *name != 0;
    if (*pattern == '*') {
        for (;; name++) {
            if (fnmatch(pattern + 1, name, 0) == 0) return 0;
            if (!*name) return 1;
        }
    }
    if (!*name || (*pattern != '?' && *pattern != *name)) return 1;
    return fnmatch(pattern + 1, name + 1, 0);
}
/* A unique sibling file for the atomic save write. */
static int pe_mkstemp(char* path)
{
    size_t length = strlen(path);
    static unsigned counter;
    for (int attempt = 0; attempt < 100; attempt++) {
        snprintf(path + length - 6, 7, "%06x", ((unsigned)_getpid() * 2654435761u + (counter++) * 40503u) & 0xffffff);
        int fd = _open(path, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}
#define mkstemp(path) pe_mkstemp(path)
#define write(fd, data, bytes) _write(fd, data, (unsigned)(bytes))
#define close(fd) _close(fd)
#define unlink(path) _unlink(path)
/* rename() fails on Windows when the target exists; this replaces it. */
/* Antivirus scanners briefly hold a file that was just written, so a failed
 * replace is retried for up to half a second. */
static int pe_rename(const char* from, const char* to)
{
    for (int attempt = 0; attempt < 10; attempt++) {
        if (MoveFileExA(from, to, PE_MOVEFILE_REPLACE_EXISTING | PE_MOVEFILE_WRITE_THROUGH)) return 0;
        SDL_Delay(50);
    }
    return -1;
}
#define rename(from, to) pe_rename(from, to)
#else
#include <fnmatch.h>
#include <unistd.h>
#endif

enum { CARD_BLOCK_BYTES = 8192, CARD_BLOCKS = 15, CARD_SECTOR_BYTES = 128 };
typedef struct { char name[21]; size_t bytes; int blocks; } CardFile;
static char cardRoot[PATH_MAX];
static char openPath[PATH_MAX];
static long openMode;
enum { CARD_IO_IDLE, CARD_IO_RUNNING, CARD_IO_COMPLETE };
static int started, ioState, newCard[2];
static long completedCommand, completedResult;

/* Card file names as the game sees them. Its save names end in random characters
 * from "A-Za-z0-9;:", and ':' is not allowed in Windows file names (macOS shows it
 * as '/'), so on disk ':' is stored as HOST_COLON, which the game never uses. */
enum { HOST_COLON = '!' };
static int validName(const char* name)
{
    if (!name || !*name || strlen(name) > 20 || name[0] == '.') return 0;
    return strpbrk(name, "/\\*?!") == NULL;
}

static void hostName(const char* name, char* host)
{
    for (; *name; ++name, ++host) *host = *name == ':' ? HOST_COLON : *name;
    *host = 0;
}

/* The game's name for a file on disk, or 0 for a file that is not a card file. */
static int gameName(const char* host, char* name)
{
    if (strlen(host) > 20 || strchr(host, ':')) return 0;
    for (; *host; ++host, ++name) *name = *host == HOST_COLON ? ':' : *host;
    *name = 0;
    return 1;
}

static void cardLog(const char* what, const char* path)
{
    fprintf(stderr, "[memory card] %s %s: %s\n", what, path, strerror(errno));
}

static long cardDirectory(long channel, char* path)
{
    if (!started || !cardRoot[0] || (channel != 0 && channel != 0x10)) return McErrCardNotExist;
    if (snprintf(path, PATH_MAX, "%s/card%d", cardRoot, channel == 0x10) >= PATH_MAX) return McErrCardInvalid;
    if (mkdir(path, 0700) != 0 && errno != EEXIST) { cardLog("cannot create", path); return McErrCardInvalid; }
    struct stat st;
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode)) { cardLog("not a folder:", path); return McErrCardInvalid; }
    return McErrNone;
}

static long filePath(long channel, const char* name, char* path)
{
    char directory[PATH_MAX];
    long result = cardDirectory(channel, directory);
    if (result != McErrNone) return result;
    /* A retail directory name fills all 20 bytes without a terminator. */
    char filename[21];
    if (!name) return McErrCardInvalid;
    strncpy(filename, name, 20);
    filename[20] = 0;
    char host[21];
    if (!validName(filename)) return McErrCardInvalid;
    hostName(filename, host);
    if (snprintf(path, PATH_MAX, "%s/%s", directory, host) >= PATH_MAX) return McErrCardInvalid;
    return McErrNone;
}

static int compareFiles(const void* a, const void* b)
{
    return strcmp(((const CardFile*)a)->name, ((const CardFile*)b)->name);
}

static long listFiles(long channel, CardFile* files, int* count, int* blocks)
{
    char directory[PATH_MAX], path[PATH_MAX];
    long result = cardDirectory(channel, directory);
    *count = *blocks = 0;
    if (result != McErrNone) return result;
    DIR* dir = opendir(directory);
    if (!dir) { cardLog("cannot list", directory); return McErrCardInvalid; }
    struct dirent* entry;
    for (;;) {
        /* NULL also signals an error; calls below may change errno. */
        errno = 0;
        entry = readdir(dir);
        if (!entry) {
            if (errno != 0) { cardLog("cannot list", directory); result = McErrCardInvalid; }
            break;
        }
        char name[21];
        if (!gameName(entry->d_name, name) || !validName(name)) continue;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= sizeof(path)) { result = McErrCardInvalid; break; }
        struct stat st;
        if (lstat(path, &st) != 0) { cardLog("cannot read", path); result = McErrCardInvalid; break; }
        if (!S_ISREG(st.st_mode)) continue;
        if (*count == CARD_BLOCKS || st.st_size <= 0 || st.st_size > CARD_BLOCKS * CARD_BLOCK_BYTES) {
            if (*count == CARD_BLOCKS) fprintf(stderr, "[memory card] more than %d files in %s\n", CARD_BLOCKS, directory);
            else fprintf(stderr, "[memory card] %s (%lld bytes) is not a save file; move it out of %s\n", entry->d_name, (long long)st.st_size, directory);
            result = McErrCardInvalid;
            break;
        }
        CardFile* file = &files[(*count)++];
        strcpy(file->name, name);
        file->bytes = (size_t)st.st_size;
        file->blocks = (file->bytes + CARD_BLOCK_BYTES - 1) / CARD_BLOCK_BYTES;
        *blocks += file->blocks;
    }
    if (closedir(dir) != 0) result = McErrCardInvalid;
    if (*blocks > CARD_BLOCKS) result = McErrCardInvalid;
    qsort(files, *count, sizeof(*files), compareFiles);
    return result;
}

/* Write a whole save to a temporary sibling before publishing it. Interrupted
 * writes retain the previous file; never truncate the live save in place. */
static long storeFile(const char* path, const void* data, size_t bytes)
{
    char temporary[PATH_MAX];
    const char* slash = strrchr(path, '/');
    if (!slash || snprintf(temporary, sizeof(temporary), "%.*s/.write-XXXXXX", (int)(slash - path), path) >= sizeof(temporary)) return McErrCardInvalid;
    int fd = mkstemp(temporary);
    if (fd < 0) { cardLog("cannot create a file in the folder of", path); return McErrCardInvalid; }
    const unsigned char* cursor = data;
    size_t remaining = bytes;
    int ok = 1;
    while (remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { ok = 0; break; }
        cursor += written;
        remaining -= written;
    }
    if (ok && fsync(fd) != 0) ok = 0;
    if (close(fd) != 0) ok = 0;
    if (!ok) cardLog("cannot write", temporary);
    else if (rename(temporary, path) == 0) return McErrNone;
    else cardLog("cannot replace", path);
    unlink(temporary);
    return McErrCardInvalid;
}

static long transfer(const char* path, void* data, long offset, long bytes, int writing)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return McErrFileNotExist;
        cardLog("cannot read", path);
        return McErrCardInvalid;
    }
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > CARD_BLOCKS * CARD_BLOCK_BYTES ||
        offset > st.st_size || bytes > st.st_size - offset) return McErrCardInvalid;
    unsigned char* contents = malloc((size_t)st.st_size);
    if (!contents) return McErrCardInvalid;
    FILE* file = fopen(path, "rb");
    long result = McErrCardInvalid;
    if (!file) cardLog("cannot open", path);
    if (file) {
        int ok = fread(contents, 1, (size_t)st.st_size, file) == (size_t)st.st_size;
        if (fclose(file) != 0) ok = 0;
        if (ok) {
            if (writing) {
                memcpy(contents + offset, data, (size_t)bytes);
                result = storeFile(path, contents, (size_t)st.st_size);
            } else {
                memcpy(data, contents + offset, (size_t)bytes);
                result = McErrNone;
            }
        }
    }
    free(contents);
    return result;
}

static long complete(long command, long result)
{
    ioState = CARD_IO_RUNNING;
    completedCommand = command;
    completedResult = result;
    return 1;
}

void MemCardClose(void) { openPath[0] = 0; openMode = 0; }
void MemCardInit(long unused)
{
    (void)unused;
    started = ioState = 0;
    newCard[0] = newCard[1] = 1;
    MemCardClose();
    cardRoot[0] = 0;
    const char* override = getenv("PE2_MEMORY_CARD_DIR");
    char* allocated = NULL;
    const char* root = override ? override : (allocated = SDL_GetPrefPath("parasite-eve-ii-port", "parasite-eve-ii"));
    if (root && *root && strlen(root) < sizeof(cardRoot) && (mkdir(root, 0700) == 0 || errno == EEXIST)) strcpy(cardRoot, root);
    SDL_free(allocated);
}
void MemCardStart(void) { started = 1; }

long MemCardExist(long channel)
{
    if (!started || ioState == CARD_IO_RUNNING) return 0;
    char path[PATH_MAX];
    long result = cardDirectory(channel, path);
    if (result == McErrNone && newCard[channel == 0x10]) result = McErrNewCard;
    return complete(McFuncExist, result);
}

long MemCardAccept(long channel)
{
    if (!started || ioState == CARD_IO_RUNNING) return 0;
    char path[PATH_MAX];
    long result = cardDirectory(channel, path);
    if (result == McErrNone && newCard[channel == 0x10]) {
        result = McErrNewCard;
        newCard[channel == 0x10] = 0;
    }
    return complete(McFuncAccept, result);
}

/* File I/O has already finished, but SDK callers observe one frame of pending
 * work. An unread completion is not a busy device: directory/open operations
 * and the next request may proceed, as the retail presence-polling UI expects. */
void peMemoryCardAdvanceIo(void)
{
    if (ioState == CARD_IO_RUNNING) ioState = CARD_IO_COMPLETE;
}

long MemCardSync(long mode, long* command, long* result)
{
    if (ioState == CARD_IO_IDLE) return -1;
    if (ioState == CARD_IO_RUNNING && mode != 0) return 0;
    if (command) *command = completedCommand;
    if (result) *result = completedResult;
    ioState = CARD_IO_IDLE;
    return 1;
}

long MemCardOpen(long channel, char* name, long mode)
{
    if (ioState == CARD_IO_RUNNING || openPath[0]) return -1;
    char path[PATH_MAX];
    long result = filePath(channel, name, path);
    if (result != McErrNone) return result;
    if (mode != CARD_READ && mode != CARD_WRITE) {
        fprintf(stderr, "[memory card] unknown open mode %ld for %s\n", mode, path);
        return McErrCardInvalid;
    }
    struct stat st;
    if (lstat(path, &st) != 0) return errno == ENOENT ? McErrFileNotExist : McErrCardInvalid;
    if (!S_ISREG(st.st_mode)) return McErrCardInvalid;
    strcpy(openPath, path);
    openMode = mode;
    return McErrNone;
}

long MemCardCreateFile(long channel, char* name, long blocks)
{
    if (ioState == CARD_IO_RUNNING) return -1;
    char path[PATH_MAX];
    long result = filePath(channel, name, path);
    if (result != McErrNone) return result;
    struct stat st;
    if (lstat(path, &st) == 0) return McErrAlreadyExist;
    if (errno != ENOENT) return McErrCardInvalid;
    CardFile files[CARD_BLOCKS];
    int count, used;
    result = listFiles(channel, files, &count, &used);
    if (result != McErrNone) return result;
    if (blocks < 1 || blocks > CARD_BLOCKS) return McErrCardInvalid;
    if (used + blocks > CARD_BLOCKS) return McErrBlockFull;
    unsigned char* data = calloc((size_t)blocks, CARD_BLOCK_BYTES);
    if (!data) return McErrCardInvalid;
    result = storeFile(path, data, (size_t)blocks * CARD_BLOCK_BYTES);
    free(data);
    return result;
}

static int validTransfer(const void* data, long offset, long bytes)
{
    return data && offset >= 0 && bytes > 0 && offset % CARD_SECTOR_BYTES == 0 && bytes % CARD_SECTOR_BYTES == 0;
}

long MemCardReadData(unsigned long* data, long offset, long bytes)
{
    if (!started || ioState == CARD_IO_RUNNING || !openPath[0] || openMode != CARD_READ || !validTransfer(data, offset, bytes)) return 0;
    return complete(McFuncReadData, transfer(openPath, data, offset, bytes, 0));
}
long MemCardWriteData(unsigned long* data, long offset, long bytes)
{
    if (!started || ioState == CARD_IO_RUNNING || !openPath[0] || openMode != CARD_WRITE || !validTransfer(data, offset, bytes)) return 0;
    return complete(McFuncWriteData, transfer(openPath, data, offset, bytes, 1));
}
long MemCardWriteFile(long channel, char* name, unsigned long* data, long offset, long bytes)
{
    if (!started || ioState == CARD_IO_RUNNING || openPath[0] || !validTransfer(data, offset, bytes)) return 0;
    char path[PATH_MAX];
    long result = filePath(channel, name, path);
    if (result == McErrNone) result = transfer(path, data, offset, bytes, 1);
    return complete(McFuncWriteFile, result);
}

long peMemoryCardGetDirentry(long channel, const char* pattern, PeMemoryCardDirectoryEntry* entries, long* count, long offset, long maximum)
{
    if (ioState == CARD_IO_RUNNING) return -1;
    if (!pattern || !count || offset < 0 || maximum < 0 || (maximum && !entries)) return McErrCardInvalid;
    *count = 0;
    CardFile files[CARD_BLOCKS];
    int number, blocks;
    long result = listFiles(channel, files, &number, &blocks);
    if (result != McErrNone) return result;
    int firstBlock = 1, matched = 0;
    for (int i = 0; i < number; ++i) {
        if (fnmatch(pattern, files[i].name, 0) == 0 && matched++ >= offset && *count < maximum) {
            PeMemoryCardDirectoryEntry* entry = &entries[(*count)++];
            memset(entry, 0, sizeof(*entry));
            strncpy(entry->name, files[i].name, sizeof(entry->name));
            entry->attr = 0x51;
            entry->size = (int32_t)files[i].bytes;
            entry->head = firstBlock * (CARD_BLOCK_BYTES / CARD_SECTOR_BYTES);
        }
        firstBlock += files[i].blocks;
    }
    return McErrNone;
}

long MemCardFormat(long channel)
{
    if (ioState == CARD_IO_RUNNING) return -1;
    CardFile files[CARD_BLOCKS];
    int count, blocks;
    long result = listFiles(channel, files, &count, &blocks);
    if (result != McErrNone) return result;
    for (int i = 0; i < count; ++i) {
        char path[PATH_MAX];
        result = filePath(channel, files[i].name, path);
        if (result != McErrNone || unlink(path) != 0) return McErrCardInvalid;
        if (!strcmp(path, openPath)) MemCardClose();
    }
    return McErrNone;
}
