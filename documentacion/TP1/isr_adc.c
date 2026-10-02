/* Fragmento de source/TP1.c: interrupcion de fin de conversion del ADC0 */
void ADC0_IRQHANDLER(void) {
  uint32_t trigger_status_flag;
  uint32_t status_flag;
  /* Trigger interrupt flags */
  trigger_status_flag = LPADC_GetTriggerStatusFlags(ADC0_PERIPHERAL);
  /* Interrupt flags */
  status_flag = LPADC_GetStatusFlags(ADC0_PERIPHERAL);
  /* Clears trigger interrupt flags */
  LPADC_ClearTriggerStatusFlags(ADC0_PERIPHERAL, trigger_status_flag);
  /* Clears interrupt flags */
  LPADC_ClearStatusFlags(ADC0_PERIPHERAL, status_flag);

  /* Place your code here */
  static uint16_t sample_count = 0;
  static uint16_t dac_count = 0;
  lpadc_conv_result_t result;

  if (LPADC_GetConvResult(ADC0_PERIPHERAL, &result, 0)) {
      uint16_t v = (result.convValue >> 4) & 0x0FFF;   /* solo si single-ended 12 bit */
      uint16_t out;
      if (flag_adc) { samples[sample_count] = (q15_t)result.convValue; sample_count = (sample_count + 1) % SAMPLES_NUM; out = v; }
      else          { out = (uint16_t)samples[dac_count]; dac_count = (dac_count + 1) % SAMPLES_NUM; }
      DAC_SetData(DAC0, out);
  }

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}
