/**
 * ============================================================================
 *  driver.c — Driver de modo kernel para enumeración de procesos en Windows
/* =========================================================================
 *  DECLARACIONES DE FUNCIONES NO INCLUIDAS EN ntddk.h
 * =========================================================================
 *
 *  PsGetProcessImageFileName es una función exportada por ntoskrnl.exe
 *  (el ejecutable del kernel de Windows), pero no está declarada en los
 *  headers estándar del WDK (ntddk.h / wdm.h). Esto ocurre con varias
 *  funciones que Microsoft considera "semi-documentadas": existen y se
 *  pueden usar, pero no se garantiza su estabilidad entre versiones.
 *
 *  La palabra clave NTKERNELAPI indica que es una función exportada por
 *  el kernel (ntoskrnl.exe) y que nuestro driver puede importarla.
 *
 *  Esta función recibe un puntero a una estructura EPROCESS y devuelve
 *  un puntero a un string de caracteres (PUCHAR) con el nombre del
 *  ejecutable del proceso (por ejemplo "notepad.exe"). El string tiene
 *  un máximo de 15 caracteres, ya que el campo ImageFileName dentro de
 *  EPROCESS es un array de 16 bytes (15 caracteres + terminador nulo).
 * ========================================================================= */
NTKERNELAPI PUCHAR PsGetProcessImageFileName(
    _In_ PEPROCESS Process
);

NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(
    _In_  HANDLE     ProcessId,
    _Out_ PEPROCESS* Process
);

/* =========================================================================
 *  DEFINICIONES PROPIAS
 * =========================================================================
 *
 *  Código IOCTL para la enumeración de procesos.
 *  (Véase common.h para la explicación detallada de CTL_CODE)
 * ========================================================================= */
#define IOCTL_ENUM_PROCESSES  \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Máximos para el array de procesos */
#define MAX_PROCESS_COUNT  1024
#define MAX_IMAGE_NAME       16

/* -----------------------------------------------------------------------
 *  Estructura que almacena la información de un proceso individual.
 *  Es idéntica a la definida en common.h; se repite aquí para que el
 *  driver sea autocontenido (en un proyecto real se compartiría el header).
 * ----------------------------------------------------------------------- */
typedef struct _PROCESS_ENTRY {
    ULONG Pid;
    CHAR  ImageName[MAX_IMAGE_NAME];
} PROCESS_ENTRY, *PPROCESS_ENTRY;

/* -----------------------------------------------------------------------
 *  Estructura contenedora con la respuesta completa al IOCTL.
 * ----------------------------------------------------------------------- */
typedef struct _PROCESS_LIST_RESPONSE {
    ULONG         Count;
    PROCESS_ENTRY Entries[MAX_PROCESS_COUNT];
} PROCESS_LIST_RESPONSE, *PPROCESS_LIST_RESPONSE;

/* =========================================================================
 *  VARIABLES GLOBALES
 * =========================================================================
 *
 *  g_DeviceName :
 *    Nombre interno del dispositivo en el espacio de nombres del Object
 *    Manager del kernel. Sigue la convención \\Device\\<NombreDelDriver>.
 *    Este nombre sólo es visible desde modo kernel.
 *
 *  g_SymLinkName :
 *    Enlace simbólico que mapea el dispositivo del kernel al espacio de
 *    nombres accesible desde modo usuario (\\??\\, equivalente a
 *    \\DosDevices\\). Gracias a este enlace, una aplicación de usuario
 *    puede abrir el dispositivo con CreateFile(L"\\\\.\\ProcEnumDrv",...).
 *
 *  g_DeviceObject :
 *    Puntero al DEVICE_OBJECT creado por IoCreateDevice. Representa
 *    nuestro dispositivo en el kernel. Lo guardamos como global para
 *    poder eliminarlo en DriverUnload.
 *
 *  RTL_CONSTANT_STRING es una macro que inicializa una estructura
 *  UNICODE_STRING en tiempo de compilación (evita llamar a
 *  RtlInitUnicodeString en tiempo de ejecución).
 * ========================================================================= */
UNICODE_STRING  g_DeviceName  = RTL_CONSTANT_STRING(L"\\Device\\ProcEnumDrv");
UNICODE_STRING  g_SymLinkName = RTL_CONSTANT_STRING(L"\\??\\ProcEnumDrv");
PDEVICE_OBJECT  g_DeviceObject = NULL;

/* =========================================================================
 *  PROTOTIPOS DE FUNCIONES
 * =========================================================================
 *
 *  DRIVER_UNLOAD y DRIVER_DISPATCH son tipos definidos en el WDK que
 *  representan las firmas esperadas para las funciones de descarga y
 *  las funciones de despacho de IRPs, respectivamente. Usar estos tipos
 *  permite al compilador verificar que nuestras funciones tienen la
 *  firma correcta.
 * ========================================================================= */
DRIVER_UNLOAD     DriverUnload;           /* Rutina de descarga              */
DRIVER_DISPATCH   DispatchCreateClose;    /* Manejador de CREATE y CLOSE     */
DRIVER_DISPATCH   DispatchDeviceControl;  /* Manejador de DEVICE_CONTROL     */


/* =========================================================================
 *  DriverUnload
 * =========================================================================
 *
 *  Esta función es invocada por el sistema cuando se solicita la descarga
 *  del driver (por ejemplo, con "sc stop ProcEnumDrv" desde la consola o
 *  mediante la herramienta OSR Driver Loader).
 *
 *  Su responsabilidad es liberar todos los recursos que el driver haya
 *  adquirido durante su ejecución. En nuestro caso:
 *
 *    1. Eliminar el enlace simbólico (IoDeleteSymbolicLink) para que las
 *       aplicaciones de usuario ya no puedan abrir el dispositivo.
 *
 *    2. Eliminar el objeto de dispositivo (IoDeleteDevice) para liberar
 *       la memoria del kernel asociada al DEVICE_OBJECT.
 *
 *  Si no se limpian estos recursos, permanecerían en memoria hasta el
 *  siguiente reinicio, consumiendo recursos innecesariamente y pudiendo
 *  causar conflictos si se intenta cargar el driver de nuevo.
 *
 *  PARÁMETROS:
 *    DriverObject — Puntero al DRIVER_OBJECT de nuestro driver. Contiene
 *                   información sobre el driver: punteros a las funciones
 *                   de despacho, la lista de dispositivos creados, etc.
 * ========================================================================= */
VOID DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject   /* Puntero al objeto driver */
)
{
    /* El parámetro DriverObject no se usa directamente en esta función
       porque accedemos al dispositivo a través de la variable global.
       UNREFERENCED_PARAMETER suprime la advertencia del compilador. */
    UNREFERENCED_PARAMETER(DriverObject);

    /* Paso 1: Eliminar el enlace simbólico.
       Es importante eliminarlo ANTES del dispositivo, ya que el enlace
       apunta al dispositivo. Si eliminamos el dispositivo primero,
       el enlace quedaría apuntando a un objeto inexistente. */
    IoDeleteSymbolicLink(&g_SymLinkName);

    /* Paso 2: Eliminar el objeto de dispositivo.
       IoDeleteDevice libera la memoria del DEVICE_OBJECT y lo elimina
       del espacio de nombres del kernel. */
    if (g_DeviceObject != NULL) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }

    /* Mensaje de depuración visible con DebugView (Sysinternals) o WinDbg.
       DbgPrint es el equivalente kernel de printf; escribe en el buffer
       de depuración del kernel. Sólo visible en builds de depuración o
       si se habilita la captura de mensajes del kernel en DebugView. */
    DbgPrint("[ProcEnumDrv] Driver descargado correctamente.\n");
}


/* =========================================================================
 *  DispatchCreateClose
 * =========================================================================
 *
 *  Manejador para los IRPs de tipo IRP_MJ_CREATE e IRP_MJ_CLOSE.
 *
 *  ¿QUÉ ES UN IRP?
 *  Un IRP (I/O Request Packet) es la estructura que el I/O Manager del
 *  kernel de Windows utiliza para representar una operación de E/S.
 *  Cuando una aplicación de usuario llama a CreateFile, ReadFile,
 *  WriteFile, DeviceIoControl o CloseHandle, el I/O Manager genera un
 *  IRP correspondiente y lo envía al driver responsable del dispositivo.
 *
 *  El tipo de operación se identifica por un "código de función mayor"
 *  (Major Function Code):
 *    - IRP_MJ_CREATE  → se genera cuando la aplicación llama a CreateFile
 *    - IRP_MJ_CLOSE   → se genera cuando la aplicación llama a CloseHandle
 *    - IRP_MJ_DEVICE_CONTROL → se genera con DeviceIoControl
 *
 *  Para que nuestra aplicación de usuario pueda abrir un handle al
 *  dispositivo (CreateFile) y cerrarlo (CloseHandle), necesitamos
 *  manejar estos dos tipos de IRP. En nuestro caso simplemente
 *  devolvemos STATUS_SUCCESS (éxito), ya que no necesitamos realizar
 *  ninguna acción especial al abrir o cerrar el dispositivo.
 *
 *  PARÁMETROS:
 *    DeviceObject — Puntero al DEVICE_OBJECT al que va dirigido el IRP.
 *    Irp          — Puntero al IRP que debemos procesar.
 *
 *  RETORNO:
 *    NTSTATUS — STATUS_SUCCESS si todo fue bien.
 * ========================================================================= */
NTSTATUS DispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,  /* Dispositivo destino */
    _In_ PIRP           Irp            /* Paquete de E/S      */
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    /* Establecer el resultado de la operación en el IRP.
       IoStatus.Status contiene el código de resultado (NTSTATUS).
       IoStatus.Information contiene datos adicionales (por ejemplo,
       el número de bytes transferidos); en CREATE/CLOSE es 0. */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    /* Completar el IRP. IoCompleteRequest indica al I/O Manager que
       hemos terminado de procesar esta solicitud. El segundo parámetro
       (IO_NO_INCREMENT) indica que no queremos aumentar la prioridad
       del hilo que esperaba este IRP (no es una operación que deba
       recibir un boost de prioridad). */
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


/* =========================================================================
 *  EnumerateProcesses
 * =========================================================================
 *
 *  Función interna que recorre los procesos del sistema y rellena la
 *  estructura PROCESS_LIST_RESPONSE con el PID y nombre de cada uno.
 *
 *  MÉTODO UTILIZADO: Iteración por PIDs con PsLookupProcessByProcessId
 *  --------------------------------------------------------------------------
 *  Los PIDs en Windows son valores múltiplos de 4 (0, 4, 8, 12, ...).
 *  El PID 0 corresponde al proceso "System Idle" y el PID 4 al proceso
 *  "System". Iteramos desde 0 hasta un máximo razonable (65536) probando
 *  cada múltiplo de 4.
 *
 *  Para cada PID, llamamos a PsLookupProcessByProcessId, que busca en la
 *  tabla interna del kernel (PspCidTable) si existe un proceso con ese
 *  identificador. Si lo encuentra, nos devuelve un puntero al EPROCESS
 *  correspondiente e incrementa su contador de referencias.
 *
 *  SOBRE LA ESTRUCTURA EPROCESS
 *  --------------------------------------------------------------------------
 *  EPROCESS (Executive Process) es la estructura fundamental que el kernel
 *  de Windows mantiene para cada proceso. Reside en memoria del kernel
 *  (no accesible desde modo usuario) y contiene toda la información que
 *  el sistema necesita para gestionar el proceso:
 *
 *    - UniqueProcessId (offset ~0x440 en Win11): el PID del proceso.
 *    - ImageFileName (offset ~0x5A8 en Win11): nombre del ejecutable
 *      (máximo 15 caracteres, ej: "notepad.exe").
 *    - ActiveProcessLinks (offset ~0x448 en Win11): nodo de una lista
 *      doblemente enlazada (LIST_ENTRY) que conecta TODOS los procesos
 *      del sistema. Es la lista que herramientas como el Task Manager
 *      recorren internamente.
 *    - Token: puntero al token de seguridad del proceso (define los
 *      privilegios que tiene).
 *    - Pcb (KPROCESS): subestructura que contiene datos de planificación
 *      del scheduler, como la afinidad de CPU y la prioridad base.
 *
 *  Estos offsets son ESPECÍFICOS de cada versión de Windows y se pueden
 *  obtener con WinDbg ejecutando: dt nt!_EPROCESS
 *  O consultando https://www.vergiliusproject.com/
 *
 *  MÉTODO ALTERNATIVO: Recorrido de ActiveProcessLinks (DKOM)
 *  --------------------------------------------------------------------------
 *  Otro enfoque más avanzado (usado por rootkits reales) consiste en:
 *    1. Obtener el EPROCESS del proceso actual con PsGetCurrentProcess().
 *    2. Leer el campo ActiveProcessLinks (una LIST_ENTRY).
 *    3. Recorrer la lista doblemente enlazada usando Flink (Forward Link)
 *       hasta volver al punto de partida.
 *    4. Para cada nodo, usar CONTAINING_RECORD para obtener el puntero
 *       al EPROCESS completo a partir del puntero al LIST_ENTRY.
 *
 *  Este método requiere conocer el offset exacto de ActiveProcessLinks,
 *  que varía entre versiones de Windows, por lo que es más frágil.
 *  Los rootkits que ocultan procesos (como Nidhogg o BlackAngel) manipulan
 *  esta lista: desenganchan el EPROCESS del proceso objetivo para que
 *  no aparezca al recorrerla, técnica conocida como DKOM (Direct Kernel
 *  Object Manipulation).
 *
 *  En este driver utilizamos el enfoque con PsLookupProcessByProcessId
 *  porque es más robusto (no depende de offsets) y más seguro.
 *
 *  PARÁMETROS:
 *    ProcessList — Puntero a la estructura de salida que se rellenará
 *                  con la información de los procesos encontrados.
 *
 *  RETORNO:
 *    STATUS_SUCCESS siempre (la función es best-effort: si un PID no
 *    corresponde a ningún proceso, simplemente se salta).
 * ========================================================================= */
NTSTATUS EnumerateProcesses(
    _Out_ PPROCESS_LIST_RESPONSE ProcessList   /* Buffer de salida */
)
{
    ULONG count = 0;   /* Contador de procesos encontrados */

    /* Inicializar toda la estructura a ceros para evitar devolver
       datos residuales de la memoria del kernel al modo usuario. */
    RtlZeroMemory(ProcessList, sizeof(PROCESS_LIST_RESPONSE));

    /* Recorrer los posibles PIDs.
       En Windows, los PIDs son múltiplos de 4. El valor máximo real
       depende de la configuración del sistema, pero 65536 es un límite
       razonable que cubre la inmensa mayoría de escenarios. */
    for (ULONG pid = 0; pid < 65536; pid += 4) {

        PEPROCESS process = NULL;  /* Puntero al EPROCESS (inicialmente NULL) */

        /* PsLookupProcessByProcessId:
           - Busca en PspCidTable (tabla interna de handles del kernel)
             el proceso con el PID indicado.
           - Si lo encuentra, devuelve STATUS_SUCCESS y escribe en &process
             un puntero al EPROCESS del proceso.
           - IMPORTANTE: esta función incrementa el contador de referencias
             del objeto EPROCESS (ObReferenceObject internamente). Por ello,
             cuando terminemos de usar el puntero, DEBEMOS llamar a
             ObDereferenceObject para decrementar la referencia. Si no lo
             hacemos, el objeto nunca se liberará (memory leak en kernel). */
        NTSTATUS status = PsLookupProcessByProcessId(
            (HANDLE)(ULONG_PTR)pid,  /* PID a buscar (casteado a HANDLE) */
            &process                 /* Puntero de salida al EPROCESS    */
        );

        /* Si el PID no corresponde a ningún proceso activo, la función
           devuelve STATUS_INVALID_PARAMETER o STATUS_INVALID_CID.
           Simplemente pasamos al siguiente PID. */
        if (!NT_SUCCESS(status)) {
            continue;
        }

        /* --- A partir de aquí tenemos un puntero válido al EPROCESS --- */

        /* PsGetProcessId:
           Función documentada que extrae el campo UniqueProcessId del
           EPROCESS. Devuelve un HANDLE que en realidad contiene el PID
           como valor numérico. Usamos esta función en lugar de acceder
           directamente al offset del campo porque es más portable (no
           depende de la versión de Windows). */
        HANDLE realPid = PsGetProcessId(process);

        /* PsGetProcessImageFileName:
           Función semi-documentada que devuelve un puntero al campo
           ImageFileName del EPROCESS. Este campo es un array de 16 bytes
           (CHAR[16]) dentro de EPROCESS que contiene el nombre del
           ejecutable truncado a 15 caracteres + '\0'.
           Por ejemplo, para "svchost.exe" devuelve "svchost.exe".
           Para nombres largos como "WindowsTerminal.exe" devuelve
           "WindowsTermina" (truncado a 15 chars). */
        PUCHAR imageName = PsGetProcessImageFileName(process);

        /* Almacenar el PID en nuestra estructura de salida.
           Casteamos de HANDLE a ULONG_PTR (entero sin signo del tamaño
           de un puntero) y luego a ULONG (32 bits), ya que los PIDs en
           la práctica caben en 32 bits. */
        ProcessList->Entries[count].Pid = (ULONG)(ULONG_PTR)realPid;

        /* Copiar el nombre de la imagen al buffer de salida.
           Usamos RtlCopyMemory (equivalente kernel de memcpy) para copiar
           un máximo de 15 bytes, y nos aseguramos de que el string termine
           en '\0' por seguridad. */
        if (imageName != NULL) {
            RtlCopyMemory(
                ProcessList->Entries[count].ImageName,  /* Destino */
                imageName,                               /* Origen  */
                MAX_IMAGE_NAME - 1                       /* 15 bytes */
            );
            ProcessList->Entries[count].ImageName[MAX_IMAGE_NAME - 1] = '\0';
        }
        else {
            /* Si imageName es NULL (no debería ocurrir, pero por seguridad),
               dejamos el nombre vacío. */
            ProcessList->Entries[count].ImageName[0] = '\0';
        }

        /* Incrementar el contador de procesos encontrados */
        count++;

        /* ObDereferenceObject:
           PASO CRÍTICO — Decrementar el contador de referencias del
           EPROCESS. Cada llamada exitosa a PsLookupProcessByProcessId
           incrementa la referencia en 1. Si no la decrementamos, el
           objeto EPROCESS permanecerá en memoria incluso después de
           que el proceso termine (el kernel no podrá liberarlo).

           En el contexto de un rootkit, olvidarse de esta llamada es
           un error grave que puede causar inestabilidad del sistema. */
        ObDereferenceObject(process);

        /* Comprobación de límites: no desbordar nuestro array.
           Si hemos alcanzado el máximo, salimos del bucle. */
        if (count >= MAX_PROCESS_COUNT) {
            DbgPrint("[ProcEnumDrv] AVISO: alcanzado limite de %lu procesos.\n",
                     MAX_PROCESS_COUNT);
            break;
        }
    }

    /* Guardar el total de procesos encontrados en la cabecera */
    ProcessList->Count = count;

    DbgPrint("[ProcEnumDrv] Enumeracion completada: %lu procesos encontrados.\n", count);

    return STATUS_SUCCESS;
}


/* =========================================================================
 *  DispatchDeviceControl
 * =========================================================================
 *
 *  Manejador para IRPs de tipo IRP_MJ_DEVICE_CONTROL.
 *
 *  Se invoca cuando una aplicación de modo usuario llama a la función
 *  DeviceIoControl() sobre un handle abierto a nuestro dispositivo.
 *
 *  El I/O Manager empaqueta la solicitud en un IRP que contiene:
 *    - El código IOCTL (qué operación pide el usuario).
 *    - Los buffers de entrada y salida.
 *    - Los tamaños de dichos buffers.
 *
 *  Como usamos METHOD_BUFFERED, el I/O Manager:
 *    1. Asigna un buffer intermedio en memoria del sistema (SystemBuffer).
 *    2. Copia los datos de entrada del usuario a SystemBuffer.
 *    3. Cuando completamos el IRP, el I/O Manager copia los datos de
 *       SystemBuffer de vuelta al buffer de salida del usuario.
 *    Esto evita que el driver acceda directamente a la memoria de usuario,
 *    lo cual sería peligroso (el usuario podría liberar esa memoria
 *    mientras el driver la está usando → BSOD).
 *
 *  IO_STACK_LOCATION:
 *    Cada IRP tiene una pila de "stack locations" — una por cada driver
 *    que participa en el procesamiento. IoGetCurrentIrpStackLocation()
 *    nos da nuestra entrada en la pila, que contiene los parámetros
 *    específicos de nuestra operación (código IOCTL, tamaños de buffer, etc.).
 * ========================================================================= */
NTSTATUS DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,  /* Dispositivo destino */
    _In_ PIRP           Irp            /* Paquete de E/S      */
)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    /* Obtener la ubicación actual en la pila de IRPs.
       Contiene los parámetros del DeviceIoControl: código IOCTL,
       tamaños de buffer de entrada y salida, etc. */
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    NTSTATUS status       = STATUS_SUCCESS;  /* Código de resultado         */
    ULONG    bytesWritten = 0;               /* Bytes devueltos al usuario  */

    /* Comprobar qué IOCTL ha solicitado el usuario */
    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {

        /* ---- IOCTL_ENUM_PROCESSES ---- */
        case IOCTL_ENUM_PROCESSES:
        {
            /* Verificar que el buffer de salida proporcionado por el usuario
               es lo suficientemente grande para contener nuestra respuesta.
               Si es demasiado pequeño, devolvemos STATUS_BUFFER_TOO_SMALL. */
            ULONG outputLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

            if (outputLen < sizeof(PROCESS_LIST_RESPONSE)) {
                DbgPrint("[ProcEnumDrv] Buffer de salida insuficiente: %lu < %llu\n",
                         outputLen, (ULONGLONG)sizeof(PROCESS_LIST_RESPONSE));
                status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            /* Obtener el puntero al buffer del sistema.
               Con METHOD_BUFFERED, el I/O Manager asigna un buffer intermedio
               en pool no paginado y lo apunta desde Irp->AssociatedIrp.SystemBuffer.
               Este buffer es seguro para acceder desde modo kernel. */
            PPROCESS_LIST_RESPONSE processList =
                (PPROCESS_LIST_RESPONSE)Irp->AssociatedIrp.SystemBuffer;

            if (processList == NULL) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Llamar a nuestra función de enumeración de procesos */
            status = EnumerateProcesses(processList);

            if (NT_SUCCESS(status)) {
                /* Indicar cuántos bytes hemos escrito en el buffer.
                   El I/O Manager usará este valor para saber cuántos bytes
                   copiar de vuelta al buffer de usuario. */
                bytesWritten = sizeof(PROCESS_LIST_RESPONSE);
            }

            break;
        }

        /* ---- Cualquier otro IOCTL no reconocido ---- */
        default:
            DbgPrint("[ProcEnumDrv] IOCTL no reconocido: 0x%08X\n",
                     irpSp->Parameters.DeviceIoControl.IoControlCode);
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    /* Completar el IRP con el resultado */
    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information = bytesWritten;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}


/* =========================================================================
 *  DriverEntry — Punto de entrada del driver
 * =========================================================================
 *
 *  DriverEntry es el equivalente kernel de la función main() en modo
 *  usuario. Es la primera función que ejecuta el sistema cuando se carga
 *  el driver (por ejemplo, con "sc start ProcEnumDrv").
 *
 *  Sus responsabilidades son:
 *    1. Crear un DEVICE_OBJECT — la representación del "dispositivo" en
 *       el kernel.
 *    2. Crear un enlace simbólico para que las aplicaciones de usuario
 *       puedan acceder al dispositivo.
 *    3. Registrar las funciones de despacho (dispatch routines) que
 *       manejarán los IRPs enviados al dispositivo.
 *    4. Registrar la función de descarga (DriverUnload) para limpieza.
 *
 *  ESTRUCTURA DRIVER_OBJECT:
 *    El sistema le pasa al driver un puntero a su DRIVER_OBJECT, que es
 *    la estructura que representa al driver en el kernel. Contiene:
 *      - DeviceObject    : lista enlazada de dispositivos creados por el driver.
 *      - DriverUnload    : puntero a la función de descarga.
 *      - MajorFunction[] : array de punteros a funciones (dispatch table).
 *                          Cada posición corresponde a un tipo de IRP
 *                          (IRP_MJ_CREATE, IRP_MJ_CLOSE, IRP_MJ_READ, etc.).
 *                          El driver rellena las posiciones de los IRPs que
 *                          quiere manejar.
 *
 *  PARÁMETROS:
 *    DriverObject — Puntero al DRIVER_OBJECT recién creado por el sistema.
 *    RegistryPath — Ruta del registro donde se almacena la configuración
 *                   del driver (HKLM\SYSTEM\CurrentControlSet\Services\<nombre>).
 *                   No la usamos en este driver, pero es útil para leer
 *                   parámetros de configuración.
 *
 *  RETORNO:
 *    STATUS_SUCCESS si todo se inicializa correctamente.
 *    Otro NTSTATUS de error si falla algún paso (el sistema no cargará
 *    el driver y mostrará un error).
 * ========================================================================= */
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,   /* Objeto driver del sistema */
    _In_ PUNICODE_STRING RegistryPath    /* Ruta del registro         */
)
{
    NTSTATUS status;

    /* No usamos RegistryPath en este driver */
    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[ProcEnumDrv] === Iniciando carga del driver ===\n");

    /* ---------------------------------------------------------------
     *  PASO 1: Crear el DEVICE_OBJECT
     * ---------------------------------------------------------------
     *  IoCreateDevice crea un nuevo objeto de dispositivo y lo asocia
     *  a nuestro driver.
     *
     *  Parámetros:
     *    DriverObject         — Nuestro DRIVER_OBJECT.
     *    0                    — DeviceExtensionSize: bytes adicionales de
     *                           memoria que queremos asociar al dispositivo
     *                           (0 = no necesitamos datos extra).
     *    &g_DeviceName        — Nombre del dispositivo en el Object Manager.
     *    FILE_DEVICE_UNKNOWN  — Tipo de dispositivo (genérico).
     *    FILE_DEVICE_SECURE_OPEN — Característica de seguridad: el sistema
     *                           aplicará los ACLs del dispositivo a cada
     *                           apertura. Buena práctica de seguridad.
     *    FALSE                — Exclusive: si TRUE, sólo un handle puede
     *                           estar abierto a la vez. FALSE permite
     *                           múltiples handles simultáneos.
     *    &g_DeviceObject      — Puntero de salida al DEVICE_OBJECT creado.
     * --------------------------------------------------------------- */
    status = IoCreateDevice(
        DriverObject,               /* Driver al que pertenece       */
        0,                          /* Sin device extension          */
        &g_DeviceName,              /* Nombre del dispositivo        */
        FILE_DEVICE_UNKNOWN,        /* Tipo genérico                 */
        FILE_DEVICE_SECURE_OPEN,    /* Seguridad en apertura         */
        FALSE,                      /* No exclusivo                  */
        &g_DeviceObject             /* [out] Puntero al dispositivo  */
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcEnumDrv] ERROR: IoCreateDevice fallo con 0x%08X\n", status);
        return status;
    }

    DbgPrint("[ProcEnumDrv] Dispositivo creado: %wZ\n", &g_DeviceName);

    /* ---------------------------------------------------------------
     *  PASO 2: Crear el enlace simbólico
     * ---------------------------------------------------------------
     *  IoCreateSymbolicLink crea un enlace en \\??\\ que apunta a
     *  nuestro dispositivo en \\Device\\. Sin este enlace, las
     *  aplicaciones de modo usuario no podrían acceder al dispositivo.
     *
     *  En modo usuario, el enlace se accede como \\\\.\\ProcEnumDrv
     *  (CreateFile(L"\\\\.\\ProcEnumDrv", ...)).
     * --------------------------------------------------------------- */
    status = IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);

    if (!NT_SUCCESS(status)) {
        DbgPrint("[ProcEnumDrv] ERROR: IoCreateSymbolicLink fallo con 0x%08X\n", status);
        /* Si falla la creación del enlace simbólico, debemos limpiar
           el dispositivo que ya creamos para no dejar basura. */
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    DbgPrint("[ProcEnumDrv] Enlace simbolico creado: %wZ -> %wZ\n",
             &g_SymLinkName, &g_DeviceName);

    /* ---------------------------------------------------------------
     *  PASO 3: Registrar las funciones de despacho (dispatch routines)
     * ---------------------------------------------------------------
     *  El DRIVER_OBJECT contiene un array llamado MajorFunction[] con
     *  IRP_MJ_MAXIMUM_FUNCTION + 1 posiciones. Cada posición corresponde
     *  a un tipo de IRP. Al asignar una función a una posición, le
     *  decimos al I/O Manager: "cuando recibas un IRP de este tipo para
     *  mi dispositivo, llama a esta función".
     *
     *  IRP_MJ_CREATE         (0x00) — Cuando el usuario llama a CreateFile.
     *  IRP_MJ_CLOSE          (0x02) — Cuando el usuario llama a CloseHandle.
     *  IRP_MJ_DEVICE_CONTROL (0x0E) — Cuando el usuario llama a DeviceIoControl.
     *
     *  Las posiciones no asignadas quedan con el manejador por defecto del
     *  sistema, que devuelve STATUS_INVALID_DEVICE_REQUEST.
     * --------------------------------------------------------------- */
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

    /* ---------------------------------------------------------------
     *  PASO 4: Registrar la función de descarga
     * ---------------------------------------------------------------
     *  DriverUnload se llama cuando se descarga el driver. Si no la
     *  asignamos, el driver NO se podrá descargar en caliente (sería
     *  necesario reiniciar el sistema). */
    DriverObject->DriverUnload = DriverUnload;

    /* ---------------------------------------------------------------
     *  PASO 5: Configurar los flags del dispositivo
     * ---------------------------------------------------------------
     *  DO_BUFFERED_IO indica al I/O Manager que queremos usar E/S
     *  con buffer (el método más seguro y simple). El I/O Manager
     *  asignará un buffer intermedio en pool del sistema y copiará
     *  los datos entre modo usuario y modo kernel automáticamente.
     *
     *  DO_DEVICE_INITIALIZING es un flag que el sistema activa por
     *  defecto al crear un dispositivo. Indica que el dispositivo
     *  todavía se está inicializando y no debe recibir IRPs. Debemos
     *  desactivarlo cuando terminamos la inicialización para que el
     *  I/O Manager empiece a enviar IRPs al dispositivo.
     *  (Nota: en DriverEntry esto lo hace el sistema automáticamente,
     *  pero es buena práctica hacerlo explícitamente, y es OBLIGATORIO
     *  si se crean dispositivos fuera de DriverEntry).
     * --------------------------------------------------------------- */
    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    DbgPrint("[ProcEnumDrv] === Driver cargado correctamente ===\n");
    DbgPrint("[ProcEnumDrv] Dispositivo accesible desde modo usuario como: \\\\.\\ProcEnumDrv\n");

    return STATUS_SUCCESS;
}
