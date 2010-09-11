#include "sbus.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

#define CLOCK   14
#define CS      13
#define SDO     12

#define CONVERSION_TIME 240 // Nao pode ir abaixo de 140
#define PREPARE_TIME 1
#define INTERVAL_TIME 1

#define NANO_SECONDS 1000
#define MICRO_SECONDS 1000

inline int clean_unwanted_bits(int data){
	return data & 0x0fffffff;
}

void wait_miliseconds(int miliseconds){
    struct timespec interval, left;

    interval.tv_sec = 0;
    interval.tv_nsec = miliseconds * MICRO_SECONDS * NANO_SECONDS;

    if (nanosleep(&interval, &left) == -1) {
        if (errno == EINTR) {
            printf("nanosleep interrupted\n");
            printf("Remaining secs: %d\n", left.tv_sec);
            printf("Remaining nsecs: %d\n", left.tv_nsec);
        }
        else perror("nanosleep");
    }
}

void wait_seconds(int seconds){

    struct timespec interval, left;

    interval.tv_sec = seconds;
    interval.tv_nsec = 0;

    if (nanosleep(&interval, &left) == -1) {
        if (errno == EINTR) {
            printf("nanosleep interrupted\n");
            printf("Remaining secs: %d\n", left.tv_sec);
            printf("Remaining nsecs: %d\n", left.tv_nsec);
        }
        else perror("nanosleep");
    }
}

void setpin(int pin, int value){
    sbuslock();
    setdiopin(pin, value);
    sbusunlock();
}

int getpin(int pin){
    int value;
    sbuslock();
    value = getdiopin(pin);
    sbusunlock();

    return value;
}

void wait_until_eoc(){
  while(getpin(SDO) == 1);
}

int do_reading(){
  int value;
  int i = 1;
  int x;

  printf(".\n");
  setpin(SDO, 2);                           // PREPARAR METER O SDO para enviar dados

  setpin(CLOCK, 0);                         // Meter clock a 0
  wait_miliseconds(PREPARE_TIME);           // Esperar um segundo
  setpin(CS, 1);                            // Desligar o CS
  wait_miliseconds(PREPARE_TIME);           // Esperar um segundo
  setpin(CS, 0);                            // Ligar o CS

  wait_miliseconds(CONVERSION_TIME);           // Esperar um segundo
 // wait_until_eoc();

  while(i <= 32){
 /*     printf("Bit %d\n", i); */
      setpin(CLOCK, 1);                       // Flanco ascendente
      wait_miliseconds(INTERVAL_TIME);         // Esperar um segundo
      x = getpin(SDO);             // Buscar valores

      /* Por a rodar para o proximo bit */
      if (x == 0)
        value = value << 1; 
      else
        value = (value << 1) | 1;

      setpin(CLOCK, 0);                        // Flanco descendente
      wait_miliseconds(INTERVAL_TIME);         // Esperar um segundo
      i++;
  }

      setpin(CLOCK, 2);                        // Flanco descendente
      setpin(CS, 1);                            // Desligar o CS

  return value;
}

int main(int argc, char **argv)
{
  int value = 0xffffffff;
  int repetitions = atoi(argv[1]);

  printf("\n");
  while(repetitions--){
  	value = do_reading();
  	printf("\t\t%x", clean_unwanted_bits(value));
  }
  printf("\n");
}

