#include "../include/MemoriaMain.h"


t_log* Memoria_Logger;

int SocketMemoria;

void sighandler(int s) {
	liberar_conexion(SocketMemoria);
	exit(0);
}


int main(int argc, char* argv[])
{
	signal(SIGINT, sighandler);

	Memoria_Logger = log_create("Memoria.log", NOMBRE_PROCESO, true, LOG_LEVEL_INFO);
	
	LeerConfigs(argv[1]);

	inicializarMemoria();

	crearSegmento(1,10,1);
	crearSegmento(1,5,2);
//	crearSegmento(2,20,1);
//	crearSegmento(2,25,2);
	crearSegmento(3,8,1);
//	crearSegmento(3,3894,2);


	eliminarSegmento(1,1);
//	eliminarSegmento(2,2);
//	eliminarSegmento(3,1);

	VerHuecos();

//	crearSegmento(3,8,4);

	compactarSegmentos();
	VerMem();
	VerHuecos();



	InicializarSemaforos();

	InicializarConexiones();

	while(true){
		log_info(Memoria_Logger, "hilo principal esta ejecutando");
		sleep(10);
	}

	return EXIT_SUCCESS;
}

//Crea un Hilo que escucha conexiones de los modulos
int InicializarConexiones()
{
	//Crear hilo escucha
	pthread_t HiloEscucha;
    if (pthread_create(&HiloEscucha, NULL, EscucharConexiones, NULL) != 0) {
        exit(EXIT_FAILURE);
    }
	return 1;
}

//Inicia un servidor en el que escucha por modulos permanentemente y cuando recibe uno crea un hilo para administrar esaa conexion
void* EscucharConexiones()
{
	SocketMemoria = iniciar_servidor(Memoria_Logger, NOMBRE_PROCESO, "127.0.0.1", PUERTO_ESCUCHA);
	
	if(SocketMemoria != 0)
	{
		//Escuchar por modulos en bucle
		while(true)
		{
			int SocketCliente = esperar_cliente(Memoria_Logger, NOMBRE_PROCESO, SocketMemoria);

			if(SocketCliente != 0)
			{	
				//Crea un hilo para el modulo conectado
				pthread_t HiloAdministradorDeMensajes;
				if (pthread_create(&HiloAdministradorDeMensajes, NULL, AdministradorDeModulo, (void*)&SocketCliente) != 0) {
					exit(EXIT_FAILURE);
				}
			}
		}
	}

	liberar_conexion(SocketMemoria);
	return (void*)EXIT_FAILURE;
}

//Acciones a realizar para cada modulo conectado
void* AdministradorDeModulo(void* arg)
{
	int* SocketClienteConectado = (int*)arg;

	//bucle que esperara peticiones del modulo conectado
	//y realiza la operacion
	while(true)
	{
		char* PeticionRecibida = (char*)recibir_paquete(*SocketClienteConectado);

		char* Pedido = strtok(PeticionRecibida, " ");

		if(strcmp(Pedido, "CREAR_PROCESO")==0)
		{
			char* PID = strtok(NULL, " ");
			
			//Crear estructuras administrativas NO enviar nada a Kernel
			log_info(Memoria_Logger,"Creación de Proceso PID: %d",PID);
			EnviarMensage("OK", *SocketClienteConectado);
		}
		else if(strcmp(Pedido, "FINALIZAR_PROCESO")==0)
		{
			//PID del proceso a finalizar
			char* PID = strtok(NULL, " ");

			//finalizar_proceso(PID);
			//NO enviar nada al kernel, solo borrar las estructuras y liberar la memoria
			finalizarProceso(PID);
			log_info(Memoria_Logger,"Eliminación de Proceso PID: %d",PID);
		}
		//enviar el valor empezando desde la direccion recibida y con la longitud recibida
		else if(strcmp(Pedido, "MOV_IN")==0)
		{
			//PID del proceso
			char* PID = strtok(NULL, " ");
			//direccion donde buscar el contenido
			char* NumSegmento = strtok(NULL, " ");
			char* Desplazamiento = strtok(NULL, " ");
			//longitud del contenido a buscar
			char* Longitud = strtok(NULL, " ");	

			char*  Contenido = leerSegmento(PID,NumSegmento,Desplazamiento,Longitud);

			//enviar el contenido encontrado o SEG_FAULT en caso de error
			EnviarMensage(Contenido, *SocketClienteConectado);
		}
		//guardar el valor recibido en la direccion recibida
		else if(strcmp(Pedido, "MOV_OUT")==0)
		{
			//PID del proceso
			char* PID = strtok(NULL, " ");
			//direccion donde guardar el contenido
			char* NumSegmento = strtok(NULL, " ");
			char* Desplazamiento = strtok(NULL, " ");
			//valor a guardar TERMINADO EN \0
			char* Valor = strtok(NULL, " ");

			char* Contenido = escribirSegmento(PID,NumSegmento,Desplazamiento,Valor);
			//eviar SEG_FAULT en caso de error sino enviar cualquier otra cadena de caracteres
			EnviarMensage(Contenido, *SocketClienteConectado);
		}
		//crear un segmento para un proceso
		else if(strcmp(Pedido, "CREATE_SEGMENT")==0)
		{
			//PID del proceso
			char* PID = strtok(NULL, " ");
			//ID del segmento a crear
			char* ID = strtok(NULL, " ");
			//Tamano del segmento a crear
			char* Tamanio = strtok(NULL, " ");

			//crear_segmento(PID, ID, Tamanio);
			
			/*enviar la respuesta correspondiente a la situacion, ya sea:
			OUT_OF_MEMORY
			COMPACTAR <OCUPADO/cualquier otra cadena si no hay operacion entre FS y Mem>
			Cualquier otra cadena si hay memoria disponible y no hay que compactar
			*/

			char* Contenido = crearSegmento(ID,Tamanio,PID);

			EnviarMensage(Contenido, *SocketClienteConectado);
		}
		//eliminar un segmento de un proceso
		else if(strcmp(Pedido, "DELETE_SEGMENT")==0)
		{
			//PID del proceso
			char* PID = strtok(NULL, " ");
			//ID del segmento a eliminar
			char* ID = strtok(NULL, " ");

			//eliminar_segmento(PID, ID);
			//enviar SEG_FAULT en caso de error sino enviar cualquier otra cadena de caracteres			
			char* Contenido = eliminarSegmento(ID,PID);
			EnviarMensage(Contenido, *SocketClienteConectado);
		}

		//agregar el resto de operaciones entre FS y memoria
	}

	liberar_conexion(*SocketClienteConectado);
	return NULL;
}


void LeerConfigs(char* path)
{
    config = config_create(path);

    PUERTO_ESCUCHA = config_get_string_value(config, "PUERTO_ESCUCHA");

	char *tam_memoria = config_get_string_value(config, "TAM_MEMORIA");
	TAM_MEMORIA= atoi(tam_memoria);

	char *tam_segmento_0 = config_get_string_value(config, "TAM_SEGMENTO_0");
	TAM_SEGMENTO_0= atoi(tam_segmento_0);

	char *cant_segmentos = config_get_string_value(config, "CANT_SEGMENTOS");
	CANT_SEGMENTOS= atoi(cant_segmentos);

	char *retardo_memoria = config_get_string_value(config, "RETARDO_MEMORIA");
	RETARDO_MEMORIA= atoi(retardo_memoria);

	char *retardo_compactacion = config_get_string_value(config, "RETARDO_COMPACTACION");
	RETARDO_COMPACTACION= atoi(retardo_compactacion);

	char* alg_asig = config_get_string_value(config, "ALGORITMO_ASIGNACION");

	if(strcmp(alg_asig,"BEST")==0)ALGORITMO_ASIGNACION=0;
	else if(strcmp(alg_asig,"WORST")==0)ALGORITMO_ASIGNACION=1;
	else ALGORITMO_ASIGNACION=2;
}

//Inicializa los semaforos
void InicializarSemaforos()
{
	sem_init(&m_UsoDeMemoria, 0, 1);
}

void VerMem(){		
	
	for(int i = 0; i<list_size(TABLA_SEGMENTOS); i++){
		Segmento* aux = list_get(TABLA_SEGMENTOS,i);

		log_info(Memoria_Logger,"direccion base es: %p",aux->direccionBase);
		log_info(Memoria_Logger,"ID: %d",aux->idSegmento);
		log_info(Memoria_Logger,"PID: %d",aux->PID);
		log_info(Memoria_Logger,"LIMITE: %d",aux->limite);

		//Obtener un puntero al tipo de datos deseado (por ejemplo, char*)
		char* datos = (char*)aux->direccionBase;

		//Acceder y leer los datos asignados
		for (int i = 0; i < aux->limite; i++) {
			log_info(Memoria_Logger,"dato %c, posicion %p", datos[i],(aux->direccionBase)+i);
		}
	}
}

void VerHuecos(){		
	
	if(list_size(TABLA_HUECOS) > 0){
		for(int i = 0; i<list_size(TABLA_HUECOS); i++){
			Hueco* aux = list_get(TABLA_HUECOS,i);

			log_info(Memoria_Logger,"direccion base es(HUECO): %p",aux->direccionBase);
			log_info(Memoria_Logger,"LIMITE(HUECO): %d",aux->limite);
		}
	}
}

void inicializarMemoria() {
	TABLA_SEGMENTOS=list_create();  // Establecer la cantidad de segmentos
	TABLA_HUECOS=list_create();  // Establecer la cantidad de segmentos

	Segmento* seg= malloc(sizeof(Segmento*));

    // Asignar memoria para el espacio de usuario
    MEMORIA = (void *) malloc(TAM_MEMORIA);
    memset(MEMORIA,'\0',TAM_MEMORIA);

    seg->PID=-1;
	seg->idSegmento=0;
	seg->direccionBase=MEMORIA;
	seg->limite=TAM_SEGMENTO_0;
	memset(seg->direccionBase,'\0', seg->limite);
	list_add(TABLA_SEGMENTOS,seg);
	Hueco* hueco = malloc(sizeof())
	list_add()
}

int validarSegmento(int idSeg,int desplazamientoSegmento){
	//valida la existencia del segmento y checkea que el desplazamiento no tire seg_fault
	int aux=0;
	Segmento* seg=list_get(TABLA_SEGMENTOS,aux);

	while(list_size(TABLA_SEGMENTOS)>aux){
		if(seg->idSegmento==idSeg) {
			if(((int) seg->direccionBase + seg->limite) >= ((int) seg->direccionBase + desplazamientoSegmento)) return 1;
			else return 0;
		}
		aux++;
		if(list_size(TABLA_SEGMENTOS)>aux)seg=list_get(TABLA_SEGMENTOS,aux);
	}

	return 0;
}

char* crearSegmento(int idSeg, int tamanoSegmento, int PID) {
    // Buscar espacio contiguo disponible para el segmento
    // utilizando el algoritmo de asignación especificado (FIRST, BEST, WORST)
	char* estado = validarMemoria(tamanoSegmento);

	if(estado == "HUECO"){
		switch(ALGORITMO_ASIGNACION){
			case 0:
				BestFit(idSeg,tamanoSegmento,PID);
				return "OK";
				break;
			case 1:
				WorstFit(idSeg,tamanoSegmento,PID);
				return "OK";
				break;
			case 2:
				FirstFit(idSeg,tamanoSegmento,PID);
				log_info(Memoria_Logger,"TERMINO FIRST");
				return "OK";
				break;
		}
	}
	else if(estado == "CONTIGUO")
	{
		Segmento* lastSeg =list_get(TABLA_SEGMENTOS,list_size(TABLA_SEGMENTOS)-1);	
		AgregarSegmento(lastSeg,PID,tamanoSegmento,idSeg);
		return "OK";
	}

	return estado;
}

char* validarMemoria(int Tamano){
	Segmento* lastSeg = list_get(TABLA_SEGMENTOS,0);	
	void* maxPos = lastSeg->direccionBase + lastSeg->limite;
	int TamHuecos = 0;

	for(int i = 0; i < list_size(TABLA_SEGMENTOS); i++){
		Segmento* Seg = list_get(TABLA_SEGMENTOS,i);	
		if(maxPos < Seg->direccionBase)
			maxPos = Seg->direccionBase + Seg->limite;		
	}

	if(maxPos + 1 + Tamano <= MEMORIA + TAM_MEMORIA)
		return "CONTIGUO";

	else if(list_size(TABLA_HUECOS) != 0){
		for(int i = 0; i < list_size(TABLA_HUECOS); i++){
			Hueco* hueco = list_get(TABLA_HUECOS,i);	

			if(Tamano <= hueco->limite)return "HUECO";

			TamHuecos += hueco->limite;
		}
		if(TamHuecos >= Tamano)
			return "COMPACTAR";
		else
			return "OUT_OF_MEMORY";
	}
}

void AgregarSegmento(Segmento* lastSeg,int PID,int tamanoSegmento,int idSeg){
	Segmento* seg = malloc(sizeof(Segmento*));
	seg->PID = PID;
	seg->direccionBase=(lastSeg->direccionBase) + (lastSeg->limite+1);
	seg->limite=tamanoSegmento;
	seg->idSegmento=idSeg;
	list_add(TABLA_SEGMENTOS,seg);
	memset(seg->direccionBase,'\0',seg->limite);

	log_info(Memoria_Logger,"PID: %d - Crear Segmento: %d - Base: %p - TAMAÑO: %d",PID,idSeg,seg->direccionBase,tamanoSegmento);
}

char* eliminarSegmento(int idSegmento, int PID){
	int indice = buscarSegmento(PID, idSegmento,false);

	if(indice != -1) {
		Segmento* seg=list_get(TABLA_SEGMENTOS,indice);

		Hueco* hueco = malloc(sizeof(Hueco*));
		hueco->direccionBase = seg->direccionBase;
		hueco->limite = seg->limite;

		list_add(TABLA_HUECOS,hueco);

		memset(seg->direccionBase,'\0',seg->limite);
		list_remove(TABLA_SEGMENTOS,indice);

		log_info(Memoria_Logger,"PID: %d - Eliminar Segmento: %d - Base: %p - TAMAÑO: %d",PID,idSegmento,seg->direccionBase,seg->limite);
		return "OK";
	}
	else 
		return "SEG_FAULT";	
}

char* leerSegmento(int PID, int IdSeg, int Offset, int Longitud){
	int indice = buscarSegmento(PID, IdSeg,false);

	if(indice != -1) {
		Segmento* seg=list_get(TABLA_SEGMENTOS,indice);

		char buffer[Longitud];
		char* cont;
		if(validarSegmento(IdSeg,(Offset + Longitud)) == 1){
			memcpy(buffer,(seg->direccionBase + Offset),Longitud);
			buffer[Longitud] = '\0';
			cont = buffer;
			return cont;
		}
		else{
			return "SEG_FAULT";
		} 
	}
}

char* escribirSegmento(int PID, int IdSeg, int Offset, char* datos){
	int indice = buscarSegmento(PID, IdSeg,false);

	if(indice != -1) {
		Segmento* seg = list_get(TABLA_SEGMENTOS,indice);

		if(validarSegmento(IdSeg,(Offset + strlen(datos))) == 1){
			for(int i = 0; i < strlen(datos);i++)
				memset(seg->direccionBase + Offset + i,datos[i],1);
			return "OK";
		}
		else
			return "SEG_FAULT";
	}
}

int buscarSegmento(int PID, int idSeg,bool Finish){
	int aux=0;
	Segmento* seg=list_get(TABLA_SEGMENTOS,aux);

	while(list_size(TABLA_SEGMENTOS)>aux){
		if(Finish && seg->PID == PID) return aux;
		if(seg->idSegmento==idSeg && seg->PID == PID) return aux;
		aux++;
		if(list_size(TABLA_SEGMENTOS)>aux)seg=list_get(TABLA_SEGMENTOS,aux);
	}
	return -1;
}

void finalizarProceso(int PID){
	int indice = 0;
	while(indice != -1){
		indice = buscarSegmento(1,0,true);
		if(indice != -1){
			Segmento* seg = list_get(TABLA_SEGMENTOS,indice); 
			eliminarSegmento(seg->idSegmento,PID);
		}
	}
}

void compactarSegmentos() {
    // Mover los segmentos para eliminar los huecos libres
	log_info(Memoria_Logger,"Solicitud de Compactación");
	t_list* tablaOrdenada= list_create();
	t_list* tablaSeg= list_duplicate(TABLA_SEGMENTOS);
	list_clean(TABLA_HUECOS);
	void* menorBase=MEMORIA+TAM_MEMORIA+1;
	int indice;
	Segmento* seg= malloc(sizeof(Segmento*));
	Segmento* segAnterior= malloc(sizeof(Segmento*));
	//SEGMENTO 0 no se modifica
	list_add(tablaOrdenada,list_get(TABLA_SEGMENTOS,0));

	//crea una nueva lista con los segmentos ordenados
	for(int j=1;j<list_size(TABLA_SEGMENTOS);j++){
		for(int i=0;i<list_size(tablaSeg);i++){
			seg=list_get(tablaSeg,i);
			if(menorBase>seg->direccionBase){
				menorBase = seg->direccionBase;
				indice=i+1;
			}
		}
		seg = list_remove(tablaSeg, indice);
//		log_info(Memoria_Logger,"agrego el segm %d",indice);
		list_add(tablaOrdenada,seg);
	}

    // Actualizar la tabla de segmentos ordenada con las nuevas direcciones base
	for(int i=1;i<list_size(tablaOrdenada);i++){
		seg=list_get(tablaOrdenada,i);
//		log_info(Memoria_Logger,"indices %d id%d PID%d",i,seg->idSegmento,seg->PID);
		segAnterior=list_get(tablaOrdenada,i-1);
		seg->direccionBase=segAnterior->direccionBase + segAnterior->limite + 1;
		list_replace(tablaOrdenada,i,seg);
		log_info(Memoria_Logger,"PID: %d - Cambiar base de Segmento: %d - Base: %p - TAMAÑO: %d",seg->PID,seg->idSegmento,seg->direccionBase,seg->limite);
	}

	TABLA_SEGMENTOS=list_duplicate(tablaOrdenada);
	list_destroy(tablaOrdenada);
	list_destroy(tablaSeg);

}

void BestFit(int idSeg, int tam, int PID){
	Hueco* MinHole = malloc(sizeof(Hueco*));
	Hueco* h = malloc(sizeof(Hueco*));
	int Diff = INT_MAX;
	int indice = 0;

	for(int i = 0; i < list_size(TABLA_HUECOS); i++){
		h = list_get(TABLA_HUECOS,i);

		if(h->limite >= tam && (h->limite - tam) < Diff){
			Diff = h->limite - tam;
			MinHole = h;
			indice = i;
		} 
	}

	Segmento* seg = malloc(sizeof(Segmento*));
	seg->direccionBase = MinHole->direccionBase;
	seg->idSegmento = idSeg;
	seg->PID = PID;
	seg->limite = tam;

	int newHoleLimit = MinHole->limite - tam;

	if(newHoleLimit>0){
		MinHole->limite = newHoleLimit;
		MinHole->direccionBase = MinHole->direccionBase + tam + 1;

		list_remove(TABLA_HUECOS,indice);
		memset(MinHole->direccionBase,'\0',MinHole->limite);
		list_add(TABLA_HUECOS,MinHole);
	}
	else list_remove(TABLA_HUECOS,indice);

	list_add(TABLA_SEGMENTOS,seg);
	log_info(Memoria_Logger,"PID: %d - Crear Segmento: %d - Base: %p - TAMAÑO: %d",PID,idSeg,seg->direccionBase,tam);
	memset(seg->direccionBase,'\0',seg->limite);

}

void WorstFit(int idSeg, int tam, int PID){
	Hueco* MaxHole = malloc(sizeof(Hueco*));
	Hueco* h = malloc(sizeof(Hueco*));
	MaxHole->limite = 0;
	int indice = 0;

	for(int i = 0; i < list_size(TABLA_HUECOS); i++){

		h = list_get(TABLA_HUECOS,i);

		if(h->limite >= tam && h->limite > MaxHole->limite){
			MaxHole = h;
			indice = i;
		} 
	}

	Segmento* seg = malloc(sizeof(Segmento*));
	seg->direccionBase = MaxHole->direccionBase;
	seg->idSegmento = idSeg;
	seg->PID = PID;
	seg->limite = tam;

	int newHoleLimit = MaxHole->limite - tam;

	if(newHoleLimit>0){
		MaxHole->limite = newHoleLimit;
		MaxHole->direccionBase = MaxHole->direccionBase + tam + 1;

		list_remove(TABLA_HUECOS,indice);
		memset(MaxHole->direccionBase,'\0',MaxHole->limite);
		list_add(TABLA_HUECOS,MaxHole);

	}else list_remove(TABLA_HUECOS,indice);

	list_add(TABLA_SEGMENTOS,seg);
	log_info(Memoria_Logger,"PID: %d - Crear Segmento: %d - Base: %p - TAMAÑO: %d",PID,idSeg,seg->direccionBase,tam);
	memset(seg->direccionBase,'\0',seg->limite);
}

void FirstFit(int idSeg, int tam, int PID){
	int aux = 0;
	bool find = false;

	while(list_size(TABLA_HUECOS)>aux && !find){
		Hueco* h = list_get(TABLA_HUECOS,aux);
		Hueco* newHole = malloc(sizeof(Hueco*));

		if(h->limite >= tam){
			Segmento* seg = malloc(sizeof(Segmento*));
			seg->direccionBase = h->direccionBase;
			seg->idSegmento = idSeg;
			seg->PID = PID;
			seg->limite = tam;

			int newHoleLimit = h->limite - tam;

			if(newHoleLimit>0){
				newHole->limite = newHoleLimit;
				newHole->direccionBase = h->direccionBase + tam + 1;

				list_remove(TABLA_HUECOS,aux);
				memset(newHole->direccionBase,'\0',newHole->limite);
				list_add(TABLA_HUECOS,newHole);
			}
			else list_remove(TABLA_HUECOS,aux);		

			list_add(TABLA_SEGMENTOS,seg);
			memset(seg->direccionBase,'\0',seg->limite);
			log_info(Memoria_Logger,"PID: %d - Crear Segmento: %d - Base: %p - TAMAÑO: %d",PID,idSeg,seg->direccionBase,tam);
			find = true;
		}
		else aux++;
	};
}
