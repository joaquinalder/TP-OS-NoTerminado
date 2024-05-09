#ifndef MOD_FS_H_
#define MOD_FS_H_

#include "../../shared/include/shared_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>


typedef struct {
	char* nombreArchivo;
	int tamanoArchivo;
	uint32_t* punteroDirecto;
	uint32_t* punteroIndirecto;
}FCB;

t_config *config, *configSB;
char* IP_MEMORIA;
char* PUERTO_MEMORIA;
char* PUERTO_ESCUCHA;
char* BLOCK_SIZE;
char* BLOCK_COUNT;
void* BLOQUES;
char* PATH_FCB;
char* PATH_BLOQUES;
int fd;

void InicializarConexiones();
void* EscuchaKernel();
void LeerConfigs(char*, char*);
void LiberarMemoria();
void CrearArchivo(char*);

char NOMBRE_PROCESO[11] = "FileSystem";

#endif
