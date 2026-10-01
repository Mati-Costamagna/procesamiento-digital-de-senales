/* Fragmento de board/peripherals.c (generado por Config Tools): CTIMER0 */
const ctimer_config_t CTIMER0_config = {
  .mode = kCTIMER_TimerMode,
  .input = kCTIMER_Capture_0,
  .prescale = 0
};
const ctimer_match_config_t CTIMER0_Match_3_config = {
  .matchValue = 9374,
  .enableCounterReset = true,
  .enableCounterStop = false,
  .outControl = kCTIMER_Output_Toggle,
  .outPinInitState = false,
  .enableInterrupt = false
};

static void CTIMER0_init(void) {
  /* CTIMER0 peripheral initialization */
  CTIMER_Init(CTIMER0_PERIPHERAL, &CTIMER0_config);
  /* Match channel 3 of CTIMER0 peripheral initialization */
  CTIMER_SetupMatch(CTIMER0_PERIPHERAL, CTIMER0_MATCH_3_CHANNEL, &CTIMER0_Match_3_config);
  /* Start the timer */
  CTIMER_StartTimer(CTIMER0_PERIPHERAL);

}
