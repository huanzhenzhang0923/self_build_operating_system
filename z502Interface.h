//
//  z502Interface.h
//  CS502
//
//  Created by Huanzhen Zhang on 11/26/19.
//  Copyright © 2019 Huanzhen Zhang. All rights reserved.
//

#ifndef z502Interface_h
#define z502Interface_h
long Get_CurrentTime(void);

long osGetCurrentContext(void);

long osGetNumofProcessor(void);

void osStartContext(long ContextID);

long osInitailizeContext(long Address,long pagetable);

void osCauseZ502Idle(void);

void osStartTimer(long Time);

void HaltZ502(void);
#endif /* z502Interface_h */
