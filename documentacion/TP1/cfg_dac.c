/* Fragmento de board/peripherals.c (generado por Config Tools): DAC0 y VREF0 */
const dac_config_t DAC0_config = {
  .fifoWatermarkLevel = 0UL,
  .fifoTriggerMode = kDAC_FIFOTriggerByHardwareMode,
  .fifoWorkMode = kDAC_FIFODisabled,
  .referenceVoltageSource = kDAC_ReferenceVoltageSourceAlt3,
  .referenceCurrentSource = kDAC_ReferenceCurrentSourcePtat,
  .enableOpampBuffer = true,
  .periodicTriggerNumber = 0UL,
  .periodicTriggerWidth = 0UL,
  .syncTime = 1UL,
  .enableLowerLowPowerMode = false,
};

static void DAC0_init(void) {
  /* Power up analog module in SPC */
  SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlDac0);
  /* Initialize the LPDAC */
  DAC_Init(DAC0_PERIPHERAL, &DAC0_config);
  /* Enable the LPDAC */
  DAC_Enable(DAC0_PERIPHERAL, true);

}

static void VREF0_init(void) {
    /* Power up VREF */
    vref_config_t vrefConfig;

    /* enable VREF */
    SPC_EnableActiveModeAnalogModules(SPC0, kSPC_controlVref);

    VREF_GetDefaultConfig(&vrefConfig);
    vrefConfig.bufferMode = kVREF_ModeBandgapOnly;
    /* Initialize VREF module, the VREF module is only used to supply the bias current for LPADC. */
    VREF_Init(VREF0_PERIPHERAL, &vrefConfig);
}
