//
//  z502Interface.c
//  CS502
//
//  Created by Huanzhen Zhang on 11/26/19.
//  Copyright © 2019 Huanzhen Zhang. All rights reserved.
//

#include             "z502Interface.h"
#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             <stdlib.h>
#include             <ctype.h>
#include             <stdbool.h>
//#include             "z502Disk.h"
long osGetCurrentContext(){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode = Z502GetCurrentContext;
    mmio.Field1 = 0;
    mmio.Field2 = 0;
    mmio.Field3 = 0;
    mmio.Field4=0;
    MEM_READ(Z502Context, &mmio);
    return mmio.Field1;
}

long Get_CurrentTime(){
    long CurrentTime;
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502ReturnValue;
    mmio.Field1=mmio.Field2=mmio.Field3=0;
    MEM_READ(Z502Clock, &mmio);
    CurrentTime=mmio.Field1;
    return CurrentTime;
}


long osGetNumofProcessor(){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode = Z502GetProcessorNumber;
    mmio.Field1 = 0;
    mmio.Field2 = 0;
    mmio.Field3 = 0;
    mmio.Field4=0;
    MEM_READ(Z502Processor, &mmio);
    return mmio.Field1;
}

void osStartContext(long ContextID){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502GetProcessorNumber;
    mmio.Field1=mmio.Field2=mmio.Field3=mmio.Field4=0;
    MEM_READ(Z502Processor, &mmio);
    if(mmio.Field1==1){
        mmio.Mode=Z502StartContext;
        mmio.Field1=ContextID;
        mmio.Field2=START_NEW_CONTEXT_AND_SUSPEND;
        mmio.Field3=mmio.Field4=0;
        MEM_WRITE(Z502Context, &mmio);
    }
}

long osInitailizeContext(long Address,long pagetable){
    INT32 ContextID;
    MEMORY_MAPPED_IO mmio;
    mmio.Mode = Z502InitializeContext;
    mmio.Field1 = 0;
    mmio.Field2 = Address;
    mmio.Field3 = pagetable;
    mmio.Field4=0;
    MEM_WRITE(Z502Context, &mmio);
    return mmio.Field1;
}

void osCauseZ502Idle(){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode = Z502Action;
    mmio.Field1 = mmio.Field2 = mmio.Field3 = 0;
    MEM_WRITE(Z502Idle, &mmio);
}

void osStartTimer(long Time){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502Start;
    mmio.Field1=Time;
    mmio.Field2=mmio.Field3=mmio.Field4=0;
    MEM_WRITE(Z502Timer, &mmio);
}

void HaltZ502(){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502Action;
    mmio.Field1=mmio.Field2=mmio.Field3=0;
    MEM_WRITE(Z502Halt, &mmio);
}
