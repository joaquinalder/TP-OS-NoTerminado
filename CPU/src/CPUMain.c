#include "../include/CPUMain.h"


t_log* CPU_Logger;

int SocketCPU;
int SocketMemoria;
int SocketKernel;
time_t tiempoInicio;
time_t tiempoFinal;
pthread_t HiloEscucha;

void sighandler(int s)
{
	ModuloDebeTerminar = true;

	pthread_cancel(HiloEscucha);

	log_destroy(CPU_Logger);

	config_destroy(config);

	liberar_conexion(SocketKernel);
	liberar_conexion(SocketCPU);	
	liberar_conexion(SocketMemoria);
	
	exit(0);
}


int main(int argc, char* argv[])
{
	signal(SIGINT, sighandler);
	signal(SIGSEGV, sighandler);

	LeerConfigs(argv[1]);

	CPU_Logger = log_create("CPU.log", NOMBRE_PROCESO, true, LOG_LEVEL_INFO);
	
	InicializarConexiones();

	while(true){
		log_info(CPU_Logger, "hilo principal esta ejecutando");
		sleep(10);
	}

	return EXIT_SUCCESS;
}

//Crea un hilo de escucha para el kernel
int InicializarConexiones()
{
	SocketMemoria = conectar_servidor(CPU_Logger, "Memoria", IP_MEMORIA, PUERTO_MEMORIA);

    if (pthread_create(&HiloEscucha, NULL, EscuchaKernel, NULL) != 0) {
        exit(EXIT_FAILURE);
    }
	//pthread_detach(HiloEscucha);
	return 1;
}


//Crea un servidor y espera al kernel, luego recibe mensajes del mismo
void* EscuchaKernel()
{
	printf("HILO ESCUCHA KERNEL\n");
	SocketCPU = iniciar_servidor(CPU_Logger, NOMBRE_PROCESO, "0.0.0.0", PUERTO_ESCUCHA);
	if(SocketCPU == 0)
	{
		liberar_conexion(SocketCPU);
		return (void*)EXIT_FAILURE;
	}
	SocketKernel = esperar_cliente(CPU_Logger, NOMBRE_PROCESO, SocketCPU);

	if(SocketKernel == 0)
	{
		liberar_conexion(SocketCPU);
		return (void*)EXIT_FAILURE;
	}

	while(!ModuloDebeTerminar)
	{
		bool SeguirEjecutando = true;
		//recive una lista con todos los datos del PCB
		t_list* DatosRecibidos = (t_list*)recibir_paquete(SocketKernel);

		//guarda el tiempo en que empezo a ejecutar para calcular el tiempo de ejecucion mas tarde
		time(&tiempoInicio);

		int* aux = (int*)list_remove(DatosRecibidos, 0);
		int PC = *aux;
		free(aux);

		aux = (int*)list_remove(DatosRecibidos, 0);
		int PID = *aux;
		free(aux);
		
		//guarda los registros en una estructura
		t_registrosCPU* Registros = ObtenerRegistrosDelPaquete(DatosRecibidos);

		t_list* TablaSegmentos = ObtenerTablaSegmentosDelPaquete(DatosRecibidos);

		//loopea por las instrucciones y las realiza una por una hasta que alguna requiera desalojar
		while (SeguirEjecutando)
		{
			//obtiene la instruccion a ejecutar
			char* Linea_A_Ejecutar = (char*)list_get(DatosRecibidos, PC);
			
			log_info(CPU_Logger, "PID: [%d] - Ejecutando: %s", PID, Linea_A_Ejecutar);

			//divide la linea en instruccion y parametros
			char* Instruccion_A_Ejecutar = strtok(Linea_A_Ejecutar, " ");

			if(strcmp(Instruccion_A_Ejecutar, "YIELD\n")==0)
			{
				PC++;
				EnviarMensage("YIELD\n", SocketKernel);
				Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
				SeguirEjecutando = false;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "SET")==0)
			{
				PC++;
				
				//aplicar el retardo de instruccion al hacer el set
				//Los milisegundos indicados en las config
				struct timespec tiempoespera;
				tiempoespera.tv_sec = RETARDO_INSTRUCCION / 1000;
				tiempoespera.tv_nsec = (RETARDO_INSTRUCCION % 1000) * 1000000;
				nanosleep(&tiempoespera, NULL);

				//obtengo los parametros del SET
				char* Reg_A_Setear = strtok(NULL, " ");
				char* Valor_A_Setear = strtok(NULL, " ");
				
				//obtener el registro a modificar y setear el valor
				strncpy(ObtenerRegistro(Reg_A_Setear, Registros), Valor_A_Setear, strlen(Valor_A_Setear)-1);	
			}

			else if(strcmp(Instruccion_A_Ejecutar, "EXIT\n")==0)
			{
				PC++;
				EnviarMensage("EXIT\n", SocketKernel);
				Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
				SeguirEjecutando = false;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "I/O")==0)
			{
				PC++;
				
				char* Mensage = "I/O\n";
				char* Tiempo = strtok(NULL, " ");

				EnviarMensage(Mensage, SocketKernel);
				EnviarMensage(Tiempo, SocketKernel);
				Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
				SeguirEjecutando = false;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "WAIT")==0)
			{
				PC++;
				EnviarMensage("WAIT\n", SocketKernel);
				char* RecursoSolicitado = strtok(NULL, " ");
				EnviarMensage(RecursoSolicitado, SocketKernel);

				char* Respuesta = (char*) recibir_paquete(SocketKernel);

				if(strcmp(Respuesta, "RECHAZADO")==0)
				{
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
			}

			else if(strcmp(Instruccion_A_Ejecutar, "SIGNAL")==0)
			{
				PC++;
				EnviarMensage("SIGNAL\n", SocketKernel);
				char* RecursoLiberado = strtok(NULL, " ");
				EnviarMensage(RecursoLiberado, SocketKernel);

				char* Respuesta = (char*) recibir_paquete(SocketKernel);
				if(strcmp(Respuesta, "RECHAZADO")==0)
				{
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
			}

			else if(strcmp(Instruccion_A_Ejecutar, "MOV_IN")==0)
			{
				PC++;

				char* Reg_A_Setear = strtok(NULL, " ");
				char* DirLogica = strtok(strtok(NULL, " "), "\n");

				char* Registro = ObtenerRegistro(Reg_A_Setear, Registros);
				int tamano = ObtenerTamanoRegistro(Reg_A_Setear);
				
				int NumSegmento;
				int Offset;
				TraducirDireccion(DirLogica, &NumSegmento, &Offset);

				int TamSegmentoBuscado = ObtenerTamanoSegmento(NumSegmento, TablaSegmentos);

				//chequeo que la direccion donde se quiere leer sea valida
				if(Offset + tamano > TamSegmentoBuscado || TamSegmentoBuscado == -1)
				{
					EnviarMensage("SEG_FAULT", SocketKernel);
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
				//si es valida pido a memoria que me envie los datos
				else
				{
					char* Mensage = malloc(100);

					//si esta intentando leer del segmento 0 envio PID -1.
					if(NumSegmento == 0)
					{
						sprintf(Mensage,"MOV_IN -1 %d %d %d\0", NumSegmento, Offset, tamano);
					}
					//si no envio el PID del proceso
					else
					{
						sprintf(Mensage,"MOV_IN %d %d %d %d\0", PID, NumSegmento, Offset, tamano);
					}
					
					EnviarMensage(Mensage, SocketMemoria);
					free(Mensage);

					//en caso de error en la memoria avisar a kernel del error
					char* Respuesta = (char*) recibir_paquete(SocketMemoria);
					if(strcmp(Respuesta, "SEG_FAULT")==0)
					{
						EnviarMensage("SEG_FAULT", SocketKernel);
						Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
						SeguirEjecutando = false;
					}
					//si no, guardo el valor en el registro
					else
					{
						memcpy(Registro, Respuesta, tamano);
					}	
				}						
			}

			else if(strcmp(Instruccion_A_Ejecutar, "MOV_OUT")==0)
			{
				PC++;

				char* DirLogica = strtok(NULL, " ");
				char* Reg_A_Buscar = strtok(strtok(NULL, " "), "\n");
				
				int tamano = ObtenerTamanoRegistro(Reg_A_Buscar);
				char* Valor = malloc(tamano+1);

				strncpy(Valor, ObtenerRegistro(Reg_A_Buscar, Registros), tamano);
				Valor[tamano] = '\0';

				int NumSegmento;
				int Offset;
				TraducirDireccion(DirLogica, &NumSegmento, &Offset);

				int TamSegmentoBuscado = ObtenerTamanoSegmento(NumSegmento, TablaSegmentos);

				//chequeo que la direccion donde se quiere escribir sea valida
				if(Offset + tamano > TamSegmentoBuscado || TamSegmentoBuscado == -1)
				{
					EnviarMensage("SEG_FAULT", SocketKernel);
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
				//si es valida pido a memoria que me almacene los datos
				else
				{
					char* Mensage = malloc(30+tamano);

					//si esta intentando escribir en el segmento 0 envio PID -1.
					if(NumSegmento == 0)
					{
						sprintf(Mensage,"MOV_OUT -1 %d %d %s\0", NumSegmento, Offset, Valor);
					}
					else
					{
						sprintf(Mensage,"MOV_OUT %d %d %d %s\0", PID, NumSegmento, Offset, Valor);
					}
					EnviarMensage(Mensage, SocketMemoria);
					free(Mensage);
					free(Valor);

					char* Respuesta = (char*) recibir_paquete(SocketMemoria);

					//si hubo error en la memoria avisar a kernel del error
					if(strcmp(Respuesta, "SEG_FAULT")==0)
					{
						EnviarMensage("SEG_FAULT", SocketKernel);
						Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
						SeguirEjecutando = false;
					}//si no, no hacer nada.
				}	

			}
			
			else if(strcmp(Instruccion_A_Ejecutar, "CREATE_SEGMENT")==0)
			{
				PC++;

				char* IdSegmento = strtok(NULL, " ");
				char* Tamano = strtok(strtok(NULL, " "), "\n");

				if(atoi(Tamano) > TAM_MAX_SEGMENTO)
				{
					EnviarMensage("SEG_FAULT", SocketKernel);
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
				else
				{
					EnviarMensage("CREATE_SEGMENT", SocketKernel);

					char* Mensage = malloc(50);
					sprintf(Mensage,"%d %s %s\0", PID, IdSegmento, Tamano);
					EnviarMensage(Mensage, SocketKernel);
					free(Mensage);

					char* Respuesta = (char*) recibir_paquete(SocketKernel);
					if(strcmp(Respuesta, "RECHAZADO")==0)
					{
						Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
						SeguirEjecutando = false;
					}
					else
					{
						t_list* NuevaTabla = ObtenerTablaSegmentosDelPaquete((t_list*)recibir_paquete(SocketKernel));
						LimpiarElementosDeTabla(TablaSegmentos);
						TablaSegmentos = NuevaTabla;
					}
				}				
			}

			else if(strcmp(Instruccion_A_Ejecutar, "DELETE_SEGMENT")==0)
			{
				PC++;

				int IdSegmento = atoi(strtok(NULL, " "));

				int TamSegmentoBuscado = ObtenerTamanoSegmento(IdSegmento, TablaSegmentos);				//chequeo que el segmento exista
				
				//si el segmento no existe..
				if (TamSegmentoBuscado == -1)
				{
					EnviarMensage("SEG_FAULT", SocketKernel);
					Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
					SeguirEjecutando = false;
				}
				//si existe..
				else
				{
					EnviarMensage("DELETE_SEGMENT", SocketKernel);

					char* Mensage = malloc(50);
					sprintf(Mensage,"%d %d\0", PID, IdSegmento);
					EnviarMensage(Mensage, SocketKernel);
					free(Mensage);

					char* Respuesta = (char*) recibir_paquete(SocketKernel);
					if(strcmp(Respuesta, "RECHAZADO")==0)
					{
						Enviar_PCB_A_Kernel(PC, Registros, SocketKernel);
						SeguirEjecutando = false;
					}
					else
					{
						t_list* NuevaTabla = ObtenerTablaSegmentosDelPaquete((t_list*)recibir_paquete(SocketKernel));
						LimpiarElementosDeTabla(TablaSegmentos);
						TablaSegmentos = NuevaTabla;
					}
				}				
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_OPEN")==0)
			{
				PC++;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_CLOSE")==0)
			{
				PC++;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_SEEK")==0)
			{
				PC++;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_READ")==0)
			{
				PC++;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_WRITE")==0)
			{
				PC++;
			}

			else if(strcmp(Instruccion_A_Ejecutar, "F_TRUNCATE")==0)
			{
				PC++;
			}
		}

		free(Registros);

		//liberar memoria TablaSegmentos
		while (!list_is_empty(TablaSegmentos))
		{
			void* elemtnto = list_remove(TablaSegmentos, 0);
			free(elemtnto);
		}
		list_destroy(TablaSegmentos);

		//liberar memoria DatosRecibidos
		while (!list_is_empty(DatosRecibidos))
		{
			void* elemtnto = list_remove(DatosRecibidos, 0);
			free(elemtnto);
		}
		//list_destroy(DatosRecibidos);
		free(DatosRecibidos);
	}
	pthread_exit(NULL);
}

//funcion que recibe una lista con los datos del PCB y los guarda en una estructura
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

void Enviar_PCB_A_Kernel(int ProgramCounter, t_registrosCPU* Registros_A_Enviar, int SocketKernel)
{
	t_paquete* Paquete_Actualizado_PCB = crear_paquete(CPU_PCB);

	agregar_a_paquete(Paquete_Actualizado_PCB, &ProgramCounter, sizeof(int));

	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->AX), sizeof(Registros_A_Enviar->AX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->BX), sizeof(Registros_A_Enviar->BX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->CX), sizeof(Registros_A_Enviar->CX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->DX), sizeof(Registros_A_Enviar->DX));

	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->EAX), sizeof(Registros_A_Enviar->EAX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->EBX), sizeof(Registros_A_Enviar->EBX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->ECX), sizeof(Registros_A_Enviar->ECX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->EDX), sizeof(Registros_A_Enviar->EDX));

	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->RAX), sizeof(Registros_A_Enviar->RAX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->RBX), sizeof(Registros_A_Enviar->RBX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->RCX), sizeof(Registros_A_Enviar->RCX));
	agregar_a_paquete(Paquete_Actualizado_PCB, &(Registros_A_Enviar->RDX), sizeof(Registros_A_Enviar->RDX));

	time(&tiempoFinal);
	
	double diferenciaTiempo = difftime(tiempoFinal, tiempoInicio);

	agregar_a_paquete(Paquete_Actualizado_PCB, &diferenciaTiempo, sizeof(double));

	enviar_paquete(Paquete_Actualizado_PCB, SocketKernel);
	eliminar_paquete(Paquete_Actualizado_PCB);
}

//devuelve un puntero a un registro por su nombre
char* ObtenerRegistro(char* NombreRegistro, t_registrosCPU* Registros)
{
	if(strcmp(NombreRegistro, "AX")==0)
		return Registros->AX;
	else if(strcmp(NombreRegistro, "BX")==0)
		return Registros->BX;
	else if(strcmp(NombreRegistro, "CX")==0)
		return Registros->CX;
	else if(strcmp(NombreRegistro, "DX")==0)
		return Registros->DX;
	else if(strcmp(NombreRegistro, "EAX")==0)
		return Registros->EAX;
	else if(strcmp(NombreRegistro, "EBX")==0)
		return Registros->EBX;
	else if(strcmp(NombreRegistro, "ECX")==0)
		return Registros->ECX;
	else if(strcmp(NombreRegistro, "EDX")==0)
		return Registros->EDX;
	else if(strcmp(NombreRegistro, "RAX")==0)
		return Registros->RAX;
	else if(strcmp(NombreRegistro, "RBX")==0)
		return Registros->RBX;
	else if(strcmp(NombreRegistro, "RCX")==0)
		return Registros->RCX;
	else if(strcmp(NombreRegistro, "RDX")==0)
		return Registros->RDX;
	else
		return NULL;
}

//devuelve el tamaño de un registro por su nombre
int ObtenerTamanoRegistro(char* NombreRegistro)
{
	if
	(
		strcmp(NombreRegistro, "AX")==0 ||
		strcmp(NombreRegistro, "BX")==0 ||
		strcmp(NombreRegistro, "CX")==0 ||
		strcmp(NombreRegistro, "DX")==0
	)return 4;
	else if
	(
		strcmp(NombreRegistro, "EAX")==0 ||
		strcmp(NombreRegistro, "EBX")==0 ||
		strcmp(NombreRegistro, "ECX")==0 ||
		strcmp(NombreRegistro, "EDX")==0
	)return 8;
	else if
	(
		strcmp(NombreRegistro, "RAX")==0 ||
		strcmp(NombreRegistro, "RBX")==0 ||
		strcmp(NombreRegistro, "RCX")==0 ||
		strcmp(NombreRegistro, "RDX")==0
	)return 16;
	else
		return 0;
}

void TraducirDireccion(char* CharDirLogica, int* NumSegmento, int* Offset)
{
	int DirLogica = atoi(CharDirLogica);

	*NumSegmento = (int)floor((DirLogica/ TAM_MAX_SEGMENTO));
	*Offset = DirLogica%TAM_MAX_SEGMENTO;
}


void LeerConfigs(char* path)
{	
    config = config_create(path);

	IP_MEMORIA = config_get_string_value(config, "IP_MEMORIA");

	PUERTO_MEMORIA = config_get_string_value(config, "PUERTO_MEMORIA");

	PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");

	RETARDO_INSTRUCCION = config_get_int_value(config, "RETARDO_INSTRUCCION");

	TAM_MAX_SEGMENTO = config_get_int_value(config, "TAM_MAX_SEGMENTO");
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