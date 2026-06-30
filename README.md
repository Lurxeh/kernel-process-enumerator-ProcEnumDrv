# kernel-process-enumerator-ProcEnumDrv
---
## Descripción del proyecto

ProcEnumDrv es un driver de modo kernel (Ring 0) para Windows 10/11 x64 que enumera todos los procesos en ejecución del sistema accediendo a la estructura interna **EPROCESS** del kernel.

El objetivo de este proyecto es educativo: explorar las estructuras y funciones internas del kernel de Windows que son la base sobre la que se construyen rootkits reales. El driver demuestra cómo un componente en Ring 0 puede acceder a información del sistema que no es directamente visible desde modo usuario, y cómo se establece la comunicación entre el kernel y las aplicaciones de usuario mediante dispositivos e IOCTLs.


---
## Estructura del proyecto

```
ProcEnumDrv/
├── driver.c      # Código fuente del driver de modo kernel (comentado extensamente)
├── client.c      # Aplicación de modo usuario para comunicarse con el driver
├── common.h      # Definiciones compartidas (IOCTLs, estructuras de datos)
└── README.md     # Este documento
```

---
## Estructura del kernel utilizada: EPROCESS

La estructura central que explora este driver es **EPROCESS** (_EPROCESS), la estructura que el kernel de Windows mantiene por cada proceso en ejecución. Cada proceso activo en el sistema tiene su propio EPROCESS almacenado en memoria del kernel.

### Campos relevantes de EPROCESS

Los offsets indicados corresponden a Windows 11 23H2 (build 22631, x64). Se pueden verificar con WinDbg ejecutando `dt nt!_EPROCESS` o consultando el Vergilius Project.

| Offset  | Campo              | Tipo           | Descripción                                     |
|---------|--------------------|----------------|--------------------------------------------------|
| 0x000   | Pcb                | KPROCESS       | Subestructura con datos del scheduler (afinidad de CPU, prioridad base, etc.) |
| 0x440   | UniqueProcessId    | VOID*          | PID del proceso                                  |
| 0x448   | ActiveProcessLinks | LIST_ENTRY     | Nodo de la lista doblemente enlazada que conecta todos los procesos |
| 0x5A8   | ImageFileName      | CHAR[16]       | Nombre del ejecutable (máx. 15 chars + null)     |
| 0x4B8   | Token              | EX_FAST_REF    | Token de seguridad del proceso                   |

### ActiveProcessLinks y DKOM

El campo `ActiveProcessLinks` es una estructura `LIST_ENTRY` (lista doblemente enlazada) con dos punteros: `Flink` (next) y `Blink` (previous). Todos los EPROCESS del sistema están conectados a través de esta lista, formando un anillo.

```
 ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
 │  EPROCESS A │────>│  EPROCESS B │────>│  EPROCESS C │───> ...
 │  (System)   │<────│  (svchost)  │<────│  (notepad)  │<─── ...
 └─────────────┘     └─────────────┘     └─────────────┘
       ↑ ActiveProcessLinks (Flink/Blink)                  │
       └───────────────────────────────────────────────────┘
```

Los rootkits que ocultan procesos (como los mencionados en los recursos del módulo) manipulan esta lista: desenganchan el `LIST_ENTRY` del proceso objetivo para que al recorrer la lista, ese proceso no aparezca. Esta técnica se conoce como **DKOM** (Direct Kernel Object Manipulation). Sin embargo, el proceso sigue ejecutándose normalmente porque el scheduler del kernel (que usa la subestructura KPROCESS, no ActiveProcessLinks) no se ve afectado.

---

## Funciones del kernel utilizadas

| Función                          | Tipo           | Descripción                                            |
|----------------------------------|----------------|--------------------------------------------------------|
| `IoCreateDevice`                 | Documentada    | Crea un objeto de dispositivo asociado al driver       |
| `IoCreateSymbolicLink`           | Documentada    | Crea un enlace simbólico accesible desde modo usuario  |
| `IoDeleteDevice`                 | Documentada    | Elimina un objeto de dispositivo                       |
| `IoDeleteSymbolicLink`           | Documentada    | Elimina un enlace simbólico                            |
| `IoGetCurrentIrpStackLocation`   | Documentada    | Obtiene la stack location actual del IRP               |
| `IoCompleteRequest`              | Documentada    | Marca un IRP como completado                           |
| `PsLookupProcessByProcessId`     | Documentada    | Obtiene un puntero al EPROCESS dado un PID             |
| `PsGetProcessId`                 | Documentada    | Extrae el PID de un EPROCESS                           |
| `PsGetProcessImageFileName`      | Semi-documentada | Extrae el nombre del ejecutable de un EPROCESS       |
| `ObDereferenceObject`            | Documentada    | Decrementa el contador de referencias de un objeto     |
| `DbgPrint`                       | Documentada    | Escribe mensajes en el buffer de depuración del kernel |
| `RtlZeroMemory`                  | Documentada    | Inicializa un bloque de memoria a ceros                |
| `RtlCopyMemory`                  | Documentada    | Copia un bloque de memoria (equivalente a memcpy)      |

---

## Flujo de comunicación Kernel ↔ Usuario

```
  MODO USUARIO (Ring 3)                    MODO KERNEL (Ring 0)
  ─────────────────────                    ─────────────────────

  1. CreateFileW("\\\\.\\ProcEnumDrv")
         │                                 ┌──────────────────────┐
         └──── IRP_MJ_CREATE ─────────────>│ DispatchCreateClose  │
                                           │   → STATUS_SUCCESS   │
                                           └──────────────────────┘

  2. DeviceIoControl(IOCTL_ENUM_PROCESSES)
         │                                 ┌──────────────────────────┐
         └──── IRP_MJ_DEVICE_CONTROL ─────>│ DispatchDeviceControl    │
                                           │   → EnumerateProcesses() │
                                           │   → Rellena el buffer    │
                                           └──────────────────────────┘

  3. Recibe buffer con lista de procesos
     (I/O Manager copia SystemBuffer → User Buffer)

  4. CloseHandle(hDevice)
         │                                 ┌──────────────────────┐
         └──── IRP_MJ_CLOSE ──────────────>│ DispatchCreateClose  │
                                           │   → STATUS_SUCCESS   │
                                           └──────────────────────┘
```

---

## Compilación

### Requisitos previos

- **Windows Driver Kit (WDK)** — se puede instalar desde Visual Studio Installer
- **Visual Studio 2022** con la carga de trabajo "Desarrollo de escritorio en C++"
- **SDK de Windows** compatible con la versión del WDK

### Compilar el driver

1. Abrir Visual Studio y crear un proyecto de tipo **"Kernel Mode Driver, Empty (KMDF)"** o **"Empty WDM Driver"**.
2. Añadir `driver.c` y `common.h` al proyecto.
3. Compilar en modo **x64 / Debug** o **Release**.
4. El resultado será un archivo `.sys` (por ejemplo, `ProcEnumDrv.sys`).

### Compilar el cliente

Desde un **Developer Command Prompt de Visual Studio**:

```cmd
cl.exe client.c /Fe:client.exe
```

---

## Despliegue y prueba

### Preparar el entorno (máquina virtual)

**IMPORTANTE**: Trabajar siempre en una máquina virtual (VM) para evitar riesgos en el sistema anfitrión. Configurar la VM con:

1. **Desactivar Secure Boot** en la configuración de la VM.
2. **Activar el modo de prueba** para cargar drivers sin firma:
   ```cmd
   bcdedit /set testsigning on
   ```
3. Reiniciar la VM.

### Instalar y cargar el driver

```cmd
:: Copiar el .sys al directorio de drivers
copy ProcEnumDrv.sys C:\Windows\System32\drivers\

:: Crear el servicio del driver
sc create ProcEnumDrv type= kernel binPath= C:\Windows\System32\drivers\ProcEnumDrv.sys

:: Iniciar el driver
sc start ProcEnumDrv

:: Verificar que está cargado
sc query ProcEnumDrv
```

### Ejecutar el cliente

```cmd
client.exe
```

Salida esperada:
```
=== ProcEnumDrv — Cliente de modo usuario ===

[OK] Dispositivo abierto correctamente.
[*]  Solicitando lista de procesos al driver...

PID       NOMBRE
------   ---------------
0         Idle
4         System
128       Registry
...
7832      notepad.exe

[*] Total de procesos: 142
[OK] Recursos liberados. Fin del programa.
```

### Detener y eliminar el driver

```cmd
sc stop ProcEnumDrv
sc delete ProcEnumDrv
```

### Depuración con DebugView

Para ver los mensajes `DbgPrint` del driver:
1. Descargar **DebugView** de Sysinternals.
2. Ejecutar como administrador.
3. Activar **Capture → Capture Kernel** en el menú.
4. Los mensajes del driver aparecerán con el prefijo `[ProcEnumDrv]`.

---

## Consideraciones sobre detección

A continuación se exponen los principales métodos de detección aplicables:

1. **Driver Signature Enforcement (DSE)**: Windows requiere que los drivers estén firmados digitalmente por Microsoft (a través de WHQL) o por un certificado EV. Un driver sin firma sólo puede cargarse en modo de prueba (testsigning) o explotando vulnerabilidades en drivers legítimos firmados (técnica BYOVD — Bring Your Own Vulnerable Driver).

2. **Detección por antivirus/EDR**: los motores de seguridad monitorizan las llamadas a `NtLoadDriver` / `ZwLoadDriver` y pueden detectar la carga de drivers sospechosos. EDRs como Microsoft Defender for Endpoint, CrowdStrike y SentinelOne implementan callbacks del kernel (`PsSetCreateProcessNotifyRoutine`, `PsSetLoadImageNotifyRoutine`) para monitorizar estos eventos.

3. **Integridad de la lista de procesos**: herramientas de detección pueden comparar la lista de procesos obtenida por métodos convencionales (API `EnumProcesses`, `NtQuerySystemInformation`) con la que se obtiene recorriendo la lista de `ActiveProcessLinks` o la `PspCidTable` directa. Discrepancias indicarían ocultación de procesos por DKOM.

4. **PatchGuard (KPP)**: Kernel Patch Protection monitoriza estructuras críticas del kernel (SSDT, IDT, GDT, tablas de dispatch de drivers del sistema). Modificaciones no autorizadas provocan un BSOD con código `CRITICAL_STRUCTURE_CORRUPTION`. Esto dificulta (pero no impide completamente) las técnicas clásicas de rootkit como el hooking de la SSDT.

5. **Virtualization-Based Security (VBS) y HVCI**: en sistemas con VBS habilitado, la integridad del código del kernel se verifica por el hipervisor (Hypervisor-enforced Code Integrity). Esto impide la ejecución de código no firmado en el kernel, incluso si se logra cargar el driver en memoria.

---

## Recursos consultados

### Documentación oficial de Microsoft
- [Introducción a los conceptos de drivers en Windows](https://learn.microsoft.com/es-es/windows-hardware/drivers/gettingstarted/)
- [Guía de diseño para drivers en modo kernel](https://learn.microsoft.com/es-es/windows-hardware/drivers/kernel/)
- [Estructuras opacas del kernel de Windows](https://learn.microsoft.com/es-es/windows-hardware/drivers/kernel/eprocess)

### Estructuras del kernel
- [Vergilius Project — Estructuras del kernel de Windows](https://www.vergiliusproject.com/)
- [Geoff Chappell — Kernel Structures](https://www.geoffchappell.com/studies/windows/km/)
- [CodeMachine — Windows Kernel Data Structures](https://www.codemachine.com/articles/kernel_structures.html)

### Videos educativos
- GuidedHacking — How to make a Kernel Driver (YouTube)
- BlackHat 2020 — Demystifying Modern Windows Rootkits (YouTube)
- BlackHat 2020 — The Art of Emulating Kernel Rootkits (YouTube)

### Rootkits de referencia (estudio de código)
- [Nidhogg — Multi-functional rootkit for red teams](https://github.com/Idov31/Nidhogg)
- [BlackAngel — Windows 11/10 x64 kernel mode rootkit](https://github.com/XaFF-XaFF/Black-Angel-Rootkit)
- [Cronos — Windows 10/11 x64 ring 0 rootkit](https://github.com/XaFF-XaFF/Cronos-Rootkit)

### Técnicas de rootkit
- [RedTeamNotes — Direct Kernel Object Manipulation (DKOM)](https://www.ired.team/miscellaneous-reversing-forensics/windows-kernel-internals/manipulating-activeprocesslinks-to-unlink-processes-in-userland)
- [CyberArk — Fantastic Rootkits and Where to Find Them](https://www.cyberark.com/resources/threat-research-blog/fantastic-rootkits-and-where-to-find-them-part-1)

### Herramientas utilizadas
- Visual Studio 2022 con Windows Driver Kit (WDK)
- DebugView (Sysinternals) para capturar mensajes DbgPrint
- WinDbg para explorar las estructuras del kernel (`dt nt!_EPROCESS`)
- OSR Driver Loader (alternativa a `sc.exe` para carga de drivers)

---
