/* Exercise the production scene forwarder through both real dispatcher hops. */
#include "../src/shared/scene/scene_broadcast.inc.c"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static TaskMessageWord expected;
static int calls, isCommand;
static s32 childMessage(Task* task, s32 id, TaskMessageWord payload, TaskMessageWord second)
{
    (void)task;
    CHECK(id==ACTOR_COMMAND_MESSAGE_APPLY && second==0 && payload==expected);
    if (isCommand) {
        const ActorCommand* command=(const void*)payload;
        CHECK(command->command==0x1234);
    }
    ++calls; return 99;
}
void Psyz_PadsOverride(int p,char* f,int n){(void)p;(void)f;(void)n;}
int main(void)
{
    Task scene={0}, actors[3]={0}; Enemy enemies[3]={0};
    TaskMessageEntry sceneMessages[]={
        {SCENE_MESSAGE_BROADCAST_TO_ACTORS,(TaskMessageHandler)_sceneBroadcastToPlacedActors},
        {TASK_MESSAGE_TABLE_END,NULL}};
    TaskMessageEntry childMessages[]={
        {ACTOR_COMMAND_MESSAGE_APPLY,(TaskMessageHandler)childMessage},
        {TASK_MESSAGE_TABLE_END,NULL}};
    scene.msgTable=sceneMessages; scene.firstChild=actors;
    for(int i=0;i<3;++i){
        actors[i].nextSibling=&actors[(i+1)%3]; actors[i].spawnArg2.pointer=&enemies[i];
        actors[i].msgTable=childMessages;
        enemies[i].workType=SCENE_PLACED_ACTOR_BANK<<SCENE_ACTOR_BANK_SHIFT;
    }
    enemies[1].workType=(SCENE_PLACED_ACTOR_BANK+1)<<SCENE_ACTOR_BANK_SHIFT;
    ActorCommand command={0}; command.command=0x1234;
    if(sizeof(void*)>4) CHECK((uintptr_t)&command>UINT32_MAX);
    expected=(TaskMessageWord)&command; isCommand=1;
    CHECK(TASK_MESSAGE_DISPATCH_POINTER(&scene,SCENE_MESSAGE_BROADCAST_TO_ACTORS,&command,ACTOR_COMMAND_MESSAGE_APPLY)==0);
    CHECK(calls==2);
    isCommand=0; calls=0; expected=-7;
    CHECK(taskMessageDispatch(&scene,SCENE_MESSAGE_BROADCAST_TO_ACTORS,expected,ACTOR_COMMAND_MESSAGE_APPLY)==0);
    CHECK(calls==2);
    scene.firstChild=NULL; calls=0;
    CHECK(taskMessageDispatch(&scene,SCENE_MESSAGE_BROADCAST_TO_ACTORS,expected,ACTOR_COMMAND_MESSAGE_APPLY)==0 && calls==0);
    puts("Scene broadcast: native pointer and signed value survive both dispatcher hops; bank filter and empty scene OK");
    return 0;
}
