/*
 * Copyright 2016-2026 NXP
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * TP4: filtros adaptativos LMS en la FRDM-MCXN947.
 * NUCLEO 1 (cm33_core1): COMUNICACION.
 *
 * El core1 no tiene FPU ni extensiones DSP, asi que no procesa nada: atiende
 * la UART del MCU-Link (LP_FLEXCOMM4, 115200 8N1) para que el core0 se
 * dedique al LMS.
 *
 *   PC -> UART -> core1 arma la linea -> cola de comandos -> core0
 *   PC <- UART <- core1 vacia la cola <- cola de texto    <- core0
 *
 * Si llega una X al principio de una linea, avisa enseguida al core0 para
 * que corte la corrida de la Parte 1 (sin esperar el Enter).
 *
 * Pines y UART: ConfigTools (TP4_core1.mex -> board/). Los relojes los
 * configura el core0, que ademas arranca a este nucleo con el MCMGR.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "fsl_lpuart.h"
#include "mcmgr.h"


/*******************************************************************************
 * Memoria compartida con el core0.
 * COPIA de la estructura de TP4_core0.c: tienen que ser identicas.
 ******************************************************************************/
#define DIRECCION_COMPARTIDA 0x2004C000UL
#define VALOR_MAGICO         0x54503443UL
#define TX_TAMANO            4096U
#define CMD_CANTIDAD         8U
#define CMD_LARGO            32U

typedef struct
{
    volatile uint32_t magico;        /* el core0 lo escribe cuando termina de inicializar */
    volatile uint32_t core1_listo;   /* el core1 lo pone en 1 cuando la UART esta lista */
    volatile uint32_t tx_escritos;   /* bytes escritos por el core0 */
    volatile uint32_t tx_leidos;     /* bytes mandados por el core1 */
    char tx[TX_TAMANO];
    volatile uint32_t cmd_escritos;  /* lineas escritas por el core1 */
    volatile uint32_t cmd_leidos;    /* lineas ejecutadas por el core0 */
    char cmd[CMD_CANTIDAD][CMD_LARGO];
    volatile uint32_t cortar;        /* el core1 lo pone en 1 al recibir una X */
} memoria_compartida_t;

static memoria_compartida_t *const compartida = (memoria_compartida_t *)DIRECCION_COMPARTIDA;

#define UART            LP_FLEXCOMM4_PERIPHERAL
#define UART_FIFO_TX    8U


/*******************************************************************************
 * Variables
 ******************************************************************************/
static char linea_recibida[CMD_LARGO];
static uint32_t largo_linea;
static bool linea_muy_larga;


/*******************************************************************************
 * Recepcion: bytes de la PC -> lineas de comando para el core0
 ******************************************************************************/
static void entregar_comando(void)
{
    const uint32_t escritos = compartida->cmd_escritos;
    if ((escritos - compartida->cmd_leidos) >= CMD_CANTIDAD)
    {
        return;   /* la cola esta llena: se pierde el comando */
    }
    memcpy(compartida->cmd[escritos % CMD_CANTIDAD], linea_recibida, CMD_LARGO);
    __DMB();   /* la linea completa antes de avisar */
    compartida->cmd_escritos = escritos + 1U;
}

static void recibir(void)
{
    /* errores de la UART (overrun, ruido, framing): se limpian y se sigue */
    const uint32_t errores = kLPUART_RxOverrunFlag | kLPUART_NoiseErrorFlag | kLPUART_FramingErrorFlag;
    LPUART_ClearStatusFlags(UART, LPUART_GetStatusFlags(UART) & errores);

    while (LPUART_GetRxFifoCount(UART) > 0U)
    {
        const char letra = (char)LPUART_ReadByte(UART);

        if ((letra == '\r') || (letra == '\n'))
        {
            if ((largo_linea > 0U) && !linea_muy_larga)
            {
                linea_recibida[largo_linea] = '\0';
                entregar_comando();
            }
            largo_linea     = 0U;
            linea_muy_larga = false;
        }
        else if (largo_linea < (CMD_LARGO - 1U))
        {
            if ((largo_linea == 0U) && ((letra == 'X') || (letra == 'x')))
            {
                compartida->cortar = 1U;   /* cortar ya la corrida de la Parte 1 */
            }
            linea_recibida[largo_linea++] = letra;
        }
        else
        {
            linea_muy_larga = true;   /* se descarta entera */
        }
    }
}


/*******************************************************************************
 * Transmision: cola de texto del core0 -> UART, sin bloquear
 ******************************************************************************/
static void transmitir(void)
{
    const uint32_t leidos     = compartida->tx_leidos;
    const uint32_t pendientes = compartida->tx_escritos - leidos;
    uint32_t lugar            = UART_FIFO_TX - LPUART_GetTxFifoCount(UART);
    uint32_t mandados         = 0U;

    __DMB();   /* leer el contador antes que los datos */
    while ((mandados < pendientes) && (lugar > 0U))
    {
        LPUART_WriteByte(UART, (uint8_t)compartida->tx[(leidos + mandados) % TX_TAMANO]);
        mandados++;
        lugar--;
    }
    if (mandados > 0U)
    {
        __DMB();   /* terminar de leer antes de devolverle el lugar al core0 */
        compartida->tx_leidos = leidos + mandados;
    }
}


/*******************************************************************************
 * main
 ******************************************************************************/
int main(void)
{
    uint32_t direccion = 0U;

    /* Como el ejemplo multicore hello_world del SDK: el core1 no toca los
     * relojes, solo actualiza SystemCoreClock. */
    BOARD_InitBootPins();   /* P1_8 = RX, P1_9 = TX (FC4) */
    SystemCoreClock = CLOCK_GetCoreSysClkFreq();

    /* MCMGR: avisa al core0 que arranco y recibe la direccion de la memoria compartida */
    MCMGR_Init();
    while (MCMGR_GetStartupData(kMCMGR_Core0, &direccion) != kStatus_MCMGR_Success)
    {
    }
    while (compartida->magico != VALOR_MAGICO)
    {
        /* el core0 la inicializa antes de arrancarnos */
    }

    BOARD_InitBootPeripherals();   /* LP_FLEXCOMM4 como LPUART */

    const char *saludo = "\r\n#TP4 core1: UART lista (115200 8N1)\r\n";
    LPUART_WriteBlocking(UART, (const uint8_t *)saludo, strlen(saludo));
    compartida->core1_listo = 1U;

    while (1)
    {
        recibir();
        transmitir();
    }
}
