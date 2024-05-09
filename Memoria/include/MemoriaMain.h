#ifndef MOD_MEMORIA_H_
#define MOD_MEMORIA_H_

#include "../../shared/include/shared_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    void* direccionBase;
    int limite;
} Hueco;

typedef struct {
    int idSegmento;
    int PID;
    void* direccionBase;
    int limite;
} Segmento;

typedef struct {
    void* espacioUsuario;
} Memoria;

t_config* config;
char* PUERTO_ESCUCHA;
int TAM_MEMORIA;
int TAM_SEGMENTO_0;
int CANT_SEGMENTOS;
int RETARDO_MEMORIA;
int RETARDO_COMPACTACION;
int ALGORITMO_ASIGNACION;
void* MEMORIA;
t_list* TABLA_SEGMENTOS;
t_list* TABLA_HUECOS;

int InicializarConexiones();
void* EscucharConexiones();
void* AdministradorDeModulo(void*);
void LeerConfigs(char*);
char* crearSegmento(int,int, int);
char* eliminarSegmento(int,int);
void compactarSegmentos();
void inicializarMemoria();
int validarSegmento(int,int);
void AgregarSegmento(Segmento*,int,int,int);
char* leerSegmento(int,int,int,int);
int buscarSegmento(int,int,bool);
char* escribirSegmento(int, int, int, char*);
void VerMem();
char* validarMemoria(int);
void BestFit(int,int,int);
void WorstFit(int,int,int);
void FirstFit(int,int,int);


char NOMBRE_PROCESO[8] = "Memoria";


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//PARA EVITAR DEADLOCK HACER LOS WAIT A LOS MUTEX EN EL ORDEN QUE ESTAN DECLARADOS ABAJO
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
void InicializarSemaforos();
sem_t m_UsoDeMemoria;


#endif
