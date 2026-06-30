/**
 * ============================================================================
 *  client.c — Cliente de modo usuario para el driver ProcEnumDrv
 * ============================================================================
 *
 *  Este programa se ejecuta en modo usuario (Ring 3) y se comunica con el
 *  driver de modo kernel a través de un IOCTL para solicitar la lista de
 *  procesos en ejecución.
 * ============================================================================
 */

#include <stdio.h>       /* printf, fprintf          */
#include <stdlib.h>      /* EXIT_SUCCESS, EXIT_FAILURE */
#include <windows.h>     /* CreateFileW, DeviceIoControl, CloseHandle */
#include <winioctl.h>    /* CTL_CODE, METHOD_BUFFERED, etc. */

/* -----------------------------------------------------------------------
 *  Incluimos las definiciones compartidas con el driver.
 *  En un proyecto real, tanto el driver como el cliente incluirían
 *  common.h para garantizar que las estructuras y IOCTLs coinciden.
 * ----------------------------------------------------------------------- */
#include "common.h"

/**
 *  main — Punto de entrada del cliente
 */
int main(void)
{
    printf("=== ProcEnumDrv — Cliente de modo usuario ===\n\n");

    /* ------------------------------------------------------------------
     *  PASO 1: Abrir un handle al dispositivo del driver
     * ------------------------------------------------------------------
     *  CreateFileW abre el dispositivo usando el enlace simbólico que el
     *  driver creó (\\.\ProcEnumDrv).
     *
     *  Esta llamada genera un IRP_MJ_CREATE que el driver maneja en
     *  DispatchCreateClose, devolviendo STATUS_SUCCESS.
     *
     *  Si el driver no está cargado, CreateFileW fallará con el error
     *  ERROR_FILE_NOT_FOUND (el enlace simbólico no existe).
     * ------------------------------------------------------------------ */
    HANDLE hDevice = CreateFileW(
        DEVICE_SYMLINK_USER,           /* Nombre del dispositivo         */
        GENERIC_READ | GENERIC_WRITE,  /* Acceso de lectura y escritura  */
        0,                             /* Sin compartir                  */
        NULL,                          /* Seguridad por defecto          */
        OPEN_EXISTING,                 /* El dispositivo ya debe existir */
        FILE_ATTRIBUTE_NORMAL,         /* Atributos normales             */
        NULL                           /* Sin template                   */
    );

    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "[ERROR] No se pudo abrir el dispositivo.\n");
        fprintf(stderr, "        Codigo de error: %lu\n", GetLastError());
        fprintf(stderr, "        Asegurate de que el driver esta cargado.\n");
        fprintf(stderr, "        Usa: sc start ProcEnumDrv\n");
        return EXIT_FAILURE;
    }

    printf("[OK] Dispositivo abierto correctamente.\n");
    printf("[*]  Solicitando lista de procesos al driver...\n\n");

    /* ------------------------------------------------------------------
     *  PASO 2: Preparar el buffer de salida
     * ------------------------------------------------------------------
     *  Reservamos memoria en modo usuario para recibir la respuesta del
     *  driver. El I/O Manager copiará los datos desde el buffer del
     *  sistema (kernel) a este buffer automáticamente (METHOD_BUFFERED).
     * ------------------------------------------------------------------ */
    PROCESS_LIST_RESPONSE *pResponse =
        (PROCESS_LIST_RESPONSE *)malloc(sizeof(PROCESS_LIST_RESPONSE));

    if (pResponse == NULL) {
        fprintf(stderr, "[ERROR] No se pudo asignar memoria.\n");
        CloseHandle(hDevice);
        return EXIT_FAILURE;
    }

    /* Inicializar a ceros */
    memset(pResponse, 0, sizeof(PROCESS_LIST_RESPONSE));

    /* ------------------------------------------------------------------
     *  PASO 3: Enviar el IOCTL al driver
     * ------------------------------------------------------------------
     *  DeviceIoControl envía un código de control al driver. El I/O
     *  Manager crea un IRP_MJ_DEVICE_CONTROL y lo despacha a nuestra
     *  función DispatchDeviceControl en el driver.
     *
     *  Parámetros:
     *    hDevice           — Handle al dispositivo.
     *    IOCTL_ENUM_PROCESSES — Código IOCTL que identifica la operación.
     *    NULL, 0           — Buffer y tamaño de entrada (no enviamos datos).
     *    pResponse         — Buffer de salida donde el driver escribirá.
     *    sizeof(...)       — Tamaño del buffer de salida.
     *    &bytesReturned    — Bytes realmente escritos por el driver.
     *    NULL              — Sin OVERLAPPED (operación síncrona).
     * ------------------------------------------------------------------ */
    DWORD bytesReturned = 0;

    BOOL success = DeviceIoControl(
        hDevice,                             /* Handle al dispositivo     */
        IOCTL_ENUM_PROCESSES,                /* Código IOCTL              */
        NULL,                                /* Sin buffer de entrada     */
        0,                                   /* Tamaño entrada = 0        */
        pResponse,                           /* Buffer de salida          */
        sizeof(PROCESS_LIST_RESPONSE),       /* Tamaño de salida          */
        &bytesReturned,                      /* Bytes devueltos           */
        NULL                                 /* Operación síncrona        */
    );

    if (!success) {
        fprintf(stderr, "[ERROR] DeviceIoControl fallo.\n");
        fprintf(stderr, "        Codigo de error: %lu\n", GetLastError());
        free(pResponse);
        CloseHandle(hDevice);
        return EXIT_FAILURE;
    }

    /* ------------------------------------------------------------------
     *  PASO 4: Mostrar los resultados
     * ------------------------------------------------------------------ */
    printf("%-8s  %s\n", "PID", "NOMBRE");
    printf("------   ---------------\n");

    for (ULONG i = 0; i < pResponse->Count; i++) {
        printf("%-8lu  %s\n",
               pResponse->Entries[i].Pid,
               pResponse->Entries[i].ImageName);
    }

    printf("\n[*] Total de procesos: %lu\n", pResponse->Count);

    /* ------------------------------------------------------------------
     *  PASO 5: Liberar recursos
     * ------------------------------------------------------------------
     *  - free() libera la memoria del buffer de respuesta.
     *  - CloseHandle() cierra el handle al dispositivo, lo que genera
     *    un IRP_MJ_CLOSE que el driver maneja en DispatchCreateClose.
     * ------------------------------------------------------------------ */
    free(pResponse);
    CloseHandle(hDevice);

    printf("[OK] Recursos liberados. Fin del programa.\n");

    return EXIT_SUCCESS;
}
