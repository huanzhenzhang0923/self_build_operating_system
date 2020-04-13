#ifndef STUDENTCONFIGURATION_H_
#define STUDENTCONFIGURATION_H_
/****************************************************************************
 StudentConfiguration.h
 Of all the files given to you at the start of this project, this
 is the ONLY one you should ever modify.
 
 4.30 Jan 2016           StudentConfiguration.h created
 4.50 Jan 2018           Automatically configure for underlying OS
 4.60 Jun 2019           Allow debugging of MODE
 ****************************************************************************/
/****************************************************************************
 The COMPILER will choose one of the operating systems below
 You do not need to modify this choice since the compiler does
 it for you.
 ****************************************************************************/
#ifdef __unix
#define LINUX
#endif
#ifdef _WIN32
#define WINDOWS
#endif
#ifdef __APPLE__
#define MAC
#endif

/*****************************************************************
 The next two defines have special meaning.  They allow the
 Z502 processor to report information about its state.  From
 this, you can find what the hardware thinks is going on.
 The information produced when this debugging is on is NOT
 something that should be handed in with the project.
 Change FALSE to TRUE to enable a feature.
 ******************************************************************/
#define         DO_DEVICE_DEBUG                 FALSE
#define         DO_MEMORY_DEBUG                 FALSE

//  These four are very useful for my debugging the hardware,
//  but are probably less useful for students.  Use these ONLY
//  if you are seriously desperate.
#define         DEBUG_LOCKS                     FALSE
#define         DEBUG_CONDITION                 FALSE
#define         DEBUG_USER_THREADS              FALSE
#define         DEBUG_MODE                      FALSE

typedef struct{
    unsigned short DiskID;
    unsigned short Bitmap_Size;
    unsigned short RootDir_Size;
    unsigned short Swap_Size;
    unsigned short Disk_Length;
    unsigned short Bitmap_Location;
    unsigned short RootDir_Location;
    unsigned short Swap_Location;
    unsigned long  Reserved;
}Sector0;

typedef struct File_Header{
    unsigned char Inode;
    unsigned char Name[7];
    unsigned long File_Description;
    unsigned long Creation_Time;
    unsigned short Index_Location;
    unsigned short File_Size;
}Header;

typedef struct File_Queue_Node{
    Header *curnode;
    Header *prenode;
    Header *nextnode;
}FileQueueNode;

#endif /* STUDENTCONFIGURATION_H_ */
