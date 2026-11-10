/* Offline artwork input decoder using the exact game VLC/MDEC implementation.
 * Outputs local disc-derived data. No renderer, window or game-state writes. */
#include "port/background_pack.h"
#include <psyz.h>
#include <libpress.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
void Psyz_PadsOverride(int port,char* frame,int length) {(void)port;(void)frame;(void)length;}
/* DecDCTvlcBuild emits the game's 0x11000-byte VLC2 table, larger than
 * the SDK DECDCTTAB intended for the earlier VLC decoder. */
static u_short table[0x11000 / sizeof(u_short)];
static uint16_t columns[320*240],rows[320*240];
int main(int argc,char** argv)
{
    if(argc!=3) {fprintf(stderr,"usage: %s input.bs output.rgb555\n",argv[0]);return 2;}
    FILE* input=fopen(argv[1],"rb");if(!input)return 2;
    if(fseek(input,0,SEEK_END))return 2;
    long bytes=ftell(input);if(bytes<12 || bytes>1024*1024 || fseek(input,0,SEEK_SET))return 2;
    unsigned char* data=calloc((size_t)bytes+64,1);if(!data)return 2;
    if(fread(data,1,(size_t)bytes,input)!=(size_t)bytes)return 2;fclose(input);
    unsigned words=(unsigned)data[0]|(unsigned)data[1]<<8;
    if(!words || data[2]!=0 || data[3]!=0x38 || (data[6]!=2 && data[6]!=3) || data[7])return 2;
    unsigned char* runlevel=calloc((size_t)words*4+64,1);if(!runlevel)return 2;
    DecDCTReset(0);DecDCTvlcBuild(table);DecDCTvlcSize2(0);
    if(DecDCTvlc2((u_long*)data,(u_long*)runlevel,table)!=0)return 3;
    DecDCTin((u_long*)runlevel,0);
    if(DecDCTinSync(0)!=0)return 3;
    for(unsigned column=0;column<20;column++) {
        DecDCTout((u_long*)(columns+column*16*240),16*240/2);
        if(DecDCToutSync(0)!=0)return 3;
    }
    uint64_t key=peBackgroundCopyNative(rows,columns,1);
    FILE* output=fopen(argv[2],"wb");if(!output)return 2;
    for(unsigned i=0;i<320*240;i++) {fputc(rows[i]&255,output);fputc(rows[i]>>8,output);}
    int failed=ferror(output);if(fclose(output))failed=1;
    if(failed) {remove(argv[2]);return 2;}
    printf("%016" PRIx64 "\n",key);
    free(data);free(runlevel);return 0;
}
