#include "../include/KernelMain.h"

t_log* Kernel_Logger;

int SocketKernel;
int SocketCPU;
int SocketMemoria;
int SocketFileSystem;
bool planificadorFIFO;

t_config* config;


void sighandler(int s) 
{
	//ModuloDebeTerminar = true;
	TerminarModulo();
	pthread_join(HiloPlanificadorDeLargoPlazo, NULL);
	pthread_join(HiloPlanificadorDeCortoPlazo, NULL);
	printf("Terminando Modulo Kernel\n");

	//pthread_join(HiloAdministradorDeCPU, NULL);
	exit(0);
}

int main(int argc, char* argv[])
{
	signal(SIGINT, sighandler);
	signal(SIGSEGV, sighandler);

	LeerConfigs(argv[1]);

	InicializarSemaforos();

	Kernel_Logger = log_create("Kernel.log", NOMBRE_PROCESO, true, LOG_LEVEL_INFO);

	//inicializar Tabla Global de Archivos Abiertos
	TablaGlobalArchivosAbiertos = list_create();

	if(InicializarPlanificadores() == 0)
		return EXIT_FAILURE;

	if(InicializarConexiones() == 0)
		return EXIT_FAILURE;
	//temporal
	while(!ModuloDebeTerminar){
		log_info(Kernel_Logger, "hilo principal esta ejecutando");
		sleep(20);
	}

	TerminarModulo();
	return EXIT_SUCCESS;
}

//borrar ifs de todos los archivos
void LeerConfigs(char* path)
{
    config = config_create(path);

    IP_MEMORIA = config_get_string_value(config, "IP_MEMORIA");

    PUERTO_MEMORIA = config_get_string_value(config, "PUERTO_MEMORIA");

    IP_FILESYSTEM = config_get_string_value(config, "IP_FILESYSTEM");

    PUERTO_FILESYSTEM = config_get_string_value(config, "PUERTO_FILESYSTEM");

    IP_CPU = config_get_string_value(config, "IP_CPU");

    PUERTO_CPU = config_get_string_value(config, "PUERTO_CPU");

    PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");

	GRADO_MULTIPROGRAMACION = config_get_int_value(config, "GRADO_MAX_MULTIPROGRAMACION");

	ALGORITMO_PLANIFICACION = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
	
	ESTIMACION_INICIAL = config_get_int_value(config, "ESTIMACION_INICIAL");
	
	HRRN_ALFA = config_get_double_value(config, "HRRN_ALFA");

	RECURSOS = config_get_array_value(config, "RECURSOS");

	G_INSTANCIAS_RECURSOS = config_get_array_value(config, "INSTANCIAS_RECURSOS");
}

//Inicializa los semaforos
void InicializarSemaforos()
{
	sem_init(&m_PIDCounter, 0, 1);
	sem_init(&m_NEW, 0, 1);
	sem_init(&m_READY, 0, 1);
	sem_init(&m_EXEC, 0, 1);
	sem_init(&m_EXIT, 0, 1);
	sem_init(&m_BLOCKED, 0, 1);
	sem_init(&m_RECURSOS, 0, 1);
	sem_init(&m_BLOCKED_RECURSOS, 0, 1);
	sem_init(&m_BLOCKED_FS, 0, 1);
	sem_init(&c_MultiProg, 0, GRADO_MULTIPROGRAMACION);
}

int InicializarPlanificadores()
{
	g_Lista_NEW = list_create();
	g_Lista_READY = list_create();
	g_Lista_EXIT = list_create();
	g_Lista_BLOCKED = list_create();
	g_Lista_BLOCKED_RECURSOS = list_create();
	g_Lista_BLOCKED_FS = list_create();

	if (pthread_create(&HiloPlanificadorDeLargoPlazo, NULL, PlanificadorLargoPlazo, NULL) != 0) {
		return 0;
	}

	planificadorFIFO = strcmp(ALGORITMO_PLANIFICACION, "HRRN") != 0;

	if (pthread_create(&HiloPlanificadorDeCortoPlazo, NULL, PlanificadorCortoPlazo, NULL)) {
		return 0;
	}

	return 1;
}

//Planificador de largo plazo FIFO (New -> Ready)
void* PlanificadorLargoPlazo()
{
	while(!ModuloDebeTerminar)
	{
		//Si hay espacio en la cola de READY y hay procesos esperando en NEW,
		//obtener el primero de la cola de NEW y mandarlo a READY
		if(list_size(g_Lista_NEW) != 0)
		{
			sem_wait(&c_MultiProg);
			
			sem_wait(&m_NEW);
			sem_wait(&m_READY);
			t_PCB* PCB = (t_PCB*) list_remove(g_Lista_NEW, 0);
			AgregarAReady(PCB);
			sem_post(&m_NEW);
			sem_post(&m_READY);
			LoguearCambioDeEstado(PCB, "NEW", "READY");
		}

		//limpiar cola de exit
		sem_wait(&m_EXIT);
		while(!list_is_empty(g_Lista_EXIT))
		{	
			t_PCB* PCB_A_Liberar = list_remove(g_Lista_EXIT, 0);
			LimpiarPCB(PCB_A_Liberar);
		}
		sem_post(&m_EXIT);
		sleep(2);
	}

	pthread_exit(NULL);
}

//Planificador de corto plazo (Ready -> Exec)
void* PlanificadorCortoPlazo()
{	
	if(planificadorFIFO)
	{
		PlanificadorCortoPlazoFIFO();
	}
	else
	{
		PlanificadorCortoPlazoHRRN();
	}

	pthread_exit(NULL);
}

void PlanificadorCortoPlazoFIFO()
{
	while(!ModuloDebeTerminar)
	{
		//Si no hay ningun proceso ejecutando y hay procesos esperando en ready,
		//obtener el primero de la cola de READY y mandarlo a EXEC
		if(g_EXEC == NULL && list_size(g_Lista_READY) != 0){
			sem_wait(&m_READY);
			sem_wait(&m_EXEC);
			//obtener PCB de la cola de READY
			t_PCB* PCB = (t_PCB*) list_remove(g_Lista_READY, 0);
			//Agregar PCB a la cola de EXEC
			g_EXEC = PCB;
			sem_post(&m_READY);
			sem_post(&m_EXEC);
			LoguearCambioDeEstado(PCB, "READY", "EXEC");

			//Enviar PCB a CPU
			Enviar_PCB_A_CPU(PCB);

			//Recibir respuesta de CPU
			char* respuesta = (char*) recibir_paquete(SocketCPU);
			
			RealizarRespuestaDelCPU(respuesta);

			free(respuesta);
		}
	}
}

void PlanificadorCortoPlazoHRRN()
{
	while(!ModuloDebeTerminar)
	{
		//Si no hay ningun proceso ejecutando y hay procesos esperando en ready,
		//Calcular el proximo que deberia ejecutar
		if(g_EXEC == NULL && list_size(g_Lista_READY) != 0){

			double RatioMasAlto = 0;
			int IndiceListaRatioMasAlto = 0;
			time_t tiempoActual= time(NULL);

			sem_wait(&m_READY);
			for(int i = 0; i < list_size(g_Lista_READY); i++)
			{				
				t_PCB* aux = (t_PCB*) list_get(g_Lista_READY, i);

				//si nunca ejecuto, entonces tomo la estimacion inicial
				if(aux->programCounter == 0 && RatioMasAlto < ESTIMACION_INICIAL)
				{
					RatioMasAlto = ESTIMACION_INICIAL;
					IndiceListaRatioMasAlto = i;
				}
				//si ya ejecuto al menos una vez entonces calculo el ratio
				else
				{
					//calculo el ratio
					double Ratio = (TiempoEsperadoEnReady(aux, tiempoActual) + EstimacionProximaRafaga(aux)) / EstimacionProximaRafaga(aux);
					
					//guardo la estimacion de la ultima rafaga para la proxima que calcule el ratio
					aux->estimacionUltimaRafaga = EstimacionProximaRafaga(aux);

					//si el ratio es mas alto que el mas alto encontrado hasta ahora,
					// entonces lo reemplazo por este
					if(Ratio > RatioMasAlto)
					{
						RatioMasAlto = Ratio;
						IndiceListaRatioMasAlto = i;
					}
				}
			}

			sem_wait(&m_EXEC);
			//obtener PCB de la cola de READY
			t_PCB* PCB = (t_PCB*) list_remove(g_Lista_READY, IndiceListaRatioMasAlto);
			//Agregar PCB a la cola de EXEC
			g_EXEC = PCB;
			sem_post(&m_READY);
			sem_post(&m_EXEC);

			LoguearCambioDeEstado(PCB, "READY", "EXEC");

			//Enviar PCB a CPU
			Enviar_PCB_A_CPU(PCB);

			//Recibir respuesta de CPU
			//printf("PID del proceso enviado a la CPU: %d\n", g_EXEC->PID);
			char* respuesta = (char*) recibir_paquete(SocketCPU);
			//printf("Respuesta de CPU: %s \n", respuesta);
			
			RealizarRespuestaDelCPU(respuesta);

			free(respuesta);
		}
	}
}

//devuelve el tiempo que paso desde que el proceso llego a ready
double TiempoEsperadoEnReady(t_PCB * PCB, time_t tiempoActual)
{
	return difftime(tiempoActual, PCB->tiempoLlegadaRedy);
}

//calcula y devuelve la estimacion de la proxima rafaga de un PCB
double EstimacionProximaRafaga(t_PCB* PCB)
{
	return (HRRN_ALFA * PCB->tiempoUltimaRafaga) + ((1 - HRRN_ALFA) * PCB->estimacionUltimaRafaga);
}

void Enviar_PCB_A_CPU(t_PCB* PCB_A_ENVIAR)
{
	t_paquete* Paquete_PCB_Actual = crear_paquete(CPU_PCB);

	//Agrega el Program Counter
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->programCounter), sizeof(int));

	//Agrega el PID
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->PID), sizeof(int));

	//Agrega los registros de la CPU
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->AX), sizeof(PCB_A_ENVIAR->registrosCPU->AX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->BX), sizeof(PCB_A_ENVIAR->registrosCPU->BX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->CX), sizeof(PCB_A_ENVIAR->registrosCPU->CX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->DX), sizeof(PCB_A_ENVIAR->registrosCPU->DX));

	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->EAX), sizeof(PCB_A_ENVIAR->registrosCPU->EAX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->EBX), sizeof(PCB_A_ENVIAR->registrosCPU->EBX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->ECX), sizeof(PCB_A_ENVIAR->registrosCPU->ECX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->EDX), sizeof(PCB_A_ENVIAR->registrosCPU->EDX));

	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->RAX), sizeof(PCB_A_ENVIAR->registrosCPU->RAX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->RBX), sizeof(PCB_A_ENVIAR->registrosCPU->RBX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->RCX), sizeof(PCB_A_ENVIAR->registrosCPU->RCX));
	agregar_a_paquete(Paquete_PCB_Actual, &(PCB_A_ENVIAR->registrosCPU->RDX), sizeof(PCB_A_ENVIAR->registrosCPU->RDX));

	//Agregar tabla de instrucciones
	AgregarTablaSegmentosAlPaquete(Paquete_PCB_Actual, PCB_A_ENVIAR->tablaDeSegmentos);

	//Agrega las instrucciones
	//como pueden ser n instrucciones, las agrego ultimas;
	for(int i=0; i < list_size(PCB_A_ENVIAR->listaInstrucciones); i++)
	{
		char* instruccion = (char*) list_get(PCB_A_ENVIAR->listaInstrucciones, i);
					
		agregar_a_paquete(Paquete_PCB_Actual, instruccion, strlen(instruccion)+1);
	}

	enviar_paquete(Paquete_PCB_Actual, SocketCPU);
	eliminar_paquete(Paquete_PCB_Actual);
}

//agrega un PCB a la cola de READY y actualiza su tiempo de llegada a ready,
// asume que ya se hicieron los wait y signal correspondientes
void AgregarAReady(t_PCB* PCB)
{
	PCB->tiempoLlegadaRedy = time(NULL);
	list_add(g_Lista_READY, PCB);

	int Tamanio = list_size(g_Lista_READY) * 10;

	char Cola[Tamanio];
	int i=0;
	t_PCB* aux;
	aux = (t_PCB*) list_get(g_Lista_READY, 0);
	sprintf(Cola, "%d", aux->PID);
	i++;
	while(i < list_size(g_Lista_READY) && (strlen(Cola) + 2) < Tamanio)
	{
		aux = (t_PCB*) list_get(g_Lista_READY, i);
		sprintf(Cola + strlen(Cola), ", %d", aux->PID);
		i++;
	}

	log_info(Kernel_Logger, "Cola de READY (%s): [%s]", ALGORITMO_PLANIFICACION, Cola);
	
}

void TerminarProceso(t_PCB* PCB, char* Motivo)
{
	log_info(Kernel_Logger, "Finaliza el proceso [%d] - Motivo: %s", PCB->PID, Motivo);

	char Mensaje[60];

	sprintf(Mensaje, "FINALIZAR_PROCESO %d", PCB->PID);
	EnviarMensage(Mensaje, SocketMemoria);

	sprintf(Mensaje, "Proceso Finalizado - Motivo: %s", Motivo);
	EnviarMensage(Mensaje, PCB->socketConsolaAsociada);
}

void LoguearCambioDeEstado(t_PCB* PCB, char* EstadoAnterior, char* EstadoActual)
{
	log_info(Kernel_Logger, "PID: [%d] - Estado Anterior: %s - Estado Actual: %s", PCB->PID, EstadoAnterior, EstadoActual);
}

//Realiza la Accion correspondiente a la respuesta del CPU
void RealizarRespuestaDelCPU(char* respuesta)
{
	//printf("Respuesta recibida: %s", respuesta);
	if(strcmp(respuesta, "YIELD\n") == 0)
	{
		Recibir_Y_Actualizar_PCB();

		LoguearCambioDeEstado(g_EXEC, "EXEC", "READY");		
		//Agregar PCB a la cola de READY
		sem_wait(&m_READY);
		sem_wait(&m_EXEC);
		AgregarAReady(g_EXEC);
		g_EXEC = NULL;
		sem_post(&m_READY);
		sem_post(&m_EXEC);
		
	}

	else if(strcmp(respuesta, "I/O\n")== 0)
	{
		char* msg = (char*) recibir_paquete(SocketCPU);
		int tiempo = atoi(msg);
		
		Recibir_Y_Actualizar_PCB();

		LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");
		log_info(Kernel_Logger, "PID: [%d] - Bloqueado por: I/O", g_EXEC->PID);
		log_info(Kernel_Logger, "PID: [%d] - Ejecuta IO: %d", g_EXEC->PID, tiempo);

		sem_wait(&m_EXEC);
		sem_wait(&m_BLOCKED);
		list_add(g_Lista_BLOCKED, g_EXEC);
		t_PCB* PCB_aux = g_EXEC;
		g_EXEC = NULL;
		sem_post(&m_EXEC);
		sem_post(&m_BLOCKED);

		t_list* ListaAux = list_create();
		list_add(ListaAux, PCB_aux);
		list_add(ListaAux, (void*)(intptr_t)tiempo);

		if (pthread_create(&HiloEntradaSalida, NULL, EsperarEntradaSalida, (void*)ListaAux) != 0) {
			return (void)0;
		}
		pthread_detach(HiloEntradaSalida);

		free(msg);
	}

	else if(strcmp(respuesta, "EXIT\n")== 0)
	{
		Recibir_Y_Actualizar_PCB();

		//imprimir los valores del registro
		ImprimirRegistrosPCB(g_EXEC);

		LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");

		//Notificar a la consola que el proceso termino exitosamente
		TerminarProceso(g_EXEC, "SUCCESS");

		//Agregar PCB a la cola de EXIT
		sem_wait(&m_EXEC);
		sem_wait(&m_EXIT);
		list_add(g_Lista_EXIT, g_EXEC);
		g_EXEC = NULL;
		sem_post(&m_EXEC);
		sem_post(&m_EXIT);

		sem_post(&c_MultiProg);		
	}
	
	else if(strcmp(respuesta, "WAIT\n")== 0)
	{
		char* msg = (char*) recibir_paquete(SocketCPU);
		char* Recurso = strtok(msg,"\n");

		bool cont = true;
		int aux = 0;
		int pos = -1;

		while (cont)
		{
			if(RECURSOS[aux] == NULL)
				cont = false;
			else if(strcmp(RECURSOS[aux], Recurso) == 0)
			{
				pos = aux;
				cont = false;
			}
			else
				aux++;
		}

		//Si idio un recurso que no existe -> Terminar el proceso
		if(pos == -1)
		{
			EnviarMensage("RECHAZADO", SocketCPU);
			
			Recibir_Y_Actualizar_PCB();

			LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
			TerminarProceso(g_EXEC, "SEG_FAULT");

			//Agregar PCB a la cola de EXIT
			sem_wait(&m_EXEC);
			sem_wait(&m_EXIT);
			list_add(g_Lista_EXIT, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_EXIT);

			sem_post(&c_MultiProg);
		}
		//Si existe el recurso
		else
		{
			sem_wait(&m_RECURSOS);
			int aux = atoi(G_INSTANCIAS_RECURSOS[pos]) - 1;
			sprintf(G_INSTANCIAS_RECURSOS[pos], "%d", aux);
			sem_post(&m_RECURSOS);

			log_info(Kernel_Logger, "PID: [%d] - Wait: %s - Instancias: %d", g_EXEC->PID, Recurso, aux);

			//Si no hay recursos disponibles,
			//agrego el proceso a la lista de bloqueados por Recursos
			if(aux < 0)
			{
				EnviarMensage("RECHAZADO", SocketCPU);

				Recibir_Y_Actualizar_PCB();

				LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");
				log_info(Kernel_Logger, "PID: [%d] - Bloqueado por: %s", g_EXEC->PID, Recurso);

				sem_wait(&m_EXEC);
				sem_wait(&m_BLOCKED_RECURSOS);
				g_EXEC->recursoBloqueante = Recurso;
				list_add(g_Lista_BLOCKED_RECURSOS, g_EXEC);
				g_EXEC = NULL;
				sem_post(&m_EXEC);
				sem_post(&m_BLOCKED_RECURSOS);
			}
			//si hay recursos disponibles
			else
			{
				EnviarMensage("ACEPTADO", SocketCPU);

				//Recibir respuesta de CPU
				char* rta = (char*) recibir_paquete(SocketCPU);
				//printf("Respuesta de CPU: %s\n", rta);
				
				RealizarRespuestaDelCPU(rta);

			}
		}
		free(msg);
		free(Recurso);
	}

	else if(strcmp(respuesta, "SIGNAL\n")== 0)
	{
		char* msg = (char*) recibir_paquete(SocketCPU);
		char* Recurso = strtok(msg,"\n");
		
		bool cont = true;
		int aux = 0;
		int pos = -1;

		while (cont)
		{
			if(RECURSOS[aux] == NULL)
				cont = false;
			else if(strcmp(RECURSOS[aux], Recurso) == 0)
			{
				pos = aux;
				cont = false;
			}
			else
				aux++;
		}

		//Si libero un recurso que no existe -> Terminar el proceso
		if(pos == -1)
		{
			EnviarMensage("RECHAZADO", SocketCPU);
			
			Recibir_Y_Actualizar_PCB();

			LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
			TerminarProceso(g_EXEC, "SEG_FAULT");

			//Agregar PCB a la cola de EXIT
			sem_wait(&m_EXEC);
			sem_wait(&m_EXIT);
			list_add(g_Lista_EXIT, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_EXIT);

			sem_post(&c_MultiProg);
		}
		//Si existe el recurso
		else
		{
			//busco algun proceso que este esperando que el recurso se libere
			if(list_size(g_Lista_BLOCKED_RECURSOS) > 0)
			{
				int i = 0;

				while (true)
				{
					//si no hay ninguno esperandolo,
					//sumo uno a la cantidad de instancias disponibles de ese recurso
					if(i >= list_size(g_Lista_BLOCKED_RECURSOS))
					{
						sem_wait(&m_RECURSOS);
						int aux = atoi(G_INSTANCIAS_RECURSOS[pos]) + 1;
						sprintf(G_INSTANCIAS_RECURSOS[pos], "%d", aux);
						sem_post(&m_RECURSOS);

						break;
					}
					
					t_PCB* PCB = list_get(g_Lista_BLOCKED_RECURSOS, i);

					//si hay uno esperando el recurso, lo paso a ready y dejo de buscar
					if(strcmp(PCB->recursoBloqueante, Recurso) == 0)
					{
						LoguearCambioDeEstado(PCB, "BLOQUEADO", "READY");

						sem_wait(&m_READY);
						sem_wait(&m_BLOCKED_RECURSOS);
						AgregarAReady(PCB);
						list_remove(g_Lista_BLOCKED_RECURSOS, i);
						sem_post(&m_READY);
						sem_post(&m_BLOCKED_RECURSOS);

						break;
					}
					
				}
			}
			else
			{
				sem_wait(&m_RECURSOS);
				int aux = atoi(G_INSTANCIAS_RECURSOS[pos]) + 1;
				sprintf(G_INSTANCIAS_RECURSOS[pos], "%d", aux);
				sem_post(&m_RECURSOS);	
			}

			EnviarMensage("ACEPTADO", SocketCPU);
			
			log_info(Kernel_Logger, "PID: [%d] - Signal: %s - Instancias: %d", g_EXEC->PID, Recurso, atoi(G_INSTANCIAS_RECURSOS[pos]) + 1);
			
			//Recibir respuesta de CPU
			char* rta = (char*) recibir_paquete(SocketCPU);
			
			RealizarRespuestaDelCPU(rta);
			free(rta);
		}
		free(msg);
		free(Recurso);
	}

	else if(strcmp(respuesta, "CREATE_SEGMENT")== 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);

		char* Mensage = malloc(100);
		sprintf(Mensage, "%s %s\0", respuesta , Parametros);
		EnviarMensage(Mensage, SocketMemoria);
		free(Mensage);

		char* msg = (char*)recibir_paquete(SocketMemoria);
		char* RespuestaMemoria = strtok(msg, " ");

		if (strcmp(RespuestaMemoria, "OUT_OF_MEMORY") == 0)
		{
			EnviarMensage("RECHAZADO", SocketCPU);

			Recibir_Y_Actualizar_PCB();
			LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
			TerminarProceso(g_EXEC, "OUT_OF_MEMORY");

			//Agregar PCB a la cola de EXIT
			sem_wait(&m_EXEC);
			sem_wait(&m_EXIT);
			list_add(g_Lista_EXIT, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_EXIT);

			sem_post(&c_MultiProg);
		}
		else 
		{
			if (strcmp(RespuestaMemoria, "COMPACTAR") == 0)
			{
				log_info(Kernel_Logger, "Compactación: Se solicitó compactación");
				char* tipo = strtok(NULL, " ");

				//Si FS y memoria estan ocupados, espero...
				if (strcmp(tipo, "OCUPADO") == 0)
				{
					log_info(Kernel_Logger, "Compactación: Esperando Fin de Operaciones de FS");
					//ESPERO UN MENSAJE PARA SABER QUE TERMINO LA OPERACION DE FS y MEMORIA
					recibir_paquete(SocketMemoria);
					log_info(Kernel_Logger, "Compactación: Se solicitó compactación");
				}
				
				//ESPERO AVISO DE QUE FINALIZO LA COMPACTACION
				recibir_paquete(SocketMemoria);
			}

			//saco el PID que no me interesa
			strtok(Parametros, " ");
			//obrengo los valores utiles
			int ID = atoi(strtok(NULL, " "));
			int Tamanio = atoi(strtok(NULL, " "));

			//creo el segmento en la tabla de segmentos
			CrearSegmento(g_EXEC->tablaDeSegmentos, ID , Tamanio);

			//envio la tabla de segmentos actualizada al CPU para que pueda seguir ejecutando
			EnviarMensage("ACEPTADO", SocketCPU);
			t_paquete* NuevaTabla = AgregarTablaSegmentosAlPaquete(NULL, g_EXEC->tablaDeSegmentos);
			enviar_paquete(NuevaTabla, SocketCPU);
			eliminar_paquete(NuevaTabla);
		}
		free(msg);
	}
	
	else if(strcmp(respuesta, "DELETE_SEGMENT")== 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);

		char* Mensage = malloc(100);
		sprintf(Mensage, "%s %s\0", respuesta , Parametros);
		EnviarMensage(Mensage, SocketMemoria);
		free(Mensage);

		char* RespuestaMemoria = (char*)recibir_paquete(SocketMemoria);

		if(strcmp(RespuestaMemoria, "SEG_FAULT")== 0)
		{
			EnviarMensage("RECHAZADO", SocketCPU);

			Recibir_Y_Actualizar_PCB();
			LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
			TerminarProceso(g_EXEC, "SEG_FAULT");

			//Agregar PCB a la cola de EXIT
			sem_wait(&m_EXEC);
			sem_wait(&m_EXIT);
			list_add(g_Lista_EXIT, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_EXIT);

			sem_post(&c_MultiProg);
		}
		else
		{

			EnviarMensage("ACEPTADO", SocketCPU);

			//saco el PID que no me interesa
			strtok(Parametros, " ");

			//obrengo los valores utiles
			int ID = atoi(strtok(NULL, " "));

			EliminarSegmento(g_EXEC->tablaDeSegmentos, ID);

			//envio la tabla de segmentos actualizada al CPU para que pueda seguir ejecutando
			t_paquete* NuevaTabla = AgregarTablaSegmentosAlPaquete(NULL, g_EXEC->tablaDeSegmentos);
			enviar_paquete(NuevaTabla, SocketCPU);
			eliminar_paquete(NuevaTabla);
		}

	}

	else if(strcmp(respuesta, "SEG_FAULT")== 0)
	{
		Recibir_Y_Actualizar_PCB();
		LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
		TerminarProceso(g_EXEC, "SEG_FAULT");

		//Agregar PCB a la cola de EXIT
		sem_wait(&m_EXEC);
		sem_wait(&m_EXIT);
		list_add(g_Lista_EXIT, g_EXEC);
		g_EXEC = NULL;
		sem_post(&m_EXEC);
		sem_post(&m_EXIT);

		sem_post(&c_MultiProg);
	}
	
	else if(strcmp(respuesta, "F_OPEN")== 0)
	{
		char* NombreArchivo = (char*)recibir_paquete(SocketCPU);
		
		t_ArchivoGlobal* ArchivoBuscado = BuscarEnTablaGlobal(NombreArchivo);

		//agrego el archivo a la tabla de archivos abiertos del pcb
		t_ArchivoPCB* ArchivoPCB = malloc(sizeof(t_ArchivoPCB));
		ArchivoPCB->NombreArchivo = malloc(strlen(NombreArchivo) + 1);
		strcpy(ArchivoPCB->NombreArchivo, NombreArchivo);
		ArchivoPCB->PosicionPuntero = 0;

		list_add(g_EXEC->tablaArchivosAbiertos, ArchivoPCB);

		//si el archivo buscado existe, entonces alguien lo tiene abierto
		if(ArchivoBuscado != NULL)
		{
			//bloqueo el proceso hasta que el otro proceso termine de usar el archivo			
			EnviarMensage("EN_USO", SocketCPU);
			Recibir_Y_Actualizar_PCB();

			LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");

			sem_wait(&m_EXEC);
			list_add(ArchivoBuscado->ProcesosBloqueados, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);			
		}
		//si no existe...
		else
		{
			//le pido al FS que me diga si el archivo existe en FS
			char* Mensage = malloc(30);
			sprintf(Mensage, "ABRIR_ARCHIVO %s\0", NombreArchivo);
			EnviarMensage(Mensage, SocketFileSystem);
			free(Mensage);

			char* RespuestaFS = RecibirDeFS();
			//(char*)recibir_paquete(SocketFileSystem);

			//si el archivo no existe en el FS, le pido que lo cree
			if(strcmp(RespuestaFS, "OK") != 0)
			{
				char* Mensage = malloc(30);
				sprintf(Mensage, "CREAR_ARCHIVO %s\0", NombreArchivo);
				EnviarMensage(Mensage, SocketFileSystem);
				free(Mensage);

				free(RespuestaFS);
				RespuestaFS = RecibirDeFS();
				free(RespuestaFS);
			}

			//creo el archivo en la tabla global
			ArchivoBuscado = malloc(sizeof(t_ArchivoGlobal));
			ArchivoBuscado->NombreArchivo = malloc(strlen(NombreArchivo) + 1);
			strcpy(ArchivoBuscado->NombreArchivo, NombreArchivo);
			ArchivoBuscado->ProcesosBloqueados = list_create();

			list_add(TablaGlobalArchivosAbiertos, ArchivoBuscado);

			//envio el mensaje de aceptacion
			EnviarMensage("OK", SocketCPU);
		}

	}

	else if(strcmp(respuesta, "F_CLOSE")== 0)
	{
		char* NombreArchivo = (char*)recibir_paquete(SocketCPU);


		int indice = BuscarArchivoEnTablaDeProceso(g_EXEC->tablaArchivosAbiertos, NombreArchivo);
		
		//lo saco de la tabla de archoivos abiertos del proceso
		if(indice == -1)
		{
			//error el archivo no existe
			ErrorArchivoInexistente(true);
		}
		else
		{
			t_ArchivoPCB* ArchivoALiberar = (t_ArchivoPCB*) list_remove(g_EXEC->tablaArchivosAbiertos, indice);
			free(ArchivoALiberar->NombreArchivo);
			free(ArchivoALiberar);

			t_ArchivoGlobal* ArchivoBuscado = BuscarEnTablaGlobal(NombreArchivo);

			//si no hay nadie esperando por usar el archivo, libero la memoria
			if(list_is_empty(ArchivoBuscado->ProcesosBloqueados))
			{
				free(ArchivoBuscado->NombreArchivo);
				free(ArchivoBuscado);
			}
			//si hay alguien esperando, lo mando a ready
			else
			{
				t_PCB* PCB = (t_PCB*) list_remove(ArchivoBuscado->ProcesosBloqueados, 0);
				sem_wait(&m_READY);
				AgregarAReady(PCB);
				list_add(g_Lista_READY, PCB);
				sem_post(&m_READY);
			}
			
			EnviarMensage("OK", SocketCPU);
		}
	}

	else if(strcmp(respuesta, "F_SEEK")== 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);
		char* NombreArchivo = strtok(Parametros, " ");
		int NuevaPosicionPuntero = atoi(strtok(NULL, " "));

		int indice = BuscarArchivoEnTablaDeProceso(g_EXEC->tablaArchivosAbiertos, NombreArchivo);

		if(indice == -1)
		{
			//error el archivo no existe
			ErrorArchivoInexistente(true);
		}
		else
		{
			t_ArchivoPCB* ArchivoPCB = (t_ArchivoPCB*) list_remove(g_EXEC->tablaArchivosAbiertos, indice);
			ArchivoPCB->PosicionPuntero = NuevaPosicionPuntero;
			EnviarMensage("OK", SocketCPU);
		}
	}
	
	else if(strcmp(respuesta, "F_TRUNCATE")== 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);

		char* NombreArchivo = strtok(Parametros, " ");
		char* NuevoTamanoArchivo = strtok(NULL, " ");

		int indice = BuscarArchivoEnTablaDeProceso(g_EXEC->tablaArchivosAbiertos, NombreArchivo);

		if(indice == -1)
		{
			//error el archivo no existe
			ErrorArchivoInexistente(false);
		}
		else
		{
			char* Mensage = malloc(60);
			sprintf(Mensage, "TRUNCAR_ARCHIVO %s %s\0", NombreArchivo, NuevoTamanoArchivo);
			EnviarMensage(Mensage, SocketFileSystem);
			free(Mensage);

			Recibir_Y_Actualizar_PCB();
			LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");

			sem_wait(&m_EXEC);
			sem_wait(&m_BLOCKED_FS);
			list_add(g_Lista_BLOCKED_FS, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_BLOCKED_FS);
		}
	}
	
	else if(strcmp(respuesta, "F_READ")== 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);

		char* NombreArchivo = strtok(Parametros, " ");
		char* DireccionFisica = strtok(NULL, " ");
		char* CantBytesALeer = strtok(NULL, " ");
		int PosicionPuntero;

		int indice = BuscarArchivoEnTablaDeProceso(g_EXEC->tablaArchivosAbiertos, NombreArchivo);

		if(indice == -1)
		{
			//error el archivo no existe
			ErrorArchivoInexistente(false);
		}
		else
		{
			t_ArchivoPCB* ArchivoPCB = (t_ArchivoPCB*) list_get(g_EXEC->tablaArchivosAbiertos, indice);
			PosicionPuntero = ArchivoPCB->PosicionPuntero;
			
			char* Mensage = malloc(60);
			sprintf(Mensage, "LEER_ARCHIVO %s %s %s %d\0", NombreArchivo, DireccionFisica, CantBytesALeer, PosicionPuntero);
			EnviarMensage(Mensage, SocketFileSystem);
			free(Mensage);

			Recibir_Y_Actualizar_PCB();
			LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");

			sem_wait(&m_EXEC);
			sem_wait(&m_BLOCKED_FS);
			list_add(g_Lista_BLOCKED_FS, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_BLOCKED_FS);
		}
	}
	
	else if(strcmp(respuesta, "F_WRITE") == 0)
	{
		char* Parametros = (char*)recibir_paquete(SocketCPU);

		char* NombreArchivo = strtok(Parametros, " ");
		char* DireccionFisica = strtok(NULL, " ");
		char* CantBytesALeer = strtok(NULL, " ");
		int PosicionPuntero;

		int indice = BuscarArchivoEnTablaDeProceso(g_EXEC->tablaArchivosAbiertos, NombreArchivo);

		if(indice == -1)
		{
			//error el archivo no existe
			ErrorArchivoInexistente(false);
		}
		else
		{
			t_ArchivoPCB* ArchivoPCB = (t_ArchivoPCB*) list_get(g_EXEC->tablaArchivosAbiertos, indice);
			PosicionPuntero = ArchivoPCB->PosicionPuntero;
			
			char* Mensage = malloc(60);
			sprintf(Mensage, "ESCRIBIR_ARCHIVO %s %s %s %d\0", NombreArchivo, DireccionFisica, CantBytesALeer, PosicionPuntero);
			EnviarMensage(Mensage, SocketFileSystem);
			free(Mensage);

			Recibir_Y_Actualizar_PCB();
			LoguearCambioDeEstado(g_EXEC, "EXEC", "BLOCKED");

			sem_wait(&m_EXEC);
			sem_wait(&m_BLOCKED_FS);
			list_add(g_Lista_BLOCKED_FS, g_EXEC);
			g_EXEC = NULL;
			sem_post(&m_EXEC);
			sem_post(&m_BLOCKED_FS);
		}
	}	
}

void DesbloquearPorFS()
{
	sem_wait(&m_READY);
	sem_wait(&m_BLOCKED_FS);
	t_PCB* PCB = (t_PCB*) list_remove(g_Lista_BLOCKED_FS, 0);
	AgregarAReady(PCB);
	list_add(g_Lista_READY, PCB);
	sem_post(&m_READY);
	sem_post(&m_BLOCKED_FS);
}

char* RecibirDeFS()
{
	char* MensageFS = (char*)recibir_paquete(SocketFileSystem);
	
	if(strcmp(MensageFS, "TERMINO") == 0)
	{
		DesbloquearPorFS();
		return RecibirDeFS();
	}
	else
	{
		return MensageFS;
	}
}

void ErrorArchivoInexistente(bool RequiereEnviarMensage)
{
	if(RequiereEnviarMensage)
		EnviarMensage("ERROR", SocketCPU);

	Recibir_Y_Actualizar_PCB();
	LoguearCambioDeEstado(g_EXEC, "EXEC", "EXIT");
	TerminarProceso(g_EXEC, "ERROR_ARCHIVO_INEXISTENTE");

	//Agregar PCB a la cola de EXIT
	sem_wait(&m_EXEC);
	sem_wait(&m_EXIT);
	list_add(g_Lista_EXIT, g_EXEC);
	g_EXEC = NULL;
	sem_post(&m_EXEC);
	sem_post(&m_EXIT);

	sem_post(&c_MultiProg);
}


void* EsperarEntradaSalida(void* arg)
{
	//recibo los argumentos
	t_list* ListaAux = (t_list*) arg;
	t_PCB* PCB_aux = (t_PCB*) list_get(ListaAux, 0);
	int Tiempo = (int)(intptr_t)list_get(ListaAux, 1);

	//Simulo la espera de la entrada/salida
	sleep(Tiempo);

	LoguearCambioDeEstado(PCB_aux, "BLOQUEADO", "READY");
	//cuando termina el tiempo agrego el PCB a la cola de READY
	sem_wait(&m_READY);
	AgregarAReady(PCB_aux);
	sem_post(&m_READY);

	list_destroy(ListaAux);
	return (void*)0;
}

//Conecta con los modulos (CPU, FS, Mem) y luego crea un Hilo que escucha conexiones de consolas
int InicializarConexiones()
{
	//Conectar con los modulos
	SocketCPU = conectar_servidor(Kernel_Logger, "CPU", IP_CPU, PUERTO_CPU);
	SocketMemoria = conectar_servidor(Kernel_Logger, "FileSystem", IP_MEMORIA, PUERTO_MEMORIA);
	SocketFileSystem = conectar_servidor(Kernel_Logger, "Memoria", IP_FILESYSTEM, PUERTO_FILESYSTEM);

	//Crear hilo escucha
    if (pthread_create(&HiloEscucha, NULL, EscucharConexiones, NULL) != 0) {
		log_error(Kernel_Logger, "Error creando hilo escucha\n");
        return 0;
    }

	return 1;
}

//Inicia un servidor en el que escucha consolas permanentemente y crea un hilo que la administre cuando recibe una
void* EscucharConexiones()
{
	SocketKernel = iniciar_servidor(Kernel_Logger, NOMBRE_PROCESO, "0.0.0.0", PUERTO_ESCUCHA);
	
	if(SocketKernel != 0)
	{
		//Escuchar por consolas en bucle
		while(!ModuloDebeTerminar)
		{
			int SocketCliente = esperar_cliente(Kernel_Logger, NOMBRE_PROCESO, SocketKernel);
			
			if(SocketCliente != 0)
			{	
				//Crea un hilo para la consola conectada
				if (pthread_create(&HiloAdministradorDeMensajes, NULL, AdministradorDeModulo, (void*)&SocketCliente) != 0) {
					exit(EXIT_FAILURE);
				}
				pthread_detach(HiloAdministradorDeMensajes);
			}
		}
	}
	log_error(Kernel_Logger, "error al crear socket kernel\n");
	liberar_conexion(SocketKernel);
	return NULL;
}

//Funcion que se ejecuta para cada consola cuando se conecta
void* AdministradorDeModulo(void* arg)
{
	int* SocketClienteConectado = (int*)arg;
	 
	t_list* instruccionesRecibidas = (t_list*)recibir_paquete(*SocketClienteConectado);

	t_PCB* PCBConsola = CrearPCB(instruccionesRecibidas, *SocketClienteConectado);

	//Verificar si el grado de multiprogramacion lo permite, si lo permite, pasar a ready directamente
	if(sem_trywait(&c_MultiProg) == 0)
	{
		LoguearCambioDeEstado(PCBConsola, "NEW", "READY");
		sem_wait(&m_READY);
		AgregarAReady(PCBConsola);		
		sem_post(&m_READY);
	}
	//si no, pasar a new
	else
	{
		sem_wait(&m_NEW);
		list_add(g_Lista_NEW, PCBConsola);
		sem_post(&m_NEW);
	}
	return NULL;
}

//Crea un PCB con los datos recibidos
t_PCB* CrearPCB(t_list* instrucciones, int socketConsola)
{
	t_PCB* pcb = malloc(sizeof(t_PCB));

	sem_wait(&m_PIDCounter);
	pcb->PID = g_PIDCounter;
	g_PIDCounter++;
	sem_post(&m_PIDCounter);

	pcb->registrosCPU = CrearRegistrosCPU();
	pcb->programCounter = 0;
	pcb->socketConsolaAsociada = socketConsola;
	pcb->listaInstrucciones = instrucciones;
	pcb->recursoBloqueante = NULL;

	//inicializar la tabla de segmentos
	pcb->tablaDeSegmentos = list_create();

	//pedir a memoria que cree las estructuras necesarias para el proceso
	char* Msg = malloc(23);
	sprintf(Msg, "CREAR_PROCESO %d", pcb->PID);
	EnviarMensage(Msg, SocketMemoria);
	free(Msg);

	pcb->tablaArchivosAbiertos = list_create();

	log_info(Kernel_Logger, "Se crea el proceso PID: [%d] en NEW", pcb->PID);

	return pcb;
}

//recibe el paquete conteniendo el nuevo pcb y lo actualiza
//!!SOLO ACTUALIZA EL PCB DEL PROCESO EN EJECUCION!!
void Recibir_Y_Actualizar_PCB()
{
	t_list* PCB_A_Actualizar = (t_list*)recibir_paquete(SocketCPU);

	sem_wait(&m_EXEC);
	int* PC = (int*) list_remove(PCB_A_Actualizar, 0);
	g_EXEC->programCounter = *PC;
	free(PC);

	free(g_EXEC->registrosCPU);
	g_EXEC->registrosCPU = ObtenerRegistrosDelPaquete(PCB_A_Actualizar);

	double* tiempoUltimaRafaga = (double*) list_remove(PCB_A_Actualizar, 0);
	g_EXEC->tiempoUltimaRafaga = *tiempoUltimaRafaga;
	free(tiempoUltimaRafaga);

	sem_post(&m_EXEC);

	list_destroy(PCB_A_Actualizar);
}

t_registrosCPU* CrearRegistrosCPU()
{
	t_registrosCPU* registrosCPU = malloc(sizeof(t_registrosCPU));
	
	strncpy(registrosCPU->AX, "\0\0\0\0", sizeof(registrosCPU->AX));
	strncpy(registrosCPU->BX, "\0\0\0\0", sizeof(registrosCPU->BX));
	strncpy(registrosCPU->CX, "\0\0\0\0", sizeof(registrosCPU->CX));
	strncpy(registrosCPU->DX, "\0\0\0\0", sizeof(registrosCPU->DX));

	strncpy(registrosCPU->EAX, "\0\0\0\0\0\0\0\0", sizeof(registrosCPU->EAX));
	strncpy(registrosCPU->EBX, "\0\0\0\0\0\0\0\0", sizeof(registrosCPU->EBX));
	strncpy(registrosCPU->ECX, "\0\0\0\0\0\0\0\0", sizeof(registrosCPU->ECX));
	strncpy(registrosCPU->EDX, "\0\0\0\0\0\0\0\0", sizeof(registrosCPU->EDX));
	
	strncpy(registrosCPU->RAX, "\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(registrosCPU->RAX));
	strncpy(registrosCPU->RBX, "\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(registrosCPU->RBX));
	strncpy(registrosCPU->RCX, "\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(registrosCPU->RCX));
	strncpy(registrosCPU->RDX, "\0\0\0\0\0\0\0\0\0\0\0\0", sizeof(registrosCPU->RDX));

	return registrosCPU;
}

t_registrosCPU* ObtenerRegistrosDelPaquete(t_list* Lista)
{
	t_registrosCPU* Registros = malloc(sizeof(t_registrosCPU));

	char* AX = (char*)list_remove(Lista, 0);
	char* BX = (char*)list_remove(Lista, 0);
	char* CX = (char*)list_remove(Lista, 0);
	char* DX = (char*)list_remove(Lista, 0);

	char* EAX = (char*)list_remove(Lista, 0);
	char* EBX = (char*)list_remove(Lista, 0);
	char* ECX = (char*)list_remove(Lista, 0);
	char* EDX = (char*)list_remove(Lista, 0);

	char* RAX = (char*)list_remove(Lista, 0);
	char* RBX = (char*)list_remove(Lista, 0);
	char* RCX = (char*)list_remove(Lista, 0);
	char* RDX = (char*)list_remove(Lista, 0);

	strncpy(Registros->AX, AX, sizeof(Registros->AX));
	strncpy(Registros->BX, BX, sizeof(Registros->BX));
	strncpy(Registros->CX, CX, sizeof(Registros->CX));
	strncpy(Registros->DX, DX, sizeof(Registros->DX));

	strncpy(Registros->EAX, EAX, sizeof(Registros->EAX));
	strncpy(Registros->EBX, EBX, sizeof(Registros->EBX));
	strncpy(Registros->ECX, ECX, sizeof(Registros->ECX));
	strncpy(Registros->EDX, EDX, sizeof(Registros->EDX));

	strncpy(Registros->RAX, RAX, sizeof(Registros->RAX));
	strncpy(Registros->RBX, RBX, sizeof(Registros->RBX));
	strncpy(Registros->RCX, RCX, sizeof(Registros->RCX));
	strncpy(Registros->RDX, RDX, sizeof(Registros->RDX));

	free(AX);
	free(BX);
	free(CX);
	free(DX);
	free(EAX);
	free(EBX);
	free(ECX);
	free(EDX);
	free(RAX);
	free(RBX);
	free(RCX);
	free(RDX);

	return Registros;
}

void ImprimirRegistrosPCB(t_PCB* PCB_Registos_A_Imprimir)
{
	printf("Valores de los registros del proceso PID: %d\n", PCB_Registos_A_Imprimir->PID);
	printf("Valor de AX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->AX), PCB_Registos_A_Imprimir->registrosCPU->AX);
	printf("Valor de BX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->BX), PCB_Registos_A_Imprimir->registrosCPU->BX);
	printf("Valor de CX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->CX), PCB_Registos_A_Imprimir->registrosCPU->CX);
	printf("Valor de DX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->DX), PCB_Registos_A_Imprimir->registrosCPU->DX);
	printf("Valor de EAX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->EAX), PCB_Registos_A_Imprimir->registrosCPU->EAX);
	printf("Valor de EBX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->EBX), PCB_Registos_A_Imprimir->registrosCPU->EBX);
	printf("Valor de ECX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->ECX), PCB_Registos_A_Imprimir->registrosCPU->ECX);
	printf("Valor de EDX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->EDX), PCB_Registos_A_Imprimir->registrosCPU->EDX);
	printf("Valor de RAX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->RAX), PCB_Registos_A_Imprimir->registrosCPU->RAX);
	printf("Valor de RBX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->RBX), PCB_Registos_A_Imprimir->registrosCPU->RBX);
	printf("Valor de RCX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->RCX), PCB_Registos_A_Imprimir->registrosCPU->RCX);
	printf("Valor de RDX: %.*s\n", (int) sizeof(PCB_Registos_A_Imprimir->registrosCPU->RDX), PCB_Registos_A_Imprimir->registrosCPU->RDX);
}

//Funcion que se ejecuta al recibir una señal de finalizacion de programa (Ctrl+C)
void TerminarModulo()
{
	log_destroy(Kernel_Logger);

	config_destroy(config);

	sem_destroy(&m_PIDCounter);
	sem_destroy(&c_MultiProg);
	sem_destroy(&m_NEW);
	sem_destroy(&m_READY);
	sem_destroy(&m_EXEC);
	sem_destroy(&m_EXIT);
	sem_destroy(&m_BLOCKED);

	bool cont = true;
	int aux = 0;
	while (cont)
	{
		if(RECURSOS[aux] == NULL)
		{
			cont = false;
		}
		else
		{
			free(RECURSOS[aux]);
			free(G_INSTANCIAS_RECURSOS[aux]);
			aux++;
		}
	}
	free(RECURSOS);
	free(G_INSTANCIAS_RECURSOS);

	LimpiarListaDePCBs(g_Lista_NEW);
	LimpiarListaDePCBs(g_Lista_READY);
	LimpiarListaDePCBs(g_Lista_EXIT);
	LimpiarListaDePCBs(g_Lista_BLOCKED);
	LimpiarListaDePCBs(g_Lista_BLOCKED_RECURSOS);
	LimpiarListaDePCBs(g_Lista_BLOCKED_FS);
	LimpiarPCB(g_EXEC);

	LimpiarTablaGlobalArchivosAbiertos();
	
	liberar_conexion(SocketCPU);
	liberar_conexion(SocketMemoria);
	liberar_conexion(SocketFileSystem);
	liberar_conexion(SocketKernel);

	exit(EXIT_SUCCESS);
}

void LimpiarListaDePCBs(t_list* lista)
{
	while(!list_is_empty(lista))
	{
		t_PCB* PCB_A_Liberar = list_remove(lista, 0);
		LimpiarPCB(PCB_A_Liberar);
	}
	list_destroy(lista);
}

void LimpiarElementosDeTabla(t_list* tabla)
{
	while (!list_is_empty(tabla))
	{
		void* elemtnto = list_remove(tabla, 0);
		free(elemtnto);
	}
	list_destroy(tabla);
}

void LimpiarPCB(t_PCB* PCB_A_Liberar)
{
	if(PCB_A_Liberar == NULL)
		return;
	LimpiarElementosDeTabla(PCB_A_Liberar->tablaDeSegmentos);
	LimpiarElementosDeTabla(PCB_A_Liberar->listaInstrucciones);
	LimpiarTablaDeArchivosDelProceso(PCB_A_Liberar->tablaArchivosAbiertos);
	liberar_conexion(PCB_A_Liberar->socketConsolaAsociada);
	free(PCB_A_Liberar->registrosCPU);
	if(PCB_A_Liberar->recursoBloqueante != NULL)
		free(PCB_A_Liberar->recursoBloqueante);

	free(PCB_A_Liberar);
}

void LimpiarTablaGlobalArchivosAbiertos()
{
	while(!list_is_empty(TablaGlobalArchivosAbiertos))
	{
		t_ArchivoGlobal* Archivo = list_remove(TablaGlobalArchivosAbiertos, 0);
		free(Archivo->NombreArchivo);
		free(Archivo);
	}
	list_destroy(TablaGlobalArchivosAbiertos);
}

void LimpiarTablaDeArchivosDelProceso(t_list* Tabla)
{
	while(!list_is_empty(Tabla))
	{
		t_ArchivoPCB* Archivo = list_remove(Tabla, 0);
		free(Archivo->NombreArchivo);
		free(Archivo);
	}
	list_destroy(Tabla);
}


t_ArchivoGlobal* BuscarEnTablaGlobal(char* NombreArchivo)
{	
	t_ArchivoGlobal* ArchivoBuscado = NULL;

	for(int i = 0; i < list_size(TablaGlobalArchivosAbiertos); i++)
	{
		t_ArchivoGlobal* Archivo = (t_ArchivoGlobal*)list_get(TablaGlobalArchivosAbiertos, i);
		if(strcmp(Archivo->NombreArchivo, NombreArchivo) == 0)
		{
			ArchivoBuscado = Archivo;
			break;
		}
	}
	return ArchivoBuscado;
}

int BuscarArchivoEnTablaDeProceso(t_list* Tabla, char* NombreArchivo)
{	
	for(int i = 0; i < list_size(Tabla); i++)
	{
		t_ArchivoPCB* Archivo = (t_ArchivoPCB*)list_get(Tabla, i);
		if(strcmp(Archivo->NombreArchivo, NombreArchivo) == 0)
		{
			return i;	
		}
	}
	return -1;
}