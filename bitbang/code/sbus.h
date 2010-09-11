#ifndef SBUS_H
#define SBUS_H
/*  Copyright 2004-2009, Technologic Systems
 *  All Rights Reserved.
 */

void sbus_poke16(unsigned int, unsigned short);
unsigned short sbus_peek16(unsigned int);
void sbuslock(void);
void sbusunlock(void);
void sbuspreempt(void); 

int getdiopin32(int pin);
#endif
