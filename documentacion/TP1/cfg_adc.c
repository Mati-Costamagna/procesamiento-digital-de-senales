/* Fragmento de board/peripherals.c (generado por Config Tools): LPADC0 */
const lpadc_config_t ADC0_config = {
  .enableInDozeMode = true,
  .conversionAverageMode = kLPADC_ConversionAverage1,
  .enableAnalogPreliminary = false,
  .powerUpDelay = 0x80UL,
  .referenceVoltageSource = kLPADC_ReferenceVoltageAlt1,
  .powerLevelMode = kLPADC_PowerLevelAlt1,
  .triggerPriorityPolicy = kLPADC_ConvPreemptImmediatelyNotAutoResumed,
  .enableConvPause = false,
  .convPauseDelay = 0UL,
  .FIFO0Watermark = 0UL,
  .FIFO1Watermark = 0UL
};
lpadc_conv_command_config_t ADC0_commandsConfig[1] = {
  {
    .sampleChannelMode = kLPADC_SampleChannelSingleEndSideA,
    .channelNumber = 0U,
    .channelBNumber = 0U,
    .chainedNextCommandNumber = 0,
    .enableChannelB = false,
    .enableAutoChannelIncrement = false,
    .loopCount = 0UL,
    .hardwareAverageMode = kLPADC_HardwareAverageCount1,
    .sampleTimeMode = kLPADC_SampleTimeADCK3,
    .hardwareCompareMode = kLPADC_HardwareCompareDisabled,
    .hardwareCompareValueHigh = 0UL,
    .hardwareCompareValueLow = 0UL,
    .conversionResolutionMode = kLPADC_ConversionResolutionStandard,
    .enableWaitTrigger = true
  }
};
lpadc_conv_trigger_config_t ADC0_triggersConfig[1] = {
  {
    .targetCommandId = 1,
    .delayPower = 0UL,
    .channelAFIFOSelect = 0,
    .channelBFIFOSelect = 0,
    .priority = 1,
    .enableHardwareTrigger = true
  }
};

static void ADC0_init(void) {
  /* Initialize LPADC converter */
  LPADC_Init(ADC0_PERIPHERAL, &ADC0_config);
  /* Configure conversion command 1. */
  LPADC_SetConvCommandConfig(ADC0_PERIPHERAL, 1, &ADC0_commandsConfig[0]);
  /* Configure trigger 0. */
  LPADC_SetConvTriggerConfig(ADC0_PERIPHERAL, 0, &ADC0_triggersConfig[0]);
  /* Enable interrupts from LPADC */
  LPADC_EnableInterrupts(ADC0_PERIPHERAL, (kLPADC_Trigger0CompletionInterruptEnable));
  /* Enable interrupt ADC0_IRQN request in the NVIC */
  EnableIRQ(ADC0_IRQN);

}
