//
//  z502ProcessManagement.h
//  CS502
//
//  Created by Huanzhen Zhang on 11/26/19.
//  Copyright © 2019 Huanzhen Zhang. All rights reserved.
//

#ifndef z502ProcessManagement_h
#define z502ProcessManagement_h

#include             <stdio.h>
#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             <stdlib.h>
#include             <ctype.h>
#include             <stdbool.h>
#include             "z502Interface.h"

typedef struct Inode_Info{
    unsigned char Inode;
    unsigned char Name[7];
    unsigned long File_Description;
    unsigned long Creation_Time;
    unsigned short Index_Location;
    unsigned short File_Size;
    unsigned short DiskID;
    unsigned short SectorID;
}Inodeinfo;

typedef struct Process_Control_Block{
    char processName[50];
    long currentContext;
    short DiskID;
    short SectorID;
    long *Pagetable;
    INT32 processID;
    INT32 processStatus;
    INT32 processPriority;
    INT32 processWakeTime;
    INT32 TargetID;
    INT32 SourceID;
    Header *OpenDirectory;
    Inodeinfo *FileInode[MAX_NUMBER_INODES];
}PCB;

#define         DO_LOCK                         1
#define         DO_UNLOCK                       0
#define         SUSPEND_UNTIL_LOCKED            TRUE
#define         DO_NOT_SUSPEND                  FALSE
#define         TimerLockAddress                1
#define         PCBLockAddress                  2
#define         ReadyLockAddress                3
#define         DiskLockAddress                 5
#define         MOST_FAVORABLE_PRIORITY         1
#define         FAVORABLE_PRIORITY             10
#define         NORMAL_PRIORITY                20
#define         LEAST_FAVORABLE_PRIORITY       30
#define         LEGAL_MESSAGE_LENGTH           (INT16)64

#define         Suspended  1
#define         NotSuspended  0

#endif /* z502ProcessManagement_h */
