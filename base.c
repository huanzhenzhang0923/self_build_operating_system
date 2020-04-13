/************************************************************************
 
 This code forms the base of the operating system you will
 build.  It has only the barest rudiments of what you will
 eventually construct; yet it contains the interfaces that
 allow test.c and z502.c to be successfully built together.
 
 Revision History:
 1.0 August 1990
 1.1 December 1990: Portability attempted.
 1.3 July     1992: More Portability enhancements.
 Add call to SampleCode.
 1.4 December 1992: Limit (temporarily) printout in
 interrupt handler.  More portability.
 2.0 January  2000: A number of small changes.
 2.1 May      2001: Bug fixes and clear STAT_VECTOR
 2.2 July     2002: Make code appropriate for undergrads.
 Default program start is in test0.
 3.0 August   2004: Modified to support memory mapped IO
 3.1 August   2004: hardware interrupt runs on separate thread
 3.11 August  2004: Support for OS level locking
 4.0  July    2013: Major portions rewritten to support multiple threads
 4.20 Jan     2015: Thread safe code - prepare for multiprocessors
 4.51 August  2018: Minor bug fixes
 4.60 February2019: Test for the ability to alloc large amounts of memory
 ************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             <stdlib.h>
#include             <ctype.h>
//#include             <stdbool.h>
//#include             "z502Disk.h"
#include             "z502Interface.h"
#include             "z502ProcessManagement.h"

//  This is a mapping of system call nmemonics with definitions

char *call_names[] = {"MemRead  ", "MemWrite ", "ReadMod  ", "GetTime  ",
    "Sleep    ", "GetPid   ", "Create   ", "TermProc ", "Suspend  ",
    "Resume   ", "ChPrior  ", "Send     ", "Receive  ", "PhyDskRd ",
    "PhyDskWrt", "DefShArea", "Format   ", "CheckDisk", "OpenDir  ",
    "OpenFile ", "CreaDir  ", "CreaFile ", "ReadFile ", "WriteFile",
    "CloseFile", "DirContnt", "DelDirect", "DelFile  " };

/************************************************************************
 INTERRUPT_HANDLER
 When the Z502 gets a hardware interrupt, it transfers control to
 this routine in the Operating System.
 NOTE WELL:  Just because the timer or the disk has interrupted, and
 therefore this code is executing, it does NOT mean the
 action you requested was successful.
 For instance, if you give the timer a NEGATIVE time - it
 doesn't know what to do.  It can only cause an interrupt
 here with an error.
 If you try to read a sector from a disk but that sector
 hasn't been written to, that disk will interrupt - the
 data isn't valid and it's telling you it was confused.
 YOU MUST READ THE ERROR STATUS ON THE INTERRUPT
 ************************************************************************/
//Here Define the global variable

int Inode=0;
int BitMap[8][NUMBER_LOGICAL_SECTORS];
int MaxSchedulePrint;
int MaxSentTime;
int PCBQueueID;
int TimerQueueID;
int DiskQueueID;
int ReadyQueueID;
int TimerSuspendQ;
int MessageQueueID;
int MessageSuspendedQ;
int TerminatedQueueID;
char ReadyQueueName[]="ReadyQueue";
char TimerQueueName[]="TimerQueue";
char PCBQueueName[]="PCBQueue";
char DiskQueueName[]="DiskQueue";
char MessageQueueName[]="MessageQueue";
char TimerSuspend[]="TimerSuspend";
char TerminatedQueueName[]="TerminatedQueueName";
char MessageSuspendedQueueName[]="MessageSusQName";


int LRUQueueID;
char LRUQueue[]="LRU";

struct {
    INT16  InUse;         // TRUE == in use, FALSE == not in use
    INT16  Pid;           // The Process holding this frame
    INT16  LogicalPage;   // The logical page in that process
    INT16  State;         // The state of the frame.
} Frame[NUMBER_PHYSICAL_PAGES];

struct {
    INT16  InUse;         // TRUE == in use, FALSE == not in use
    INT32  ProcessID;           // The Process holding this frame
    INT16  virtualPage;   // The logical page in that process
    INT16  SectorID;         // The state of the frame.
} SwapArea[500];


typedef union {
    unsigned char char_data[PGSIZE];
    unsigned short short_data[PGSIZE/sizeof(short)];
    unsigned int int_data[PGSIZE / sizeof(int)];
    unsigned long long_data[PGSIZE/sizeof(long)];
} DISK_DATA;



INT32 CurrentProcessID;


PCB *CurrentPCB;
PCB *newPCB;
PCB *pcbHead;
PCB *pcbEnd;

Header *convertInodeinfotoHeader(Inodeinfo *inodeinfo){
    Header *head=(Header*)malloc(sizeof(Header));
    head->Creation_Time=inodeinfo->Creation_Time;
    head->File_Description=inodeinfo->File_Description;
    head->File_Size=inodeinfo->File_Size;
    head->Index_Location=inodeinfo->Index_Location;
    head->Inode=inodeinfo->Inode;
    strcpy(head->Name,inodeinfo->Name);
    return head;
}



Inodeinfo *convertHeadertoInodeinfo(Header *head){
    Inodeinfo *inodeinfo=(Inodeinfo*)malloc(sizeof(Inodeinfo));
    inodeinfo->Creation_Time=head->Creation_Time;
    inodeinfo->File_Description=head->File_Description;
    inodeinfo->File_Size=head->File_Size;
    inodeinfo->Index_Location=head->Index_Location;
    inodeinfo->Inode=head->Inode;
    strcpy(inodeinfo->Name,head->Name);
    return inodeinfo;
}


long osGetNumTerminatedProcess(){
    int i=0;
    while((int)QWalk(TerminatedQueueID,i)!=-1){
        i++;
    }
    return i;
}

void Lock(INT32 address){
    INT32 ReturnValue;
    Z502MemoryReadModify(MEMORY_INTERLOCK_BASE+address, DO_LOCK, SUSPEND_UNTIL_LOCKED, &ReturnValue);
}

void UnLock(INT32 address){
    INT32 ReturnValue;
    Z502MemoryReadModify(MEMORY_INTERLOCK_BASE+address, DO_UNLOCK, SUSPEND_UNTIL_LOCKED, &ReturnValue);
}

void TryLock(INT32 address){
    INT32 ReturnValue;
    Z502MemoryReadModify(MEMORY_INTERLOCK_BASE+address, DO_LOCK, DO_NOT_SUSPEND, &ReturnValue);
}


INT32 osGetCurrentProcessID(){
    INT32 CurrentProcessID;
    long CurrentContextID;
    PCB *pcb=QWalk(PCBQueueID, 0);
    CurrentProcessID=pcb->processID;
    CurrentContextID=osGetCurrentContext();
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *pcb=QWalk(PCBQueueID, i);
        if(pcb->currentContext==CurrentContextID){
            CurrentProcessID=pcb->processID;
        }
        i++;
    }
    return CurrentProcessID;
}

int osGetNumProcessInTimer(){
    Lock(TimerLockAddress);
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        i++;
    }
    UnLock(TimerLockAddress);
    return i;
}

int osGetNumProcessInReady(){
    Lock(ReadyLockAddress);
    int i=0;
    while((int)QWalk(ReadyQueueID,i)!=-1){
        i++;
    }
    UnLock(ReadyLockAddress);
    return i;
}

int osGetNumSuspendedProcessInTimer(){
    int i=0;
    while((int)QWalk(TimerSuspendQ,i)!=-1){
        i++;
    }
    return i;
}


int osGetNumofRunningProcess(){
    int returnvalue=1;
    if(osGetNumofProcessor()==1){
        returnvalue=1;
    }
    return returnvalue;
}


int osGetNumSuspendedProcessByProcess(){
    int num=0;
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *pcb=QWalk(PCBQueueID, i);
        if(pcb->processStatus==Suspended){
            num++;
        }
        i++;
    }
    return num;
}


void SchedulePrinter(char Action[]){
    if(GetNumberOfSchedulePrints()<MaxSchedulePrint){
        SP_INPUT_DATA *input=(SP_INPUT_DATA*) malloc(sizeof(SP_INPUT_DATA));
        strcpy(input->TargetAction,Action);
        input->CurrentlyRunningPID=osGetCurrentProcessID();
        input->TargetPID=0;
        input->NumberOfRunningProcesses=1;
        input->RunningProcessPIDs[0]=osGetCurrentProcessID();
        
        Lock(ReadyLockAddress);
        input->NumberOfReadyProcesses=osGetNumProcessInReady();
        for(int i=0;i<input->NumberOfReadyProcesses;i++){
            PCB *pcb=QWalk(ReadyQueueID, i);
            input->ReadyProcessPIDs[i]=pcb->processID;
        }
        UnLock(ReadyLockAddress);
        input->NumberOfProcSuspendedProcesses=osGetNumSuspendedProcessByProcess();
        int i=0;
        int j=0;
        while((int)QWalk(PCBQueueID, i)!=-1){
            PCB *pcb=QWalk(PCBQueueID, i);
            if(pcb->processStatus==Suspended){
                input->ProcSuspendedProcessPIDs[j]=pcb->processID;
                j++;
            }
            i++;
        }
        
        input->NumberOfMessageSuspendedProcesses=0;
        input->MessageSuspendedProcessPIDs;
        
        input->NumberOfTimerSuspendedProcesses=osGetNumSuspendedProcessInTimer();
        for(int i=0;i<input->NumberOfTimerSuspendedProcesses;i++){
            int *tsus=QWalk(TimerSuspendQ, i);
            input->TimerSuspendedProcessPIDs[i]=*tsus;
        }
        
        input->NumberOfDiskSuspendedProcesses=0;
        input->DiskSuspendedProcessPIDs;
        
        input->NumberOfTerminatedProcesses=osGetNumTerminatedProcess();
        for(int i=0;i<input->NumberOfTerminatedProcesses;i++){
            int *TID=QWalk(TerminatedQueueID, i);
            input->TerminatedProcessPIDs[i]=*TID;
        }
        SPPrintLine(input);
    }
    else{
        return;
    }
}


//This function assumes that the pcb already exist
PCB *GetProcessByID(INT32 ProcessID){
    Lock(PCBLockAddress);
    PCB *pcb=QWalk(PCBQueueID, 0);
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *currentpcb=QWalk(PCBQueueID, i);
        if(currentpcb->processID==ProcessID){
            pcb=currentpcb;
        }
        i++;
    }
    UnLock(PCBLockAddress);
    return pcb;
}

bool isProcessIDExist(INT32 ProcessID){
    Lock(PCBLockAddress);
    if(ProcessID==-1){
        return true;
    }
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *pcb=QWalk(PCBQueueID, i);
        if(pcb->processID==ProcessID){
            return true;
        }
        i++;
    }
    UnLock(PCBLockAddress);
    return false;
}

int Get_Num_Process(){
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        i++;
    }
    return i;
}

bool ReadyQueueisEmpty(){
    if((int)QNextItemInfo(ReadyQueueID) == -1){
        return true;
    }
    return false;
}

void wasteTime(){
}

void osDispatcher() {
    while(ReadyQueueisEmpty()==true) {
        CALL(wasteTime());
    }
    PCB *ReadyFrontPCB=(PCB *)QRemoveHead(ReadyQueueID);
    osStartContext(ReadyFrontPCB->currentContext);
    osCauseZ502Idle();
    char stateinput[10]="Dispacher";
    SchedulePrinter(stateinput);
}

void AddtoReadyQueue(PCB *pcb){
    Lock(ReadyLockAddress);
    QInsert(ReadyQueueID, pcb->processPriority,pcb);
    UnLock(ReadyLockAddress);
};

void AddtoTimerQueue(PCB *pcb){
    Lock(TimerLockAddress);
    QInsert(TimerQueueID, pcb->processWakeTime,pcb);
    UnLock(TimerLockAddress);
};


PCB *RemovefromTimerQueue(){
    Lock(TimerLockAddress);
    PCB *pcb=(PCB *)QRemoveHead(TimerQueueID);
    UnLock(TimerLockAddress);
    return pcb;
}

void RemoveGivenPCBFromTimerQueue(PCB *pcb){
    QRemoveItem(TimerQueueID,pcb);
}

void RemovefromReadyQueue(PCB *pcb){
    QRemoveItem(ReadyQueueID, pcb);
}

void RemoveDiskQueueProcess(PCB *pcb){
    QRemoveItem(DiskQueueID, pcb);
}

void Process_Sleep(long Time_Sleep){
    INT32 CurrentProcessID=osGetCurrentProcessID();
    PCB *currentpcb=GetProcessByID(CurrentProcessID);
    INT32 currenttime=Get_CurrentTime();
    currentpcb->processWakeTime=currenttime+(INT32)Time_Sleep;
    Lock(TimerQueueID);
    if((int)QNextItemInfo(TimerQueueID)==-1){
        Lock(TimerLockAddress);
        AddtoTimerQueue(currentpcb);
        UnLock(TimerLockAddress);
        osStartTimer(Time_Sleep);
    }
    else{
        Lock(TimerLockAddress);
        PCB *pcb=(PCB *)QNextItemInfo(TimerQueueID);
        UnLock(TimerLockAddress);
        if(pcb->processWakeTime>currenttime+(INT32)Time_Sleep){
            Lock(TimerLockAddress);
            AddtoTimerQueue(currentpcb);
            UnLock(TimerLockAddress);
            osStartTimer(Time_Sleep);
        }
        else{
            Lock(TimerLockAddress);
            AddtoTimerQueue(currentpcb);
            UnLock(TimerLockAddress);
        }
    }
    osDispatcher();
}



long osGet_Process_ID(char ProcessName[30],long *ProcessID){
    if(strcmp(ProcessName, "")==0){
        long CurrentContextID;
        CurrentContextID=osGetCurrentContext();
        PCB *pcb=QWalk(PCBQueueID, 0);
        int j=0;
        while((int)QWalk(PCBQueueID,j)!=-1){
            pcb=QWalk(PCBQueueID, j);
            if(pcb->currentContext==CurrentContextID){
                *ProcessID=pcb->processID;
                return ERR_SUCCESS;
            }
            j++;
        }
    }
    else{
        int i=0;
        while((int)QWalk(PCBQueueID,i)!=-1){
            PCB *pcb=QWalk(PCBQueueID, i);
            if(strcmp(pcb->processName,ProcessName)==0){
                *ProcessID=pcb->processID;
                return ERR_SUCCESS;
            }
            i++;
        }
    }
    return ERR_BAD_PARAM;
}


void RemovePCBInDiskQueue(int DiskID){
    Lock(DiskLockAddress);
    int i=0;
    while((int)(QWalk(DiskQueueID, i))!=-1){
        PCB *pcb=QWalk(DiskQueueID, i);
//        pcb=QRemoveItem(DiskQueueID, pcb);
//        AddtoReadyQueue(pcb);
//        break;
        if(pcb->DiskID==DiskID){
            Lock(DiskLockAddress);
            pcb=QRemoveItem(DiskQueueID, pcb);
            UnLock(DiskLockAddress);
            AddtoReadyQueue(pcb);
            break;
        }
        i++;
    }
    UnLock(DiskLockAddress);
}



void InterruptHandler(void) {
    INT32 DeviceID;
    INT32 Status;
    MEMORY_MAPPED_IO mmio;       // Enables communication with hardware
    
    static BOOL  remove_this_from_your_interrupt_code = TRUE; /** TEMP **/
    static INT32 how_many_interrupt_entries = 0;              /** TEMP **/
    
    // Get cause of interrupt
    mmio.Mode = Z502GetInterruptInfo;
    mmio.Field1 = mmio.Field2 = mmio.Field3 = mmio.Field4 = 0;
    MEM_READ(Z502InterruptDevice, &mmio);
    DeviceID = mmio.Field1;
    Status = mmio.Field2;
    switch (DeviceID) {
        case TIMER_INTERRUPT:
        {
            Lock(TimerLockAddress);
            if((int)QWalk(TimerQueueID, 0)!=-1){
                long CurrentTime=Get_CurrentTime();
                PCB *FirstPCBTimer=(PCB *)QWalk(TimerQueueID, 0);
//                if the currenttime is greater than the current process wake time then we need the move the process from pcb and add to readyqueue
                if(FirstPCBTimer->processWakeTime<CurrentTime){
                    PCB *PCBDeleteFromTimer=RemovefromTimerQueue();
                    Lock(ReadyLockAddress);
                    AddtoReadyQueue(PCBDeleteFromTimer);
                    UnLock(ReadyLockAddress);
                    if((int)QWalk(TimerQueueID, 0)!=-1){
                        PCB *FirstPCB=QWalk(TimerQueueID, 0);
                        INT32 SetTime=FirstPCB->processWakeTime-(INT32)CurrentTime;
                        osStartTimer(SetTime);
                    }
                }
                else{
//                    we set a new time for the timer
                    INT32 SetTime=FirstPCBTimer->processWakeTime-CurrentTime;
                    osStartTimer(SetTime);
                }
            }
            UnLock(TimerLockAddress);
        }
            break;
        case DISK_INTERRUPT_DISK0:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK0-5);
            break;
        case DISK_INTERRUPT_DISK1:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK1-5);
            break;
            
        case DISK_INTERRUPT_DISK2:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK2-5);
            break;
        
        case DISK_INTERRUPT_DISK3:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK3-5);
            break;
        
        case DISK_INTERRUPT_DISK4:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK4-5);
            break;
        
        case DISK_INTERRUPT_DISK5:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK5-5);
            break;
        
        case DISK_INTERRUPT_DISK6:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK6-5);
            break;
        
        case DISK_INTERRUPT_DISK7:
            RemovePCBInDiskQueue(DISK_INTERRUPT_DISK7-5);
            break;
        default:
            break;
    }
    if (mmio.Field4 != ERR_SUCCESS) {
        aprintf( "The InterruptDevice call in the InterruptHandler has failed.\n");
        aprintf("The DeviceId and Status that were returned are not valid.\n");
    }
    // HAVE YOU CHECKED THAT THE INTERRUPTING DEVICE FINISHED WITHOUT ERROR?
    
    /** REMOVE THESE SIX LINES **/
    how_many_interrupt_entries++; /** TEMP **/
    if (remove_this_from_your_interrupt_code && (how_many_interrupt_entries < 10)) {
        aprintf("InterruptHandler: Found device ID %d with status %d\n",
                DeviceID, Status);
    }
    
}           // End of InterruptHandler

/************************************************************************
 FAULT_HANDLER
 The beginning of the OS502.  Used to receive hardware faults.
 ************************************************************************/


void osWriteToDisk(INT16 DiskID,INT16 SectorID,char MemoryBuffer[16]){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502DiskWrite;
    mmio.Field1=DiskID;
    mmio.Field2=SectorID;
    mmio.Field3=(long)MemoryBuffer;
    mmio.Field4=0;
    MEM_WRITE(Z502Disk, &mmio);
    
    mmio.Field2 = DEVICE_IN_USE;
    while (mmio.Field2 != DEVICE_FREE) {
        mmio.Mode = Z502Status;
        mmio.Field1 = DiskID;
        mmio.Field2 = mmio.Field3 = 0;
        MEM_READ(Z502Disk, &mmio);
    }
    
    INT32 CurrentProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(CurrentProcessID);
    QInsertOnTail(DiskQueueID, pcb);
//    osDispatcher();
    
}

void osReadOnDisk(INT16 DiskID,INT16 SectorID,char MemoryBuffer[16]){
    MEMORY_MAPPED_IO mmio;
    mmio.Mode=Z502DiskRead;
    mmio.Field1=DiskID;
    mmio.Field2=SectorID;
    mmio.Field3=(long)MemoryBuffer;
    mmio.Field4=0;
    MEM_WRITE(Z502Disk, &mmio);

    mmio.Field2 = DEVICE_IN_USE;
    while (mmio.Field2 != DEVICE_FREE) {
        mmio.Mode = Z502Status;
        mmio.Field1 = DiskID;
        mmio.Field2 = mmio.Field3 = 0;
        MEM_READ(Z502Disk, &mmio);
    }
    
    INT32 CurrentProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(CurrentProcessID);
    QInsertOnTail(DiskQueueID, pcb);
//    osDispatcher();
}

void osCheckDisk(long DiskID,long *Result){
    MEMORY_MAPPED_IO mmio;
    if(DiskID>MAX_NUMBER_OF_DISKS || DiskID<0){
        *Result=ERR_BAD_PARAM;;
    }
    else{
        mmio.Mode=Z502CheckDisk;
        mmio.Field1=DiskID;
        mmio.Field2=mmio.Field3=mmio.Field4=0;
        MEM_WRITE(Z502Disk, &mmio);
        *Result=mmio.Field4;
    }
}




int getPhysicalFrame(){
    for(int i=0;i<NUMBER_PHYSICAL_PAGES;i++){
        if(Frame[i].InUse==FALSE){
            return i;
        }
    }
    return -1;
}


//The Page Selection Algorithms is based on second-chance algorithms
int getVictimFrames(int processID){
    int victimframes=-1;
    while(true){
        for(int i=0;i<NUMBER_PHYSICAL_PAGES;i++){
            if(Frame[i].Pid==processID){
                if((Frame[i].State&FRAME_REFERENCED)==0){
                    victimframes=i;
                    break;
                }
                else{
                    Frame[i].State=Frame[i].State-1;
                }
            }
        }
        if(victimframes!=-1){
            break;
        }
    }
    return victimframes;
}

int findAvailableSector(short DiskID){
    int SectorID=0;
    for(int i=0;i<NUMBER_LOGICAL_SECTORS;i++){
        if(BitMap[DiskID][i]==0){
            SectorID=i;
            break;
        }
    }
    return SectorID;
}

bool isPageInDisk(int pageNo){
    for(int i=0;i<500;i++){
        if(SwapArea[i].virtualPage==pageNo){
            return true;
        }
    }
    return false;
}


void ReadVirtualPagetoMemory(int physicalframes,int pageNo,int pid){
    char diskwrite[16];
    char diskread[16];
    for(int i=0;i<16;i++){
        diskwrite[i]=0;
        diskread[i]=0;
    }
    for(int i=0;i<500;i++){
        if(SwapArea[i].virtualPage==pageNo){
            osReadOnDisk(0,SwapArea[i].SectorID, (long)diskread);
            Z502WritePhysicalMemory(physicalframes, diskread);
            SwapArea[i].ProcessID=-1;
            SwapArea[i].InUse=false;
            SwapArea[i].SectorID=0;
            SwapArea[i].virtualPage=-1;
            break;
        }
    }
}


void writeVictimToDisk(int victimframes,int victimVitualPageNo,int pid,int Status,short *virtualPageNo){
    char diskwrite[16];
    char diskread[16];
    for(int i=0;i<16;i++){
        diskwrite[i]=0;
        diskread[i]=0;
    }
    Z502ReadPhysicalMemory(victimframes, diskread);
    short SectorID;
    if(isPageInDisk(victimVitualPageNo)){
        for(int i=0;i<500;i++){
            if(SwapArea[i].virtualPage==victimVitualPageNo && SwapArea[i].ProcessID==pid){
                osWriteToDisk(0,SwapArea[i].SectorID, (long)diskread);
                break;
            }
        }
    }
    else{
        SectorID=findAvailableSector(0);
        for(int i=0;i<500;i++){
            if(SwapArea[i].InUse==false){
                SwapArea[i].InUse=true;
                SwapArea[i].ProcessID=pid;
                SwapArea[i].SectorID=SectorID;
                SwapArea[i].virtualPage=victimVitualPageNo;
                osWriteToDisk(0,SectorID, (long)diskread);
                BitMap[0][SectorID]=1;
                break;
            }
        }
    }
}

void resetPhysicalFrame(int physicalframes){
    Frame[physicalframes].InUse=false;
    Frame[physicalframes].Pid=-1;
    Frame[physicalframes].State=-1;
    Frame[physicalframes].LogicalPage=-1;
}



void FaultHandler(void) {
    INT32 DeviceID;
    INT32 Status;
    MEMORY_MAPPED_IO mmio;       // Enables communication with hardware
    
    static BOOL remove_this_from_your_fault_code = TRUE;
    static INT32 how_many_fault_entries = 0;
    
    // Get cause of fault
    mmio.Field1 = mmio.Field2 = mmio.Field3 = 0;
    mmio.Mode = Z502GetInterruptInfo;
    MEM_READ(Z502InterruptDevice, &mmio);
    DeviceID = mmio.Field1;
    Status   = mmio.Field2;
    switch (DeviceID) {
        case CPU_ERROR:
            break;
        case INVALID_MEMORY:
//            if(Status>=NUMBER_VIRTUAL_PAGES-1 || Status<0){
//                HaltZ502();
//            }
            mmio.Field1 = mmio.Field2 = mmio.Field3 = 0;
            mmio.Mode=Z502GetPageTable;
            MEM_READ(Z502Context, &mmio);
            short *virtualPageNo;
            virtualPageNo=(short *)mmio.Field1;
            int physicalframes=getPhysicalFrame();
            if(physicalframes==-1){
                int pid=osGetCurrentProcessID();
                int victimframes=getVictimFrames(pid);
                int victimVitualPageNo=Frame[victimframes].LogicalPage;
                writeVictimToDisk(victimframes, victimVitualPageNo, pid,Status,virtualPageNo);
                if(isPageInDisk(Status)){
                    ReadVirtualPagetoMemory(victimframes,Status,pid);
                }
                virtualPageNo[Status]=PTBL_VALID_BIT|PTBL_REFERENCED_BIT|victimframes;
                virtualPageNo[victimframes]=0;
                Frame[victimframes].InUse=TRUE;
                Frame[victimframes].Pid=osGetCurrentProcessID();
                Frame[victimframes].LogicalPage=Status;
                Frame[victimframes].State=FRAME_VALID|FRAME_REFERENCED;
            }
            else{
                virtualPageNo[Status]=PTBL_VALID_BIT|PTBL_REFERENCED_BIT|physicalframes;
                Frame[physicalframes].InUse=TRUE;
                Frame[physicalframes].Pid=osGetCurrentProcessID();
                Frame[physicalframes].LogicalPage=Status;
                Frame[physicalframes].State=FRAME_VALID|FRAME_REFERENCED;
            }
            break;
        case INVALID_PHYSICAL_MEMORY:
            break;
        case PRIVILEGED_INSTRUCTION:
            break;
        default:
            break;
    }
    // This causes a print of the first few faults - and then stops printing!
    // You can adjust this as you wish.  BUT this code as written here gives
    // an indication of what's happening but then stops printing for long tests
    // thus limiting the output.
    how_many_fault_entries++;
    if (remove_this_from_your_fault_code && (how_many_fault_entries < 10)) {
        aprintf("FaultHandler: Found device ID %d with status %d\n",
                (int) mmio.Field1, (int) mmio.Field2);
    }
    
}


PCB *Get_the_last_PCB(void){
    PCB *pcb=QWalk(PCBQueueID, 0);
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        pcb=QWalk(PCBQueueID, i);
        i++;
    }
    return pcb;
}

long osCreateProcess(INT32 processID,char processName[30],INT32 processPriority,long Address,long PageTable){
    int ProcessNumber=Get_Num_Process();
    if(processPriority<=-3 || ProcessNumber>=MAX_NUMBER_OF_USER_THREADS-1){
        return ERR_BAD_PARAM;
    }
//    Here we iterate the PCB Queue
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *pcb=QWalk(PCBQueueID, i);
        if(strcmp(pcb->processName,processName)==0){
            return ERR_BAD_PARAM;
        }
        i++;
    }
    newPCB=pcbEnd=(PCB*) malloc(sizeof(PCB));
    newPCB->processID=processID;
    strcpy(newPCB->processName,processName);
    newPCB->processPriority=processPriority;
    long ContextID=osInitailizeContext(Address, PageTable);
    newPCB->currentContext=ContextID;
    QInsertOnTail(PCBQueueID, newPCB);
    AddtoReadyQueue(newPCB);
    CurrentProcessID++;
    char stateinput[10]="CreatePro";
    SchedulePrinter(stateinput);
    return ERR_SUCCESS;
}

void RemoveFromPCB(INT32 processID, long *ReturnStatus){
    int i=0;
    while((int)QWalk(PCBQueueID,i)!=-1){
        PCB *pcb=QWalk(PCBQueueID, i);
        if(pcb->processID==processID){
            QRemoveItem(PCBQueueID, pcb);
            *ReturnStatus=ERR_SUCCESS;
        }
        i++;
    }
    if(*ReturnStatus!=ERR_SUCCESS){
        *ReturnStatus=ERR_BAD_PARAM;
    }
}

void RemoveFromTimerSuspendQ(int PID){
    int i=0;
    while((int)QWalk(TimerSuspendQ, i)!=-1){
        int *address=QWalk(TimerSuspendQ, i);
        if(*address==PID){
            QRemoveItem(TimerSuspendQ, address);
        }
        i++;
    }
}

void osResumeProcess(INT32 PID,long *Result){
    if(isProcessIDExist(PID)==false){
        *Result=ERR_BAD_PARAM;
    }
    else{
        RemoveFromTimerSuspendQ(PID);
        PCB *pcb=GetProcessByID(PID);
        if(pcb->processStatus==NotSuspended){
            *Result=ERR_BAD_PARAM;
        }
        else{
            pcb->processStatus=NotSuspended;
            AddtoReadyQueue(pcb);
            *Result=ERR_SUCCESS;
        }
    }
    char stateinput[10]="Resume";
    SchedulePrinter(stateinput);
};


void osSuspendProcess(INT32 PID,long *Result){
    if(PID==-1){
        INT32 CurrentProcessID;
        CurrentProcessID=osGetCurrentProcessID();
        PCB *pcb=GetProcessByID(CurrentProcessID);
        pcb->processStatus=Suspended;
        *Result=ERR_SUCCESS;
    }
    if(isProcessIDExist(PID)==false){
        *Result=ERR_BAD_PARAM;
    }
    else{
        PCB *pcb=GetProcessByID(PID);
        if(pcb->processStatus==Suspended){
            *Result=ERR_BAD_PARAM;
        }
        else{
            pcb->processStatus=Suspended;
            if((int)QItemExists(ReadyQueueID, pcb)!=-1){
                QRemoveItem(ReadyQueueID, pcb);
            }
            if((int)QItemExists(TimerQueueID, pcb)!=-1){
                int *pcbid=&pcb->processID;
                QInsertOnTail(TimerSuspendQ, pcbid);
                QRemoveItem(TimerQueueID, pcb);
            }
            if((int)QItemExists(DiskQueueID, pcb)!=-1){
                QRemoveItem(DiskQueueID, pcb);
            }
            *Result=ERR_SUCCESS;
        }
    }
    char stateinput[10]="Suspended";
    SchedulePrinter(stateinput);
}

//This is to examine that if the message queue has the message that the receiving process exist
void ChangePriority(INT32 PID,INT32 Priority,long *Result){
//    illegal priority
    if(Priority>LEAST_FAVORABLE_PRIORITY || Priority<MOST_FAVORABLE_PRIORITY){
        *Result=ERR_BAD_PARAM;
    }
    if(PID==-1){
        INT32 CurrentID;
        CurrentID=osGetCurrentProcessID();
        PCB *pcb=GetProcessByID(CurrentID);
        QRemoveItem(ReadyQueueID, pcb);
        pcb->processPriority=Priority;
        AddtoReadyQueue(pcb);
        *Result=ERR_SUCCESS;
    }
    if(isProcessIDExist(PID)==false){
        *Result=ERR_BAD_PARAM;
    }
    else{
        PCB *pcb=GetProcessByID(PID);
        QRemoveItem(ReadyQueueID, pcb);
        pcb->processPriority=Priority;
        AddtoReadyQueue(pcb);
        *Result=ERR_SUCCESS;
    }
    char stateinput[10]="Priority";
    SchedulePrinter(stateinput);
};

void TerminateProcess(INT32 ProcessID,long *Result){
    if(ProcessID==-1){
        INT32 PID=osGetCurrentProcessID();
        PCB *pcb=QWalk(PCBQueueID, 0);
        if(pcb->processID==PID){
            HaltZ502();
        }
        else{
            int i=0;
            while((int)QWalk(PCBQueueID,i)!=-1){
                PCB *pcb=QWalk(PCBQueueID, i);
                if(pcb->processID==PID){
                    RemoveFromPCB(pcb->processID,Result);
                    break;
                }
                i++;
            }
            i=0;
            while((int)QWalk(ReadyQueueID,i)!=-1){
                PCB *pcb=QWalk(ReadyQueueID, i);
                if(pcb->processID==PID){
                    RemovefromReadyQueue(pcb);
                    break;
                }
                i++;
            }
            i=0;
            while((int)QWalk(TimerQueueID,i)!=-1){
                PCB *pcb=QWalk(TimerQueueID, i);
                if(pcb->processID==PID){
                    RemoveGivenPCBFromTimerQueue(pcb);
                    break;
                }
                i++;
            }
            i=0;
            while((int)QWalk(DiskQueueID,i)!=-1){
                PCB *pcb=QWalk(DiskQueueID, i);
                if(pcb->processID==PID){
                    RemoveDiskQueueProcess(pcb);
                    break;
                }
                i++;
            }
            osDispatcher();
        }
        QInsertOnTail(TerminatedQueueID, &PID);
    }
    if(ProcessID==-2){
        INT32 PID=osGetCurrentProcessID();
        QInsertOnTail(TerminatedQueueID, &PID);
        HaltZ502();
    }
    else{
        QInsertOnTail(TerminatedQueueID, &ProcessID);
        RemoveFromPCB(ProcessID,Result);
    }
}

void ModifyBitMap(long DiskID,long SectorID){
    BitMap[DiskID][SectorID]=1;
}

long GetCurrentDiskID(){
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    long DiskID=pcb->DiskID;
    return DiskID;
}

long GetCurrentSectorID(){
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    long SectorID=pcb->SectorID;
    return SectorID;
}

//Find the file by name and return the Header
Header *findFileDirByName(unsigned char Name[7],unsigned int flag){
    Header *head=(Header*)malloc(sizeof(Header));
    return head;
}


void WriteHeadertoDisk(short DiskID,short SectorID,Header *fileDirhead){
    unsigned char diskwrite[16];
    diskwrite[0]=fileDirhead->Inode;
    diskwrite[1]=fileDirhead->Name[0];
    diskwrite[2]=fileDirhead->Name[1];
    diskwrite[3]=fileDirhead->Name[2];
    diskwrite[4]=fileDirhead->Name[3];
    diskwrite[5]=fileDirhead->Name[4];
    diskwrite[6]=fileDirhead->Name[5];
    diskwrite[7]=fileDirhead->Name[6];
    diskwrite[8]=fileDirhead->File_Description;
    for(int i=0;i<3;i++){
        diskwrite[9+i]=((fileDirhead->Creation_Time>>(8*i))&255);
    }
    for(int i=0;i<2;i++){
        diskwrite[12+i]=((fileDirhead->Index_Location>>(8*i))&255);
    }
    for(int i=0;i<2;i++){
        diskwrite[14+i]=((fileDirhead->File_Size>>(8*i))&255);
    }
    osWriteToDisk(DiskID, SectorID, diskwrite);
}

Header *convertReaddatatoHeader(unsigned char diskread[16]){
    Header *head=(Header*)malloc(sizeof(Header));
    head->Inode=diskread[0];
    unsigned char name[7];
    for(int i=0;i<7;i++){
        head->Name[i]=diskread[i+1];
    }
//    strcpy(head->Name,name);
    head->File_Description=diskread[8];
    head->Creation_Time=diskread[11]*256*256+diskread[10]*256+diskread[9];
    head->Index_Location=diskread[13]*256+diskread[12];
    head->File_Size=diskread[15]*256+diskread[14];
//    for(int i=0;i<16;i++){
//        printf("%c\n",diskread[i]);
//        printf("llllllll\n");
//    }
    return head;
}



short CreateNewDataBlock(short DiskID){
    short SectorID=findAvailableSector(DiskID);
    char DataBlock[16];
    osWriteToDisk(DiskID,SectorID,DataBlock);
    ModifyBitMap(DiskID, SectorID);
    return SectorID;
}



short CreateNewIndexBlock(short DiskID){
    short SectorID=findAvailableSector(DiskID);
    short IndexBlock[8]={0};
    osWriteToDisk(DiskID,SectorID,IndexBlock);
    ModifyBitMap(DiskID, SectorID);
    return SectorID;
}


int GetMaxIndex(int indexlevel){
    int maxindex=1;
    while(indexlevel>0){
        maxindex=maxindex*8;
        indexlevel--;
    }
    return maxindex;
}

// ReadFile and other methods's traversal
short fetchIndexContent(Header *file,int index){
    unsigned char Description=file->File_Description;
    int indexlevel=(Description&6)>>1;
    int maxindex=GetMaxIndex(indexlevel);
    short DiskID=GetCurrentDiskID();
    short SectorID=file->Index_Location;
    DISK_DATA *IndexBlock=(DISK_DATA *) calloc(1, sizeof(DISK_DATA));
    osReadOnDisk(DiskID,SectorID, IndexBlock);
    maxindex=maxindex/8;
    while(maxindex>0){
        short IndexBlock[8]={0};
        osReadOnDisk(DiskID,SectorID,IndexBlock);
        int varySectorID=IndexBlock[(int)index/maxindex];
        SectorID=varySectorID;
        index=index%maxindex;
        maxindex=maxindex/8;
    }
    return SectorID;
}

int isDirorFileExist(char Name[7],int flag){
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    Header *curDir=pcb->OpenDirectory;
    short DiskID=pcb->DiskID;
    unsigned char Description=curDir->File_Description;
    int size=pcb->OpenDirectory->File_Size;
    int indexlevel=(Description&6)>>1;
    int maxindex=GetMaxIndex(indexlevel);
    for(int i=0;i<size;i++){
//        DISK_DATA *read=(DISK_DATA *) calloc(1, sizeof(DISK_DATA));
        unsigned char read[16];
        short SectorID=fetchIndexContent(curDir, i);
        osReadOnDisk(DiskID, SectorID, read);
        int realflag=read[8]&1;
        if(Name[0]!=0 && Name[0]==read[1] && Name[1]==read[2] && Name[2]==read[3] && Name[3]==read[4] && Name[4]==read[5] && Name[5]==read[6] && Name[6]==read[7] && realflag==flag){
            return SectorID;
        }
    }
    return -1;
}


void expandIndexBlock(Header *head){
    short oldSectorID=head->Index_Location;
    short DiskID=GetCurrentDiskID();
    short newSectorID=findAvailableSector(DiskID);
    ModifyBitMap(DiskID, newSectorID);
    short IndexBlock[8]={0};
    IndexBlock[0]=oldSectorID;
    osWriteToDisk(DiskID,newSectorID,IndexBlock);
    short Block[8]={0};
    osReadOnDisk(DiskID, newSectorID, Block);
//    ModifyBitMap(DiskID, newSectorID);
    head->Index_Location=newSectorID;
}

Header *CreatefileorDirectory(char Name[7], long *Result,int flag){
//    find if there are duplicated directory then it is a bad parameters for name
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    short DiskID=GetCurrentDiskID();
    if(isDirorFileExist(Name,flag)!=-1){
        *Result=ERR_BAD_PARAM;
        short SectorID=isDirorFileExist(Name, flag);
        short headinfo[8];
        osReadOnDisk(DiskID, SectorID, headinfo);
        Header *head=convertReaddatatoHeader(headinfo);
        return head;
    }
//    Create the new File or Directory Header and write header into Disk
//    Set property such as time and name, Inode in advance
    Header *head=(Header*)malloc(sizeof(Header));
    head->Inode=Inode;
    strcpy(head->Name, Name);
    head->Creation_Time=Get_CurrentTime();
    Header *curDir=pcb->OpenDirectory;
    int ParentInode=curDir->Inode;
    if(flag==1){
        head->File_Description=(ParentInode<<3)+1;
    }
    else{
        head->File_Description=(ParentInode<<3)+0;
    }
    head->Index_Location=CreateNewIndexBlock(DiskID);
    head->File_Size=0;
//    let the current directory point to the new Directory or file and write both header and curdir(updated) to disk
    curDir->File_Size++;
    if(curDir->File_Size==1){
        short HeaderSectorID=curDir->Index_Location;
        ModifyBitMap(DiskID, HeaderSectorID);
        WriteHeadertoDisk(DiskID, HeaderSectorID,head);
        Inodeinfo *inodeinfo=convertHeadertoInodeinfo(head);
        inodeinfo->DiskID=DiskID;
        inodeinfo->SectorID=HeaderSectorID;
        pcb->FileInode[Inode]=inodeinfo;
        WriteHeadertoDisk(DiskID,pcb->SectorID,curDir);
    }
    else{
        int dirparentinode=(curDir->File_Description)&248;
        int Dirindexlevel=((curDir->File_Description)&6)>>1;
        int maxindex=GetMaxIndex(Dirindexlevel);
        while(curDir->File_Size>=maxindex){
            expandIndexBlock(curDir);
            Dirindexlevel++;
            maxindex=GetMaxIndex(Dirindexlevel);
        }
        curDir->File_Description=dirparentinode+(Dirindexlevel<<1)+1;
        short varySectorID=curDir->Index_Location;
        short IndexBlock[8]={0};
        osReadOnDisk(DiskID,varySectorID,IndexBlock);
        short Index=curDir->File_Size;
        maxindex=maxindex/8;
        while(maxindex>0){
            short IndexBlock[8]={0};
            osReadOnDisk(DiskID,varySectorID,IndexBlock);
            int SectorID=IndexBlock[Index/maxindex-1];
            if(SectorID==0){
                SectorID=CreateNewIndexBlock(DiskID);
                IndexBlock[Index/maxindex-1]=SectorID;
            }
            osWriteToDisk(DiskID, varySectorID, IndexBlock);
            varySectorID=SectorID;
            Index=Index%maxindex;
            maxindex=maxindex/8;
            Dirindexlevel--;
        }
        WriteHeadertoDisk(DiskID, varySectorID, head);
        ModifyBitMap(DiskID, varySectorID);
        Inodeinfo *inodeinfo=convertHeadertoInodeinfo(head);
        inodeinfo->DiskID=DiskID;
        inodeinfo->SectorID=varySectorID;
        pcb->FileInode[Inode]=inodeinfo;
        WriteHeadertoDisk(DiskID,pcb->SectorID,curDir);
    }
    Inode++;
    *Result=ERR_SUCCESS;
    return head;
}

void OpenDirectory(long DiskID,unsigned char Name[7],long *Result){
    INT32 PID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(PID);
    if(strcmp(Name, "root")==0){
        pcb->DiskID=DiskID;
        pcb->SectorID=1;
        unsigned char diskread[16];
        osReadOnDisk(DiskID, 1, diskread);
        Header *root=convertReaddatatoHeader(diskread);
        pcb->OpenDirectory=root;
        pcb->SectorID=1;
        *Result=ERR_SUCCESS;
        return;
    }
    if(DiskID==-1){
        DiskID=pcb->DiskID;
    }
    short SectorID=isDirorFileExist(Name, 1);
    Header *curDir;
    if(SectorID==-1){
        curDir=CreatefileorDirectory(Name, Result,1);
    }
    else{
        char *diskread[16];
        osReadOnDisk(DiskID, SectorID, (long)diskread);
        curDir=convertReaddatatoHeader(diskread);
    }
    pcb->OpenDirectory=curDir;
    pcb->SectorID=isDirorFileExist(Name, 1);
    *Result=ERR_SUCCESS;
}

//First lookup file name and then return the Inode,which serves as the ID of the file.
void OpenFile(char Name[7],long *Inode,long *Result){
//    Find Whether the file is already in Directory, if is not in directory,then Created it
    INT32 PID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(PID);
    Header *curOpenDir=pcb->OpenDirectory;
    short DiskID=pcb->DiskID;
    if(isDirorFileExist(Name,0)==-1){
        Header *file=CreatefileorDirectory(Name, Result,0);
        *Inode=file->Inode;
    }
    else{
        short SectorID=isDirorFileExist(Name, 0);
        short fileheadinfo[16];
        osReadOnDisk(DiskID, SectorID, fileheadinfo);
        Header *file=convertReaddatatoHeader(fileheadinfo);
        *Inode=file->Inode;
//    store fileheader to fileinode vector
        Inodeinfo *inodeinfo=convertHeadertoInodeinfo(file);
        inodeinfo->SectorID=SectorID;
        inodeinfo->DiskID=DiskID;
        pcb->FileInode[*Inode]=inodeinfo;
    }
    *Result=ERR_SUCCESS;
}



void writeRootDir(short DiskID,short SectorID){
//    Setup root directory
    Header *roothead=(Header*)malloc(sizeof(Header));
    roothead->Inode=Inode;
    strcpy(roothead->Name, "root");
    roothead->Creation_Time=Get_CurrentTime();
    roothead->Index_Location=CreateNewDataBlock(DiskID);
    roothead->File_Description=(31<<3);
//  Setup the file size initially it is empty
    roothead->File_Size=0;
//    Assign a new IndexBlock and assciate with the Directory and let the current directory point to the new Directory or file?
    // get the indexblock of parent
//    Write that header to Disk
    WriteHeadertoDisk(DiskID, SectorID, roothead);
    
    Inode++;
}

void DiskFormat(long DiskID,long *Result){
//    if there is illegal DiskID
    if(DiskID<0 || DiskID>8){
        *Result=ERR_BAD_PARAM;
    }
//    SetUp prameters for sector0 when Formating and Create Root Directory
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    pcb->DiskID=DiskID;
    pcb->SectorID=0;
    ModifyBitMap(DiskID, 0);
    Sector0 *sector0=(Sector0*)malloc(sizeof(Sector0));
    sector0->DiskID=(unsigned char)DiskID;
    sector0->RootDir_Location=1;
    ModifyBitMap(DiskID, 1);
    writeRootDir(DiskID, 1);
//    Write Sector to Disk When considering the LBT and MBT.
    DISK_DATA *Block0ToDisk=(DISK_DATA *) calloc(1, sizeof(DISK_DATA));
    Block0ToDisk->char_data[0]=DiskID;
    Block0ToDisk->char_data[1]=sector0->Bitmap_Size;
    Block0ToDisk->char_data[2]=sector0->RootDir_Size;
    Block0ToDisk->char_data[3]=sector0->Swap_Size;
    Block0ToDisk->char_data[4]=sector0->Disk_Length&0xFF;
    Block0ToDisk->char_data[5]=(sector0->Disk_Length>>8)&0xFF;
    Block0ToDisk->char_data[6]=0;
    Block0ToDisk->char_data[7]=0;
    Block0ToDisk->char_data[8]=sector0->RootDir_Location&0xFF;
    Block0ToDisk->char_data[9]=(sector0->RootDir_Location>>8)&0xFF;
    Block0ToDisk->char_data[10]=sector0->Swap_Location&0xFF;
    Block0ToDisk->char_data[11]=(sector0->Swap_Location>>8)&0xFF;
    osWriteToDisk(DiskID, 0, (long)Block0ToDisk->char_data);
    *Result=ERR_SUCCESS;
}

void ReadFile(long Inode,long Index,char ReadBuffer[PGSIZE],long *Result){
//    Get file header according to the inode
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    Inodeinfo *inodeinfo=pcb->FileInode[(int)Inode];
    Header *filehead=convertInodeinfotoHeader(inodeinfo);
//    printf("%s\n",filehead->Name);
//    printf("%d\n",inodeinfo->File_Size);
//    printf("wwwwwwww\n");
//    printf("solve it!\n");
//    Find the SectorID according to the Index and read the data block from the disk
    short SectorID=fetchIndexContent(filehead, Index);
//    char IndexBlock[16]={0};
    short DiskID=GetCurrentDiskID();
    osReadOnDisk(DiskID,SectorID,ReadBuffer);
}

void WriteFile(long Inode,long Index,char WriteBuffer[PGSIZE],long *Result){
//    Get file header according to the inode
    long DiskID=GetCurrentDiskID();
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    
    Inodeinfo *inodeinfo=pcb->FileInode[(int)Inode];
    Header *file=convertInodeinfotoHeader(inodeinfo);
    int parentinode=(file->File_Description)&248;
    int indexlevel=((file->File_Description)&6)>>1;
    int flag=(file->File_Description)&1;
    int maxindex=GetMaxIndex(indexlevel);
    if(Index==0){
        short varySectorID=file->Index_Location;
        osWriteToDisk(DiskID, varySectorID, WriteBuffer);
        return;
    }
    while(Index>=maxindex){
        expandIndexBlock(file);
        inodeinfo->Index_Location=file->Index_Location;
        indexlevel++;
        maxindex=GetMaxIndex(indexlevel);
    }
    file->File_Description=parentinode+(indexlevel<<1)+flag;
    file->File_Size++;
    inodeinfo->File_Description=parentinode+(indexlevel<<1)+flag;
    inodeinfo->File_Size++;
    short varySectorID=file->Index_Location;
    short IndexBlock[8]={0};
    osReadOnDisk(DiskID,varySectorID,IndexBlock);
    maxindex=maxindex/8;
    while(maxindex>0){
        short IndexBlock[8]={0};
        osReadOnDisk(DiskID,varySectorID,IndexBlock);
        int SectorID=IndexBlock[(int)Index/maxindex];
        if(SectorID==0){
            SectorID=CreateNewIndexBlock(DiskID);
            IndexBlock[(int)Index/maxindex]=SectorID;
        }
        osWriteToDisk(DiskID, varySectorID, IndexBlock);
        varySectorID=SectorID;
        Index=Index%maxindex;
        maxindex=maxindex/8;
        indexlevel--;
    }
    osWriteToDisk(DiskID, varySectorID, WriteBuffer);
    ModifyBitMap(DiskID, varySectorID);
    WriteHeadertoDisk(DiskID, inodeinfo->SectorID, file);
}


void CloseFile(long Inode,long *Result){
//  Save all the content and remove the header info from inode vector
    INT32 PID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(PID);
    Inodeinfo *inodeinfo=pcb->FileInode[Inode];
    Header *filehead=convertInodeinfotoHeader(inodeinfo);
    short DiskID=inodeinfo->DiskID;
    short SectorID=inodeinfo->SectorID;
    WriteHeadertoDisk(DiskID,SectorID, filehead);
    pcb->FileInode[Inode]=NULL;
    *Result=ERR_SUCCESS;
}

void DirectoryContent(long *Result){
//  Search along the cur directory and print the info of file or subdirectory
    INT32 ProcessID=osGetCurrentProcessID();
    PCB *pcb=GetProcessByID(ProcessID);
    Header *curDir=pcb->OpenDirectory;
    short DiskID=GetCurrentDiskID();
    for(int i=0;i<curDir->File_Size;i++){
        short SectorID=fetchIndexContent(curDir,i);
        char *diskread[16];
        osReadOnDisk(DiskID, SectorID, (long)diskread);
        Header *head=convertReaddatatoHeader(diskread);
        long Inode=head->Inode;
        char FileName[7];
        strcpy(FileName,head->Name);
        long CreationTime=head->Creation_Time;
        long FileSize=head->File_Size;
        int flag=(head->File_Description)&1;
        char dirorfile;
        if(flag==1){
            dirorfile='D';
        }
        else{
            dirorfile='F';
        }
        aprintf("Inode %ld, File Name %s, D/F %c, Creation Time %ld,FileSize %ld\n", Inode, FileName,dirorfile,CreationTime,FileSize);
    }
}







/************************************************************************
 SVC
 The beginning of the OS502.  Used to receive software interrupts.
 All system calls come to this point in the code and are to be
 handled by the student written code here.
 The variable do_print is designed to print out the data for the
 incoming calls, but does so only for the first ten calls.  This
 allows the user to see what's happening, but doesn't overwhelm
 with the amount of data.
 ************************************************************************/


void svc(SYSTEM_CALL_DATA *SystemCallData) {
    short call_type;
    static short do_print = 10;
    short i;
    INT32 Time;
    MEMORY_MAPPED_IO mmio;

    call_type = (short) SystemCallData->SystemCallNumber;
    if (do_print > 0) {
        aprintf("SVC handler: %s\n", call_names[call_type]);
        for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++) {
            //Value = (long)*SystemCallData->Argument[i];
            aprintf("Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
                    (unsigned long) SystemCallData->Argument[i],
                    (unsigned long) SystemCallData->Argument[i]);
        }
        do_print--;
    }
    switch (call_type) {
        case SYSNUM_GET_TIME_OF_DAY:
            *(long *)SystemCallData->Argument[0]=Get_CurrentTime();
            break;
        
        case SYSNUM_SLEEP:
            Process_Sleep((long)SystemCallData->Argument[0]);
            break;
        
        case SYSNUM_GET_PROCESS_ID:
            *SystemCallData->Argument[2]=ERR_BAD_PARAM;
            *SystemCallData->Argument[2]=osGet_Process_ID((char *)SystemCallData->Argument[0],(long*)SystemCallData->Argument[1]);
            break;
        
        case SYSNUM_CREATE_PROCESS:
        {
            void *PageTable = (void *) calloc(2, NUMBER_VIRTUAL_PAGES);
            *SystemCallData->Argument[4]=osCreateProcess(CurrentProcessID, (char *)SystemCallData->Argument[0],(INT32)SystemCallData->Argument[2],(long)SystemCallData->Argument[1],(long)PageTable);
            
            if(*SystemCallData->Argument[4]==ERR_SUCCESS){
                PCB *pcbEnd=Get_the_last_PCB();
                *SystemCallData->Argument[3]=pcbEnd->processID;
            }
            
        }
            break;
        
        case SYSNUM_TERMINATE_PROCESS:
            TerminateProcess((INT32)SystemCallData->Argument[0],SystemCallData->Argument[1]);
            break;
        
        case SYSNUM_SUSPEND_PROCESS:
            osSuspendProcess((INT32)SystemCallData->Argument[0], SystemCallData->Argument[1]);
            break;
        
        case SYSNUM_RESUME_PROCESS:
            osResumeProcess((INT32)SystemCallData->Argument[0], SystemCallData->Argument[1]);
            break;
        
        case SYSNUM_CHANGE_PRIORITY:
            ChangePriority((INT32)SystemCallData->Argument[0], (INT32)SystemCallData->Argument[1], (long*)SystemCallData->Argument[2]);
            break;
            
        case SYSNUM_PHYSICAL_DISK_READ:
        {
            mmio.Field2 = DEVICE_IN_USE;
            while (mmio.Field2 != DEVICE_FREE) {
                mmio.Mode = Z502Status;
                mmio.Field1 = (INT16)SystemCallData->Argument[0];
                mmio.Field2 = mmio.Field3 = 0;
                MEM_READ(Z502Disk, &mmio);
            }
//            osReadOnDisk((INT16)SystemCallData->Argument[0],(INT16)SystemCallData->Argument[1],(long)SystemCallData->Argument[2]);
            mmio.Mode=Z502DiskRead;
            mmio.Field1=(INT16)SystemCallData->Argument[0];
            mmio.Field2=(INT16)SystemCallData->Argument[1];
            mmio.Field3=(long)SystemCallData->Argument[2];
            mmio.Field4=0;
            MEM_WRITE(Z502Disk, &mmio);
            
            INT32 CurrentProcessID=osGetCurrentProcessID();
            PCB *pcb=GetProcessByID(CurrentProcessID);
            pcb->DiskID=(INT16)SystemCallData->Argument[0];
            QInsertOnTail(DiskQueueID, pcb);
            osDispatcher();
//            osCauseZ502Idle();
//            osCauseZ502Idle();
        }
            break;
        
        case SYSNUM_PHYSICAL_DISK_WRITE:
        {
            mmio.Field2 = DEVICE_IN_USE;
            while (mmio.Field2 != DEVICE_FREE) {
                mmio.Mode = Z502Status;
                mmio.Field1 = (INT16)SystemCallData->Argument[0];
                mmio.Field2 = mmio.Field3 = 0;
                MEM_READ(Z502Disk, &mmio);
            }
//            osWriteToDisk((INT16)SystemCallData->Argument[0],(INT16)SystemCallData->Argument[1],(long)SystemCallData->Argument[2]);
            mmio.Mode=Z502DiskWrite;
            mmio.Field1=(INT16)SystemCallData->Argument[0];
            mmio.Field2=(INT16)SystemCallData->Argument[1];
            mmio.Field3=(long)SystemCallData->Argument[2];
            mmio.Field4=0;
            MEM_WRITE(Z502Disk, &mmio);
            
            INT32 CurrentProcessID=osGetCurrentProcessID();
            PCB *pcb=GetProcessByID(CurrentProcessID);
            pcb->DiskID=(INT16)SystemCallData->Argument[0];
            QInsertOnTail(DiskQueueID, pcb);
            osDispatcher();
//            osCauseZ502Idle();
//            osCauseZ502Idle();
        }
            break;
            
        case SYSNUM_CHECK_DISK:
            osCheckDisk((long)SystemCallData->Argument[0], (long *)SystemCallData->Argument[1]);
            break;
            
        case SYSNUM_DEFINE_SHARED_AREA:
            break;
            
        case SYSNUM_FORMAT:
            DiskFormat((long)SystemCallData->Argument[0],(long *)SystemCallData->Argument[1]);
            break;
            
            
        case SYSNUM_OPEN_DIR:
            OpenDirectory((long)SystemCallData->Argument[0], (char *)SystemCallData->Argument[1], (long *)SystemCallData->Argument[2]);
            break;
            
        case SYSNUM_OPEN_FILE:
            OpenFile((char *)SystemCallData->Argument[0], (long *)SystemCallData->Argument[1], (long *)SystemCallData->Argument[2]);
            break;
            
        case SYSNUM_CREATE_DIR:
            CreatefileorDirectory((char *)SystemCallData->Argument[0], (long *)SystemCallData->Argument[1],1);
            break;
            
        case SYSNUM_CREATE_FILE:
            CreatefileorDirectory((char *)SystemCallData->Argument[0], (long *)SystemCallData->Argument[1],1);
            break;
            
        case SYSNUM_READ_FILE:
            ReadFile((long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1], (char*)SystemCallData->Argument[2], (long *)SystemCallData->Argument[3]);
            break;
            
        case SYSNUM_WRITE_FILE:
            WriteFile((long)SystemCallData->Argument[0], (long)SystemCallData->Argument[1], (char *)SystemCallData->Argument[2], (long *)SystemCallData->Argument[3]);
            break;
        
        case SYSNUM_CLOSE_FILE:
            CloseFile((long)SystemCallData->Argument[0], (long *)SystemCallData->Argument[1]);
            break;
            
        case SYSNUM_DIR_CONTENTS:
            DirectoryContent((long *)SystemCallData->Argument[0]);
            break;
        
        case SYSNUM_MEM_READ:
            Z502MemoryRead((INT32)SystemCallData->Argument[0], (INT32 *)SystemCallData->Argument[1]);
            break;
        
        case SYSNUM_MEM_WRITE:
            Z502MemoryWrite((INT32)SystemCallData->Argument[0], (INT32 *)SystemCallData->Argument[1]);
            break;
        default:
            printf("ERROR! call_type not recognized!\n");
            printf( "Call_type is - %i\n", call_type);
            break;
    }
}                                               // End of svc






/************************************************************************
 
 osInit
 This is the first routine called after the simulation begins.  This
 is equivalent to boot code.  All the initial OS components can be
 defined and initialized here.
 ************************************************************************/

void osInit(int argc, char *argv[]) {
    // Every process will have a page table.  This will be used in
    // the second half of the project.
    void *PageTable = (void *) calloc(2, NUMBER_VIRTUAL_PAGES);
    INT32 i;
    MEMORY_MAPPED_IO mmio;
    
    // Demonstrates how calling arguments are passed thru to here
    
    aprintf("Program called with %d arguments:", argc);
    for (i = 0; i < argc; i++)
        aprintf(" %s", argv[i]);
    aprintf("\n");
    aprintf("Calling with argument 'sample' executes the sample program.\n");
    
    // Here we check if a second argument is present on the command line.
    // If so, run in multiprocessor mode.  Note - sometimes people change
    // around where the "M" should go.  Allow for both possibilities
    if (argc > 2) {
        if ((strcmp(argv[1], "M") ==0) || (strcmp(argv[1], "m")==0)) {
            strcpy(argv[1], argv[2]);
            strcpy(argv[2],"M\0");
        }
        if ((strcmp(argv[2], "M") ==0) || (strcmp(argv[2], "m")==0)) {
            aprintf("Simulation is running as a MultProcessor\n\n");
            mmio.Mode = Z502SetProcessorNumber;
            mmio.Field1 = MAX_NUMBER_OF_PROCESSORS;
            mmio.Field2 = (long) 0;
            mmio.Field3 = (long) 0;
            mmio.Field4 = (long) 0;
            MEM_WRITE(Z502Processor, &mmio);   // Set the number of processors
        }
    } else {
        aprintf("Simulation is running as a UniProcessor\n");
        aprintf("Add an 'M' to the command line to invoke multiprocessor operation.\n\n");
    }
    
    //  Some students have complained that their code is unable to allocate
    //  memory.  Who knows what's going on, other than the compiler has some
    //  wacky switch being used.  We try to allocate memory here and stop
    //  dead if we're unable to do so.
    //  We're allocating and freeing 8 Meg - that should be more than
    //  enough to see if it works.
    void *Temporary = (void *) calloc( 8, 1024 * 1024);
    if ( Temporary == NULL )  {  // Not allocated
        printf( "Unable to allocate memory in osInit.  Terminating simulation\n");
        exit(0);
    }
    free(Temporary);
    //  Determine if the switch was set, and if so go to demo routine.
    //  We do this by Initializing and Starting a context that contains
    //     the address where the new test is run.
    //  Look at this carefully - this is an example of how you will start
    //     all of the other tests.
    
    ReadyQueueID=QCreate(ReadyQueueName);
    TimerQueueID=QCreate(TimerQueueName);
    PCBQueueID=QCreate(PCBQueueName);
    TerminatedQueueID=QCreate(TerminatedQueueName);
    TimerSuspendQ=QCreate(TimerSuspend);
    
    if ((argc > 1) && (strcmp(argv[1], "sample") == 0)) {
        mmio.Mode = Z502InitializeContext;
        mmio.Field1 = 0;
        mmio.Field2 = (long) SampleCode;
        mmio.Field3 = (long) PageTable;

        MEM_WRITE(Z502Context, &mmio);   // Start of Make Context Sequence
        mmio.Mode = Z502StartContext;
        // Field1 contains the value of the context returned in the last call
        mmio.Field2 = START_NEW_CONTEXT_AND_SUSPEND;
        MEM_WRITE(Z502Context, &mmio);     // Start up the context

    }
    if((argc > 1) && (strcmp(argv[1],"test1")==0)){
        // Field1 contains the value of the context returned in the last call
        // Suspends this current thread
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test1";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test1,(long) PageTable);
        pcb->currentContext=ContextID;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test2")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test2";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test2,(long) PageTable);
        QInsertOnTail(PCBQueueID, pcb);
        pcb->currentContext=ContextID;
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test3")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test3";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test3,(long) PageTable);
        pcb->currentContext=ContextID;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test4")==0)){
        MaxSchedulePrint=10000;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test4";
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test4, (long) PageTable);
        pcb->currentContext=ContextID;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(pcb->currentContext);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test5")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test5";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test5,(long) PageTable);
        QInsertOnTail(PCBQueueID, pcb);
        pcb->currentContext=ContextID;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    if((argc > 1) && (strcmp(argv[1],"test6")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        CurrentProcessID=InitialProcessID+1;
        char processName[30]="test6";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test6,(long) PageTable);
        pcb->currentContext=ContextID;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    if((argc > 1) && (strcmp(argv[1],"test7")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test7";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test7,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test8")==0)){
        MaxSchedulePrint=10000;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test8";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test8,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test9")==0)){
        MaxSchedulePrint=10000;
        MaxSentTime=9;
        MessageQueueID=QCreate(MessageQueueName);
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test9";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test9,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test10")==0)){
        MaxSchedulePrint=50;
        MaxSentTime=15;
        MessageSuspendedQ=QCreate(MessageSuspendedQueueName);
        MessageQueueID=QCreate(MessageQueueName);
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test10";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test10,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    
    if((argc > 1) && (strcmp(argv[1],"test11")==0)){
        MaxSchedulePrint=50;
        DiskQueueID=QCreate(DiskQueueName);
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test11";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test11,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test12")==0)){
        MaxSchedulePrint=50;
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test12";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test12,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test13")==0)){
        MaxSchedulePrint=50;
        DiskQueueID=QCreate(DiskQueueName);
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test13";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test13,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    
    if((argc > 1) && (strcmp(argv[1],"test14")==0)){
        MaxSchedulePrint=100;
        DiskQueueID=QCreate(DiskQueueName);
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test14";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test14,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test21")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test21";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test21,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test22")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test22";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test22,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test23")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test23";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test23,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test24")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test24";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test24,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test25")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test25";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test25,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test26")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test26";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test26,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test41")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test41";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test41,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test42")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test42";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test42,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test43")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test26";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test43,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test44")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test44";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test44,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test45")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test45";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test45,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    if((argc > 1) && (strcmp(argv[1],"test46")==0)){
        PCB *pcb=(PCB*) malloc(sizeof(PCB));
        CurrentPCB=pcb;
        INT32 InitialProcessID=1;
        char processName[30]="test46";
        strcpy(pcb->processName,processName);
        pcb->processID=InitialProcessID;
        pcb->processPriority=NORMAL_PRIORITY;
        long ContextID=osInitailizeContext((long) test46,(long) PageTable);
        pcb->currentContext=ContextID;
        CurrentProcessID=InitialProcessID+1;
        QInsertOnTail(PCBQueueID, pcb);
        osStartContext(ContextID);
    }
    
    // End of handler for sample code - This routine should never return here
    //  By default test0 runs if no arguments are given on the command line
    //  Creation and Switching of contexts should be done in a separate routine.
    //  This should be done by a "OsMakeProcess" routine, so that
    //  test0 runs on a process recognized by the operating system.
    
    mmio.Mode = Z502InitializeContext;
    mmio.Field1 = 0;
    mmio.Field2 = (long) test0;
    mmio.Field3 = (long) PageTable;
    
    MEM_WRITE(Z502Context, &mmio);   // Start this new Context Sequence
    mmio.Mode = Z502StartContext;
    // Field1 contains the value of the context returned in the last call
    // Suspends this current thread
    mmio.Field2 = START_NEW_CONTEXT_AND_SUSPEND;
    MEM_WRITE(Z502Context, &mmio);     // Start up the context
    
}                                               // End of osInit
