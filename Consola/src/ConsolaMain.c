#include "../include/ConsolaMain.h"
#include <readline/readline.h>
#include <readline/history.h>

int SocketKernel;

void sighandler(int s) 
{
	liberar_conexion(SocketKernel);
	exit(0);
}

int main(int argc, char* argv[])
{
	signal(SIGINT, sighandler);
	signal(SIGSEGV, sighandler);

	Consola_Logger = log_create("Consola.log", NOMBRE_PROCESO, true, LOG_LEVEL_INFO);

	config = config_create(argv[1]);

	//ruta del pseudocodigo
	char* path = argv[2];

	//Auxiliar para cada linea que lee
	char linea[1024];

	if(ConectarConKernel() == 0)
	{
		log_error(Consola_Logger, "No se pudo conectar con el Kernel");
		return EXIT_FAILURE;
	}

	//abro el archivo de pseudocodigo si existe
	FILE* archivo = fopen(path, "r");
	if (archivo == NULL)
	{
		return EXIT_FAILURE;
	}

	t_paquete* paquete = crear_paquete(INSTRUCCIONES);
	//loopea entre cada linea del archivo de pseudocodigo
    while (fgets(linea, 1024, archivo))
	{
		//agregar un \n al final de cada linea que no termine en \n
		if (strcmp(&linea[strlen(linea)-1], "\n") != 0){
			printf("linea sin /n");
			strcat(linea, "\n");
		}
		//agregar la linea al paquete
		agregar_a_paquete(paquete, linea, strlen(linea) + 1);
    }
	fclose(archivo);

	//enviar el paquete al kernel
	enviar_paquete(paquete, SocketKernel);
	eliminar_paquete(paquete);

	log_info(Consola_Logger, "Se envio el archivo de pseudocodigo al Kernel");

	//esperar respuesta del kernel
	char* respuesta = (char*) recibir_paquete(SocketKernel);
	log_info(Consola_Logger, "%s", respuesta);

	liberar_conexion(SocketKernel);
	return EXIT_SUCCESS;
}

int ConectarConKernel()
{
	IP_KERNEL = config_get_string_value(config, "IP_KERNEL");
	PUERTO_KERNEL = config_get_string_value(config, "PUERTO_KERNEL");
	SocketKernel = conectar_servidor(Consola_Logger, "Kernel", IP_KERNEL, PUERTO_KERNEL);
	return SocketKernel;
}