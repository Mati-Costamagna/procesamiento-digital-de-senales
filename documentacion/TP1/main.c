/* Fragmento de source/TP1.c: programa principal */
int main(void) {

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    BOARD_InitLEDsPins();
    BOARD_InitBUTTONsPins();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

    set_state(0);
    DisableIRQ(ADC0_IRQn);
    CTIMER_StopTimer(CTIMER0);
    LPADC_DoOffsetCalibration(ADC0);
    LPADC_DoAutoCalibration(ADC0);
    LPADC_DoResetFIFO0(ADC0);
    EnableIRQ(ADC0_IRQn);
    CTIMER_StartTimer(CTIMER0);

//    /* Enter an infinite loop, just incrementing a counter. */
    while(1) {
    }
    return 0 ;
}
