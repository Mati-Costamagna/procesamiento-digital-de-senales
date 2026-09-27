/* Fragmento de source/TP1.c: reprogramacion del CTIMER0 al cambiar de velocidad */
void setup_new_match(int m_val){

    uint32_t match_val = m_val;
    CTIMER_StopTimer(CTIMER0_PERIPHERAL);
    const ctimer_match_config_t CTIMER0_Match_3_config = {
      .matchValue = match_val,
      .enableCounterReset = true,
      .enableCounterStop = false,
      .outControl = kCTIMER_Output_Toggle,
      .outPinInitState = false,
      .enableInterrupt = false
    };
    CTIMER_SetupMatch(CTIMER0_PERIPHERAL, CTIMER0_MATCH_3_CHANNEL, &CTIMER0_Match_3_config);
    CTIMER_Reset(CTIMER0_PERIPHERAL);
    CTIMER_StartTimer(CTIMER0_PERIPHERAL);
}
