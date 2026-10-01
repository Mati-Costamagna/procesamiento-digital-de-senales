/* Fragmento de board/pin_mux.c (generado por Config Tools): ruteo del disparo y salida del DAC */
/* Timer CTIMER0 Match 3 is selected as trigger input for ADC0 channel 0 */
INPUTMUX_AttachSignal(INPUTMUX0, 0U, kINPUTMUX_Ctimer0M3ToAdc0Trigger);

/* PORT0_19 (pin C9) is configured as CT0_MAT3 */
PORT_SetPinMux(PORT0, 19U, kPORT_MuxAlt4);

/* PORT4_2 (pin T1) is configured as DAC0_OUT */
PORT_SetPinMux(PORT4, 2U, kPORT_MuxAlt0);
