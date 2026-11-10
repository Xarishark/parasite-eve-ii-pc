#include "../src/actors/actor_403600/grid_packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
int main(void)
{
    _Actor403600GridQuad quad;P_TAG head;memset(&head,0,sizeof head);memset(&quad,0xa5,sizeof quad);
    CHECK(sizeof quad==sizeof(POLY_FT4));
    CHECK(offsetof(_Actor403600GridQuad,vertex0)==offsetof(POLY_FT4,x0));
    CHECK(offsetof(_Actor403600GridQuad,vertex1)==offsetof(POLY_FT4,x1));
    CHECK(offsetof(_Actor403600GridQuad,vertex2)==offsetof(POLY_FT4,x2));
    CHECK(offsetof(_Actor403600GridQuad,vertex3)==offsetof(POLY_FT4,x3));
    setlen(&quad,9);quad.code=0x2d;
    quad.vertex0.x=-160;quad.vertex0.y=80;quad.vertex0.u=64;quad.vertex0.v=120;
    CHECK(getlen(&quad)==9 && getcode(&quad)==0x2d);
    termPrim(&head);addPrim(&head,&quad);
    CHECK(nextPrim(&head)==&quad && isendprim(&quad));
    CHECK(getlen(&quad)==9 && quad.vertex0.x==-160 && quad.vertex0.v==120);
    puts("Water grid: host tag length, command and all corner offsets match POLY_FT4");return 0;
}
