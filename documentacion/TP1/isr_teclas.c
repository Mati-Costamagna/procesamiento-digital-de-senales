/* Fragmento de source/TP1.c: interrupciones de las teclas SW3 (canal 0) y SW2 (canal 1) */
void GPIO0_INT_0_IRQHANDLER(void) {
  /* Get pin flags 0 */
  uint32_t pin_flags0 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 0U);

  /* Place your interrupt code here */
  state_counter = (state_counter + 1) % (sizeof(sample_vel) / sizeof(sample_vel[0]));
  setup_new_match(sample_vel[state_counter]);
  set_state(state_counter);  /* Clear pin flags 0 */
  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags0, 0U);

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}

void GPIO0_INT_1_IRQHANDLER(void) {
  /* Get pin flags 1 */
  uint32_t pin_flags1 = GPIO_GpioGetInterruptChannelFlags(GPIO0, 1U);

  /* Place your interrupt code here */
  flag_adc = ~flag_adc & 1;
  /* Clear pin flags 1 */
  GPIO_GpioClearInterruptChannelFlags(GPIO0, pin_flags1, 1U);

  /* Add for ARM errata 838869, affects Cortex-M4, Cortex-M4F
     Store immediate overlapping exception return operation might vector to incorrect interrupt. */
  #if defined __CORTEX_M && (__CORTEX_M == 4U)
    __DSB();
  #endif
}
