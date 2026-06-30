/**
 * ============================================================================
 *  common.h — Definiciones compartidas entre el driver y el cliente
 * ============================================================================
 *
 *  Este fichero contiene las constantes y estructuras de datos que necesitan
 *  conocer tanto el driver de modo kernel como la aplicación de modo usuario.
 *  Al mantenerlas en un único header se evitan inconsistencias.
 * ============================================================================
 */

#ifndef _COMMON_H_
#define _COMMON_H_

/* -----------------------------------------------------------------------
 *  Nombre del dispositivo (visible desde modo usuario)
 * -----------------------------------------------------------------------
 *  En Windows, los dispositivos del kernel se exponen a las aplicaciones
 *  de usuario a través de un "enlace simbólico" (symbolic link) en el
 *  espacio de nombres \\??\\ (equivalente a \\DosDevices\\).
 *
 *  El nombre real del dispositivo vive en \\Device\\ProcEnumDrv, pero
 *  desde modo usuario se accede como \\\\.\\ProcEnumDrv gracias al
 *  enlace simbólico que crea el driver al cargarse.
 * ----------------------------------------------------------------------- */
#define DEVICE_SYMLINK_USER  L"\\\\.\\ProcEnumDrv"

/* -----------------------------------------------------------------------
 *  Código IOCTL — IOCTL_ENUM_PROCESSES
 * -----------------------------------------------------------------------
 *  Un IOCTL (Input/Output Control) es el mecanismo estándar para que una
 *  aplicación de usuario envíe comandos personalizados a un driver.
 *
 *  Se define con la macro CTL_CODE(DeviceType, Function, Method, Access):
 *
 *    - FILE_DEVICE_UNKNOWN : tipo genérico de dispositivo (no es un disco,
 *                            ni red, ni teclado... es un dispositivo propio).
 *    - 0x800               : número de función. Los valores >= 0x800 están
 *                            reservados para uso del programador (los menores
 *                            los reserva Microsoft).
 *    - METHOD_BUFFERED     : el I/O Manager copia los datos de entrada y
 *                            salida entre el buffer del usuario y un buffer
 *                            del sistema (seguro, evita accesos directos a
 *                            memoria de usuario desde el kernel).
 *    - FILE_ANY_ACCESS     : no se requieren permisos especiales de lectura
 *                            o escritura en el handle del dispositivo.
 * ----------------------------------------------------------------------- */
#define IOCTL_ENUM_PROCESSES  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* -----------------------------------------------------------------------
 *  Constantes
 * ----------------------------------------------------------------------- */
#define MAX_PROCESS_COUNT  1024   /* Máximo de procesos que almacenamos   */
#define MAX_IMAGE_NAME       16   /* Longitud de ImageFileName en EPROCESS */

/* -----------------------------------------------------------------------
 *  Estructura PROCESS_ENTRY
 * -----------------------------------------------------------------------
 *  Contiene la información básica de un proceso que el driver envía al
 *  cliente de modo usuario.
 *
 *  - Pid        : identificador numérico del proceso (Process ID).
 *  - ImageName  : nombre del ejecutable (ej. "notepad.exe"). Se obtiene
 *                 del campo ImageFileName dentro de la estructura EPROCESS
 *                 del kernel, que tiene un tamaño fijo de 15 caracteres
 *                 + terminador nulo.
 * ----------------------------------------------------------------------- */
typedef struct _PROCESS_ENTRY {
    unsigned long Pid;                     /* PID del proceso               */
    char          ImageName[MAX_IMAGE_NAME]; /* Nombre de la imagen (.exe)  */
} PROCESS_ENTRY, *PPROCESS_ENTRY;

/* -----------------------------------------------------------------------
 *  Estructura PROCESS_LIST_RESPONSE
 * -----------------------------------------------------------------------
 *  Estructura "contenedora" que el driver rellena con la lista completa
 *  de procesos encontrados y devuelve al cliente a través del IOCTL.
 *
 *  - Count    : número real de procesos que se han recogido.
 *  - Entries  : array con la información de cada proceso.
 * ----------------------------------------------------------------------- */
typedef struct _PROCESS_LIST_RESPONSE {
    unsigned long Count;                            /* Total de procesos   */
    PROCESS_ENTRY Entries[MAX_PROCESS_COUNT];        /* Array de procesos   */
} PROCESS_LIST_RESPONSE, *PPROCESS_LIST_RESPONSE;

#endif /* _COMMON_H_ */
