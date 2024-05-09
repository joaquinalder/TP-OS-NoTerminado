#ifndef MOD_KERNEL_H_
#define MOD_KERNEL_H_

#include "../../shared/include/shared_utils.h"
#include "../../shared/include/shared_Kernel_CPU.h"

void InicializarAdministradorDeCPU();

//Administrar Sockets/Conecciones-----------------
int InicializarConexiones();
void* EscucharConexiones();
void* AdministradorDeModulo(void* arg);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

void ForEach_Instrucciones(char* value);


//Configuracion-----------------------------
t_config* config;

void LeerConfigs(char* path);

char* IP_MEMORIA;
char* PUERTO_MEMORIA;
char* IP_FILESYSTEM;
char* PUERTO_FILESYSTEM;
char* IP_CPU;
char* PUERTO_CPU;
char* PUERTO_ESCUCHA;
int GRADO_MULTIPROGRAMACION;
char* ALGORITMO_PLANIFICACION;
int ESTIMACION_INICIAL;
float HRRN_ALFA;
char** RECURSOS;
char** G_INSTANCIAS_RECURSOS;
void LeerConfigs(char* path);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

//PCB---------------------------------

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

typedef struct
{
    char* NombreArchivo;
    t_list* ProcesosBloqueados;
} t_ArchivoGlobal;

t_list* TablaGlobalArchivosAbiertos;

t_ArchivoGlobal* BuscarEnTablaGlobal(char* NombreArchivo);

typedef struct
{
    char* NombreArchivo;
    int PosicionPuntero;
} t_ArchivoPCB;

int BuscarArchivoEnTablaDeProceso(t_list* Tabla, char* NombreArchivo);

typedef struct
{
    int socketConsolaAsociada;
    int PID;
    int programCounter;
    char* recursoBloqueante;
    t_registrosCPU* registrosCPU;
    t_list* tablaDeSegmentos;
    double tiempoUltimaRafaga;
    double estimacionUltimaRafaga;
    time_t tiempoLlegadaRedy;
    t_list* tablaArchivosAbiertos;
    t_list* listaInstrucciones;
} t_PCB;

int g_PIDCounter = 0;
t_list* g_Lista_NEW;
t_list* g_Lista_READY;
t_PCB* g_EXEC;
t_list* g_Lista_BLOCKED;
t_list* g_Lista_EXIT;
t_list* g_Lista_BLOCKED_RECURSOS;
t_list* g_Lista_BLOCKED_FS;

t_PCB* CrearPCB(t_list* instrucciones, int socketConsola);

void Enviar_PCB_A_CPU(t_PCB* PCB_A_ENVIAR);
t_registrosCPU* CrearRegistrosCPU();
t_registrosCPU* ObtenerRegistrosDelPaquete(t_list* Lista);
void Recibir_Y_Actualizar_PCB();
void ImprimirRegistrosPCB(t_PCB* PCB_Registos_A_Imprimir);
//---------------------------------------------

//Semaforos------------------------------------

//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//PARA EVITAR DEADLOCK HACER LOS WAIT A LOS MUTEX EN EL ORDEN QUE ESTAN DECLARADOS ABAJO
//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

void InicializarSemaforos();
sem_t m_PIDCounter;
sem_t m_NEW;
sem_t m_READY;
sem_t m_EXEC;
sem_t m_BLOCKED;
sem_t m_EXIT;
sem_t m_RECURSOS;
sem_t m_BLOCKED_RECURSOS;
sem_t m_BLOCKED_FS;
sem_t c_MultiProg;

//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

pthread_t HiloPlanificadorDeLargoPlazo;//FIFO
pthread_t HiloPlanificadorDeCortoPlazo;
pthread_t HiloEntradaSalida;
pthread_t HiloEscucha;
pthread_t HiloAdministradorDeMensajes;

//PLANIFICADORES--------------------------------
int InicializarPlanificadores();

void* PlanificadorLargoPlazo();
void* PlanificadorCortoPlazo();

void PlanificadorCortoPlazoFIFO();

void PlanificadorCortoPlazoHRRN();

double TiempoEsperadoEnReady(t_PCB * PCB, time_t tiempoActual);
double EstimacionProximaRafaga(t_PCB* PCB);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

void AgregarAReady(t_PCB* PCB);
void TerminarProceso(t_PCB* PCB, char* Motivo);
void LoguearCambioDeEstado(t_PCB* PCB, char* EstadoAnterior, char* EstadoActual);

void RealizarRespuestaDelCPU(char* respuesta);

void* EsperarEntradaSalida(void* arg);
void DesbloquearPorFS();
char* RecibirDeFS();
void ErrorArchivoInexistente(bool RequiereEnviarMensage);

void TerminarModulo();
void LimpiarListaDePCBs(t_list* lista);
void LimpiarElementosDeTabla(t_list* tabla);
void LimpiarPCB(t_PCB* PCB_A_Liberar);
void LimpiarTablaDeArchivosDelProceso(t_list* Tabla);
void LimpiarTablaGlobalArchivosAbiertos();
bool ModuloDebeTerminar = false;

char NOMBRE_PROCESO[7] = "KERNEL";

#endif