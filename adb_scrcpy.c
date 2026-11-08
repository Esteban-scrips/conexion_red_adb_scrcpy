#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 5555
#define BUFFER_SIZE 8192

char ADB_PATH[MAX_PATH];
char SCRCPY_PATH[MAX_PATH];


/* =========================================================
   Obtener carpeta donde está el programa
   ========================================================= */

void obtener_ruta_programa()
{
    char ruta[MAX_PATH];

    GetModuleFileNameA(NULL, ruta, MAX_PATH);

    char *ultima_barra = strrchr(ruta, '\\');

    if (ultima_barra != NULL)
        *(ultima_barra + 1) = '\0';

    snprintf(
        ADB_PATH,
        MAX_PATH,
        "%sadb.exe",
        ruta
    );

    snprintf(
        SCRCPY_PATH,
        MAX_PATH,
        "%sscrcpy.exe",
        ruta
    );
}


/* =========================================================
   Ejecutar comando y capturar salida
   ========================================================= */

int ejecutar_comando(
    const char *comando,
    char *salida,
    size_t tam
)
{
    FILE *pipe;

    salida[0] = '\0';

    pipe = _popen(comando, "r");

    if (pipe == NULL)
        return -1;

    size_t usados = 0;

    while (fgets(
        salida + usados,
        (int)(tam - usados),
        pipe
    ) != NULL)
    {
        usados = strlen(salida);

        if (usados >= tam - 1)
            break;
    }

    int resultado = _pclose(pipe);

    return resultado;
}


/* =========================================================
   Buscar dispositivo USB
   ========================================================= */

int obtener_dispositivo_usb(
    char *serial,
    size_t tam
)
{
    char comando[MAX_PATH + 100];
    char salida[BUFFER_SIZE];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" devices",
        ADB_PATH
    );

    if (ejecutar_comando(
        comando,
        salida,
        sizeof(salida)
    ) != 0)
    {
        return -1;
    }

    char *linea = strtok(salida, "\r\n");

    while (linea != NULL)
    {
        char dispositivo[256];
        char estado[256];

        if (sscanf(
            linea,
            "%255s %255s",
            dispositivo,
            estado
        ) == 2)
        {
            if (strcmp(estado, "device") == 0)
            {
                strncpy(
                    serial,
                    dispositivo,
                    tam - 1
                );

                serial[tam - 1] = '\0';

                return 0;
            }
        }

        linea = strtok(NULL, "\r\n");
    }

    return -1;
}


/* =========================================================
   Obtener IP del teléfono
   ========================================================= */

int obtener_ip(
    const char *serial,
    char *ip,
    size_t tam
)
{
    char comando[MAX_PATH + 300];
    char salida[BUFFER_SIZE];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" -s %s shell ip route",
        ADB_PATH,
        serial
    );

    if (ejecutar_comando(
        comando,
        salida,
        sizeof(salida)
    ) != 0)
    {
        return -1;
    }

    /*
       Buscamos:

       src 192.168.1.25
    */

    char *src = strstr(salida, "src ");

    if (src != NULL)
    {
        src += 4;

        if (sscanf(
            src,
            "%63s",
            ip
        ) == 1)
        {
            ip[strcspn(ip, "\r\n")] = '\0';

            /*
               Eliminar posibles caracteres extra
            */

            char *espacio = strchr(ip, ' ');

            if (espacio != NULL)
                *espacio = '\0';

            return 0;
        }
    }

    return -1;
}


/* =========================================================
   Activar ADB TCP/IP
   ========================================================= */

int activar_tcpip(
    const char *serial
)
{
    char comando[MAX_PATH + 300];
    char salida[BUFFER_SIZE];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" -s %s tcpip %d",
        ADB_PATH,
        serial,
        PORT
    );

    printf("[*] Ejecutando: adb tcpip %d\n\n", PORT);

    ejecutar_comando(
        comando,
        salida,
        sizeof(salida)
    );

    printf("%s\n", salida);

    /*
       Normalmente devuelve:

       restarting in TCP mode port: 5555
    */

    if (strstr(
        salida,
        "restarting in TCP mode"
    ) != NULL)
    {
        return 0;
    }

    /*
       Algunas versiones pueden escribir
       el mensaje ligeramente diferente.
    */

    if (strstr(
        salida,
        "TCP mode"
    ) != NULL)
    {
        return 0;
    }

    return -1;
}


/* =========================================================
   Conectar por IP
   ========================================================= */

int conectar_red(
    const char *ip
)
{
    char comando[MAX_PATH + 300];
    char salida[BUFFER_SIZE];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" connect %s:%d",
        ADB_PATH,
        ip,
        PORT
    );

    printf(
        "[*] Conectando a %s:%d...\n\n",
        ip,
        PORT
    );

    ejecutar_comando(
        comando,
        salida,
        sizeof(salida)
    );

    printf("%s\n", salida);

    /*
       adb connect normalmente devuelve:

       connected to 192.168.1.25:5555
    */

    if (strstr(
        salida,
        "connected to"
    ) != NULL)
    {
        return 0;
    }

    /*
       También puede devolver:

       already connected to
    */

    if (strstr(
        salida,
        "already connected"
    ) != NULL)
    {
        return 0;
    }

    return -1;
}


/* =========================================================
   Comprobar estado ADB
   ========================================================= */

int comprobar_conexion(
    const char *ip
)
{
    char comando[MAX_PATH + 300];
    char salida[BUFFER_SIZE];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" -s %s:%d get-state",
        ADB_PATH,
        ip,
        PORT
    );

    ejecutar_comando(
        comando,
        salida,
        sizeof(salida)
    );

    /*
       Debe devolver:

       device
    */

    char *inicio = salida;

    while (*inicio == ' ' ||
           *inicio == '\r' ||
           *inicio == '\n' ||
           *inicio == '\t')
    {
        inicio++;
    }

    if (strncmp(
        inicio,
        "device",
        6
    ) == 0)
    {
        return 0;
    }

    return -1;
}


/* =========================================================
   Iniciar SCRCPY
   ========================================================= */

int iniciar_scrcpy(
    const char *ip
)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(
        &si,
        sizeof(si)
    );

    ZeroMemory(
        &pi,
        sizeof(pi)
    );

    si.cb = sizeof(si);

    char comando[MAX_PATH + 300];

    snprintf(
        comando,
        sizeof(comando),
        "\"%s\" -s %s:%d",
        SCRCPY_PATH,
        ip,
        PORT
    );

    printf(
        "[*] Iniciando scrcpy...\n"
    );

    printf(
        "    %s\n\n",
        comando
    );

    /*
       CreateProcess necesita un buffer modificable.
    */

    char linea[MAX_PATH + 300];

    strncpy(
        linea,
        comando,
        sizeof(linea) - 1
    );

    linea[sizeof(linea) - 1] = '\0';

    if (!CreateProcessA(
        NULL,
        linea,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    ))
    {
        return -1;
    }

    /*
       No esperamos a scrcpy.
       El programa continúa ejecutándose.
    */

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return 0;
}


/* =========================================================
   MAIN
   ========================================================= */

int main()
{
    system("chcp 65001 > nul");

    obtener_ruta_programa();

    printf("\n");
    printf("============================================\n");
    printf("          ADB SCRCPY CONNECTOR\n");
    printf("============================================\n\n");


    /* -----------------------------------------------------
       Comprobar ADB
       ----------------------------------------------------- */

    if (GetFileAttributesA(ADB_PATH) ==
        INVALID_FILE_ATTRIBUTES)
    {
        printf(
            "[ERROR] No se encontro adb.exe\n\n"
        );

        printf(
            "Debe estar en:\n%s\n\n",
            ADB_PATH
        );

        system("pause");

        return 1;
    }


    /* -----------------------------------------------------
       Comprobar SCRCPY
       ----------------------------------------------------- */

    if (GetFileAttributesA(SCRCPY_PATH) ==
        INVALID_FILE_ATTRIBUTES)
    {
        printf(
            "[ERROR] No se encontro scrcpy.exe\n\n"
        );

        printf(
            "Debe estar en:\n%s\n\n",
            SCRCPY_PATH
        );

        system("pause");

        return 1;
    }


    /* -----------------------------------------------------
       Buscar dispositivo
       ----------------------------------------------------- */

    char serial[256];

    printf(
        "[1] Buscando dispositivo USB...\n\n"
    );

    if (obtener_dispositivo_usb(
        serial,
        sizeof(serial)
    ) != 0)
    {
        printf(
            "[ERROR] No se encontro un dispositivo ADB.\n\n"
        );

        printf(
            "Compruebe:\n"
        );

        printf(
            "  - Depuracion USB activada\n"
        );

        printf(
            "  - Cable USB conectado\n"
        );

        printf(
            "  - Autorizacion ADB aceptada\n\n"
        );

        printf(
            "ESTADO: NO CONECTADO\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] Dispositivo USB encontrado\n"
    );

    printf(
        "     Serial: %s\n\n",
        serial
    );


    /* -----------------------------------------------------
       Obtener IP
       ----------------------------------------------------- */

    char ip[64];

    printf(
        "[2] Obteniendo IP del telefono...\n\n"
    );

    if (obtener_ip(
        serial,
        ip,
        sizeof(ip)
    ) != 0)
    {
        printf(
            "[ERROR] No se pudo obtener la IP.\n\n"
        );

        printf(
            "Asegurese de que el telefono esta conectado\n"
        );

        printf(
            "a la misma red que la PC.\n\n"
        );

        printf(
            "ESTADO: NO CONECTADO\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] IP detectada: %s\n\n",
        ip
    );


    /* -----------------------------------------------------
       Activar TCP/IP
       ----------------------------------------------------- */

    printf(
        "[3] Activando ADB por TCP/IP...\n"
    );

    if (activar_tcpip(serial) != 0)
    {
        printf(
            "\n[ERROR] No se pudo activar ADB TCP/IP.\n\n"
        );

        printf(
            "ESTADO: NO CONECTADO\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] ADB TCP/IP activado\n"
    );

    printf(
        "     Puerto: %d\n\n",
        PORT
    );


    /* -----------------------------------------------------
       Esperar al servidor ADB
       ----------------------------------------------------- */

    Sleep(2000);


    /* -----------------------------------------------------
       Conectar
       ----------------------------------------------------- */

    printf(
        "[4] Estableciendo conexion de red...\n\n"
    );

    if (conectar_red(ip) != 0)
    {
        printf(
            "[ERROR] adb connect fallo.\n\n"
        );

        printf(
            "ESTADO: CONEXION FALLIDA\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] Conexion ADB por red establecida\n\n"
    );


    /* -----------------------------------------------------
       Verificar realmente la conexión
       ----------------------------------------------------- */

    printf(
        "[5] Verificando conexion...\n"
    );

    if (comprobar_conexion(ip) != 0)
    {
        printf(
            "\n[ERROR] El dispositivo no responde a ADB.\n\n"
        );

        printf(
            "ESTADO: CONEXION FALLIDA\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] ADB responde correctamente\n\n"
    );


    /* -----------------------------------------------------
       Estado final
       ----------------------------------------------------- */

    printf(
        "============================================\n"
    );

    printf(
        "       CONECTADO CORRECTAMENTE\n"
    );

    printf(
        "============================================\n\n"
    );

    printf(
        "Dispositivo : %s\n",
        serial
    );

    printf(
        "IP          : %s\n",
        ip
    );

    printf(
        "Puerto      : %d\n",
        PORT
    );

    printf(
        "Conexion     : ADB TCP/IP\n\n"
    );


    /* -----------------------------------------------------
       SCRCPY
       ----------------------------------------------------- */

    if (iniciar_scrcpy(ip) != 0)
    {
        printf(
            "[ERROR] No se pudo iniciar scrcpy.\n"
        );

        system("pause");

        return 1;
    }

    printf(
        "[OK] scrcpy iniciado correctamente\n\n"
    );

    printf(
        "============================================\n"
    );

    printf(
        "       DISPOSITIVO CONECTADO\n"
    );

    printf(
        "============================================\n\n"
    );

    printf(
        "Puede cerrar esta ventana cuando termine.\n"
    );

    printf(
        "scrcpy continuara funcionando.\n\n"
    );

    system("pause");

    return 0;
}