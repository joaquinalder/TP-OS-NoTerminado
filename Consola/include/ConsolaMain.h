#ifndef MOD_CONSOLA_H_
#define MOD_CONSOLA_H_

#include "../../shared/include/shared_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pthread.h>

t_log* Consola_Logger;
t_config* config;

char* IP_KERNEL;
char* PUERTO_KERNEL;

char NOMBRE_PROCESO[7] = "CONSOLA";

void sighandler(int s);

int ConectarConKernel();

#endif