/*  Copyright 2004-2009, Technologic Systems
 *  All Rights Reserved.
 */
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sched.h>

void sbus_poke16(unsigned int, unsigned short);
static void poke32(unsigned int, unsigned int);
unsigned short sbus_peek16(unsigned int);
static unsigned int peek32(unsigned int);

void sbuslock(void);
void sbusunlock(void);
void sbuspreempt(void); 

static volatile unsigned int *cvspiregs, *cvgpioregs;
static int last_gpio_adr = 0;

void sbus_poke16(unsigned int adr, unsigned short dat) {
	unsigned int dummy;
	if (last_gpio_adr != adr >> 5) {
		last_gpio_adr = adr >> 5;
		cvgpioregs[0] = (cvgpioregs[0] & ~(0x3<<15))|((adr>>5)<<15);
	}
	adr &= 0x1f;

	asm volatile (
		"mov %0, %1, lsl #18\n"
		"orr %0, %0, #0x800000\n"
		"orr %0, %0, %2, lsl #3\n"
		"3: ldr r1, [%3, #0x64]\n"
		"cmp r1, #0x0\n"
		"bne 3b\n"
		"2: str %0, [%3, #0x50]\n"
		"1: ldr r1, [%3, #0x64]\n"
		"cmp r1, #0x0\n"
		"beq 1b\n"
		"ldr %0, [%3, #0x58]\n"
		"ands r1, %0, #0x1\n"
		"moveq %0, #0x0\n"
		"beq 3b\n"
		: "+r"(dummy) : "r"(adr), "r"(dat), "r"(cvspiregs) : "r1","cc"
	);
}


unsigned short sbus_peek16(unsigned int adr) {
	unsigned short ret;

	if (last_gpio_adr != adr >> 5) {
		last_gpio_adr = adr >> 5;
		cvgpioregs[0] = ((adr>>5)<<15|1<<3|1<<17);
	}
	adr &= 0x1f;

	asm volatile (
		"mov %0, %1, lsl #18\n"
		"2: str %0, [%2, #0x50]\n"
		"1: ldr r1, [%2, #0x64]\n"
		"cmp r1, #0x0\n"
		"beq 1b\n"
		"ldr %0, [%2, #0x58]\n"
		"ands r1, %0, #0x10000\n"
		"bicne %0, %0, #0xff0000\n"
		"moveq %0, #0x0\n"
		"beq 2b\n" 
		: "+r"(ret) : "r"(adr), "r"(cvspiregs) : "r1", "cc"
	);

	return ret;

}


static void reservemem(void) {
	char dummy[32768];
	int i, pgsize;
	FILE *maps;

	pgsize = getpagesize();
	mlockall(MCL_CURRENT|MCL_FUTURE);
	for (i = 0; i < sizeof(dummy); i += 4096) {
		dummy[i] = 0;
	}

	maps = fopen("/proc/self/maps", "r"); 
	if (maps == NULL) {
		perror("/proc/self/maps");
		exit(1);
	}
	while (!feof(maps)) {
		unsigned int s, e, i;
		char m[PATH_MAX + 1];
		char perms[16];
		int r = fscanf(maps, "%x-%x %s %*x %*x:%*x %*d",
		  &s, &e, perms);
		if (r == EOF) break;
	//	assert (r == 2);
	//	printf("MANEL %d", r);

		i = 0;
		while ((r = fgetc(maps)) != '\n') {
			if (r == EOF) break;
			m[i++] = r;
		}
		m[i] = '\0';
		assert(s <= e && (s & 0xfff) == 0);
		if (perms[0] == 'r') while (s < e) {
			volatile unsigned char *ptr = (unsigned char *)s;
			*ptr;
			s += pgsize;
		}
	}
}


static int semid = -1;
static int sbuslocked = 0;
void sbuslock(void) {
	int r;
	struct sembuf sop;
	static int inited = 0;
	if (semid == -1) {
		key_t semkey;
		reservemem();
		semkey = 0x75000000;
		semid = semget(semkey, 1, IPC_CREAT|IPC_EXCL|0777);
		if (semid != -1) {
			sop.sem_num = 0;
			sop.sem_op = 1;
			sop.sem_flg = 0;
			r = semop(semid, &sop, 1);
			assert (r != -1);
		} else semid = semget(semkey, 1, 0777);
		assert (semid != -1);
	}
	sop.sem_num = 0;
	sop.sem_op = -1;
	sop.sem_flg = SEM_UNDO;
	r = semop(semid, &sop, 1);
	assert (r == 0);
	if (inited == 0) {
		int i;
		int devmem;

		inited = 1;
		devmem = open("/dev/mem", O_RDWR|O_SYNC);
		assert(devmem != -1);
		cvspiregs = (unsigned int *) mmap(0, 4096,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x71000000);
		cvgpioregs = (unsigned int *) mmap(0, 4096,
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x7c000000);

		cvspiregs[0x64/4] = 0x0; /* RX IRQ threahold 0 */
		cvspiregs[0x40/4] = 0x80000c02; /* 24-bit mode no byte swap */
		cvspiregs[0x60/4] = 0x0; /* 0 clock inter-transfer delay */
		cvspiregs[0x6c/4] = 0x0; /* disable interrupts */
		cvspiregs[0x4c/4] = 0x4; /* deassert CS# */
		for (i = 0; i < 8; i++) cvspiregs[0x58 / 4];
		last_gpio_adr = 3;
	}
	cvgpioregs[0] = (cvgpioregs[0] & ~(0x3<<15))|(last_gpio_adr<<15);
	sbuslocked = 1;
}


void sbusunlock(void) {
	struct sembuf sop = { 0, 1, SEM_UNDO};
	int r;
	if (!sbuslocked) return;
	r = semop(semid, &sop, 1);
	assert (r == 0);
	sbuslocked = 0;
}


void sbuspreempt(void) {
	int r;
	r = semctl(semid, 0, GETNCNT);
	assert (r != -1);
	if (r) {
		sbusunlock();
		sched_yield();
		sbuslock();
	}
}

/*******************************************************************************
* setdiopin: accepts a DIO register and value to place in that DIO pin.
*   Values can be 0 (low), 1 (high), or 2 (z - high impedance).
*******************************************************************************/
void setdiopin(int pin, int val)
{
   int pinOffSet;
   int dirPinOffSet; // For Register 0x66 only
   int outPinOffSet; // For Register 0x66 only
   
   // First, check for the high impedance case
   if (val == 2)
   {
      if (pin <= 40 && pin >= 37)
      {
         dirPinOffSet = pin - 33;
         sbus_poke16(0x66, sbus_peek16(0x66) & ~(1 << dirPinOffSet));
      }
      else if (pin <= 36 && pin >= 21)
      {
         pinOffSet = pin - 21;
         sbus_poke16(0x6c, sbus_peek16(0x6c) & ~(1 << pinOffSet));
      }
      else if (pin <= 20 && pin >= 5)   
      {
         pinOffSet = pin - 5;
         sbus_poke16(0x72, sbus_peek16(0x72) & ~(1 << pinOffSet));
      }
   }
   
   /******************************************************************* 
   *0x66: DIO and tagmem control (RW)
   *  bit 15-12: DIO input for pins 40(MSB)-37(LSB) (RO)
   *  bit 11-8: DIO output for pins 40(MSB)-37(LSB) (RW)
   *  bit 7-4: DIO direction for pins 40(MSB)-37(LSB) (1 - output) (RW)
   ********************************************************************/
   else if (pin <= 40 && pin >= 37) 
   {
      dirPinOffSet = pin - 33; // -37 + 4 = Direction; -37 + 8 = Output
      outPinOffSet = pin - 29;
      
      // set bit [pinOffset] to [val] of register [0x66] 
      if(val)
         sbus_poke16(0x66, (sbus_peek16(0x66) | (1 << outPinOffSet)));
      else
         sbus_poke16(0x66, (sbus_peek16(0x66) & ~(1 << outPinOffSet)));      

      // Make the specified pin into an output in direction bits
      sbus_poke16(0x66, sbus_peek16(0x66) | (1 << dirPinOffSet)); ///

   }
   
   /********************************************************************* 
   *0x68: DIO input for pins 36(MSB)-21(LSB) (RO)    
   *0x6a: DIO output for pins 36(MSB)-21(LSB) (RW)
   *0x6c: DIO direction for pins 36(MSB)-21(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 36 && pin >= 21)
   {
      pinOffSet = pin - 21;
      
      // set bit [pinOffset] to [val] of register [0x6a] 
      if(val)
         sbus_poke16(0x6a, (sbus_peek16(0x6a) | (1 << pinOffSet)));
      else
         sbus_poke16(0x6a, (sbus_peek16(0x6a) & ~(1 << pinOffSet)));      

      // Make the specified pin into an output in direction register
      sbus_poke16(0x6c, sbus_peek16(0x6c) | (1 << pinOffSet)); ///
   }

   /********************************************************************* 
   *0x6e: DIO input for pins 20(MSB)-5(LSB) (RO)    
   *0x70: DIO output for pins 20(MSB)-5(LSB) (RW)
   *0x72: DIO direction for pins 20(MSB)-5(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 20 && pin >= 5)
   {
      pinOffSet = pin - 5;

      if(val)
         sbus_poke16(0x70, (sbus_peek16(0x70) | (1 << pinOffSet)));
      else
         sbus_poke16(0x70, (sbus_peek16(0x70) & ~(1 << pinOffSet)));     
               
      // Make the specified pin into an output in direction register
      sbus_poke16(0x72, sbus_peek16(0x72) | (1 << pinOffSet));
   }

}

/*******************************************************************************
* getdiopin: accepts a DIO pin number and returns its value.  
*******************************************************************************/
int getdiopin(int pin)
{
   int pinOffSet;
   int pinValue = 99999;

   /******************************************************************* 
   *0x66: DIO and tagmem control (RW)
   *  bit 15-12: DIO input for pins 40(MSB)-37(LSB) (RO)
   *  bit 11-8: DIO output for pins 40(MSB)-37(LSB) (RW)
   *  bit 7-4: DIO direction for pins 40(MSB)-37(LSB) (1 - output) (RW)
   ********************************************************************/
   if (pin <= 40 && pin >= 37) 
   {
      pinOffSet = pin - 25; // -37 to get to 0, + 10 to correct offset

      // Obtain the specific pin value (1 or 0)
      pinValue = (sbus_peek16(0x66) >> pinOffSet) & 0x0001;
   }
   
   /*********************************************************************   
   *0x68: DIO input for pins 36(MSB)-21(LSB) (RO)  
   *0x6a: DIO output for pins 36(MSB)-21(LSB) (RW)
   *0x6c: DIO direction for pins 36(MSB)-21(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 36 && pin >= 21)
   {
      pinOffSet = pin - 21; // Easier to understand when LSB = 0 and MSB = 15

      // Obtain the specific pin value (1 or 0)
      pinValue = (sbus_peek16(0x68) >> pinOffSet) & 0x0001;
   }

   /*********************************************************************   
   *0x6e: DIO input for pins 20(MSB)-5(LSB) (RO)  
   *0x70: DIO output for pins 20(MSB)-5(LSB) (RW)
   *0x72: DIO direction for pins 20(MSB)-5(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 20 && pin >= 5)
   {
      pinOffSet = pin - 5;  // Easier to understand when LSB = 0 and MSB = 15

      // Obtain the specific pin value (1 or 0)
      pinValue = (sbus_peek16(0x6e) >> pinOffSet) & 0x0001;
   }
   return pinValue;
}

/*******************************************************************************
* gettemp: returns the CPU temperature in celcius
*******************************************************************************/
float gettemp(void)
{
   int n = 0;
   int x = 0;
   int val = 0;
   float temp = 0; 
   
   setdiopin(22,0);
   setdiopin(12,2);
   n=0;
   while(n < 13)
   {
      setdiopin(14,0);
      setdiopin(14,1);
      x = getdiopin(12);
      if (x == 0)
         val = val << 1;
      else
         val = (val << 1) | 1;
      
      n++;
    }

    setdiopin(22,2);
    setdiopin(14,2);

    if((val & 0x1000) != 0)
    {
      val=((~(val & 0xfff) & 0xfff) + 1);
      temp = val * -62500;
      return(temp / 1000000);
    }
    else
    {
      temp = val * 62500;
      return(temp / 1000000);
    }
}

static void poke32(unsigned int adr, unsigned int dat) {
	if (!cvspiregs) *(unsigned int *)adr = dat;
	else {
		sbus_poke16(adr, dat & 0xffff);
		sbus_poke16(adr + 2, dat >> 16);
	}
}


static unsigned int peek32(unsigned int adr) {
	unsigned int ret;
	if (!cvspiregs) ret = *(unsigned int *)adr;
	else {
		unsigned short l, h;
		l = sbus_peek16(adr);
		h = sbus_peek16(adr + 2);
		ret = (l|(h<<16));
	}
	return ret;
}


int getdiopin32(int pin)
{
   int pinOffSet;
   int pinValue = 99999;

   /******************************************************************* 
   *0x66: DIO and tagmem control (RW)
   *  bit 15-12: DIO input for pins 40(MSB)-37(LSB) (RO)
   *  bit 11-8: DIO output for pins 40(MSB)-37(LSB) (RW)
   *  bit 7-4: DIO direction for pins 40(MSB)-37(LSB) (1 - output) (RW)
   ********************************************************************/
   if (pin <= 40 && pin >= 37) 
   {
      pinOffSet = pin - 25; // -37 to get to 0, + 10 to correct offset

      // Obtain the specific pin value (1 or 0)
      pinValue = (peek32(0x66) >> pinOffSet) & 0x0001;
   }
   
   /*********************************************************************   
   *0x68: DIO input for pins 36(MSB)-21(LSB) (RO)  
   *0x6a: DIO output for pins 36(MSB)-21(LSB) (RW)
   *0x6c: DIO direction for pins 36(MSB)-21(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 36 && pin >= 21)
   {
      pinOffSet = pin - 21; // Easier to understand when LSB = 0 and MSB = 15

      // Obtain the specific pin value (1 or 0)
      pinValue = (peek32(0x68) >> pinOffSet) & 0x0001;
   }

   /*********************************************************************   
   *0x6e: DIO input for pins 20(MSB)-5(LSB) (RO)  
   *0x70: DIO output for pins 20(MSB)-5(LSB) (RW)
   *0x72: DIO direction for pins 20(MSB)-5(LSB) (1 - output) (RW)
   *********************************************************************/
   else if (pin <= 20 && pin >= 5)
   {
      pinOffSet = pin - 5;  // Easier to understand when LSB = 0 and MSB = 15

      // Obtain the specific pin value (1 or 0)
      pinValue = (peek32(0x6e) >> pinOffSet) & 0x0001;
   }
   return pinValue;
}
