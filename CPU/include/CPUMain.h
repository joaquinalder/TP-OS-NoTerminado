#ifndef MOD_CPU_H_
#define MOD_CPU_H_

#include "../../shared/include/shared_utils.h"
#include "../../shared/include/shared_Kernel_CPU.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


int InicializarConexiones();
void* EscuchaKernel();

void LeerConfigs(char* path);
t_config* config;
char* IP_MEMORIA;
char* PUERTO_MEMORIA;
char* PUERTO_ESCUCHA;
int RETARDO_INSTRUCCION;
int TAM_MAX_SEGMENTO;

char NOMBRE_PROCESO[4] = "CPU";

typedef struct
{
    char AX[4];
    char BX[4];
    char CX[4];
    char DX[4];

    char EAX[8];
    char EBX[8];
    char ECX[8];
    char EDX[8];

    char RAX[16];
    char RBX[16];
    char RCX[16];
    char RDX[16];
} t_registrosCPU;

t_registrosCPU* ObtenerRegistrosDelPaquete(t_list* Lista);
void Enviar_PCB_A_Kernel(int ProgramCounter, t_registrosCPU* Registros_A_Enviar, int SocketKernel);
char* ObtenerRegistro(char* NombreRegistro, t_registrosCPU* Registros);
int ObtenerTamanoRegistro(char* NombreRegistro);

void LimpiarElementosDeTabla(t_list* tabla);

void TraducirDireccion(char* CharDirLogica, int* NumSegmento, int* Offset);

bool ModuloDebeTerminar = false;

#endif