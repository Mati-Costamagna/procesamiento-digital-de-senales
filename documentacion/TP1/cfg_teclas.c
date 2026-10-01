/* Fragmento de board/pin_mux.c (generado por Config Tools): interrupcion de las teclas */
GPIO0->ICR[23] = ((GPIO0->ICR[23] &
                   /* Mask bits to zero which are setting */
                   (~(GPIO_ICR_IRQS_MASK | GPIO_ICR_ISF_MASK)))

                  /* Interrupt Select: Interrupt, trigger output, or DMA request 1. */
                  | GPIO_ICR_IRQS(ICR_IRQS_irqs1));

/* Interrupt configuration on GPIO0_23 (pin B7): Interrupt on falling edge */
GPIO_SetPinInterruptConfig(BOARD_INITBUTTONSPINS_SW2_GPIO, BOARD_INITBUTTONSPINS_SW2_PIN, kGPIO_InterruptFallingEdge);

/* Interrupt configuration on GPIO0_6 (pin C14): Interrupt on falling edge */
GPIO_SetPinInterruptConfig(BOARD_INITBUTTONSPINS_SW3_GPIO, BOARD_INITBUTTONSPINS_SW3_PIN, kGPIO_InterruptFallingEdge);
