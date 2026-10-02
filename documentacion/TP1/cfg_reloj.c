/* Fragmento de board/clock_config.c (generado por Config Tools): PLL0 a 150 MHz y relojes de CTIMER0, ADC0 y DAC0 */
/*!< Set up PLL0 */
const pll_setup_t pll0Setup = {
    .pllctrl = SCG_APLLCTRL_SOURCE(1U) | SCG_APLLCTRL_SELI(27U) | SCG_APLLCTRL_SELP(13U),
    .pllndiv = SCG_APLLNDIV_NDIV(8U),
    .pllpdiv = SCG_APLLPDIV_PDIV(1U),
    .pllmdiv = SCG_APLLMDIV_MDIV(50U),
    .pllRate = 150000000U
};
CLOCK_SetPLL0Freq(&pll0Setup);                       /*!< Configure PLL0 to the desired values */
CLOCK_SetPll0MonitorMode(kSCG_Pll0MonitorDisable);    /* Pll0 Monitor is disabled */

/*!< Set up clock selectors  */
CLOCK_AttachClk(kPLL0_to_MAIN_CLK);
CLOCK_AttachClk(kFRO_HF_to_ADC0);                 /*!< Switch ADC0 to FRO_HF */
CLOCK_AttachClk(kFRO_HF_to_ADC1);                 /*!< Switch ADC1 to FRO_HF */
CLOCK_AttachClk(kFRO_HF_to_DAC0);                 /*!< Switch DAC0 to FRO_HF */
CLOCK_AttachClk(kPLL0_to_CTIMER0);                 /*!< Switch CTIMER0 to PLL0 */

/*!< Set up dividers */
CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U);           /*!< Set AHBCLKDIV divider to value 1 */
CLOCK_SetClkDiv(kCLOCK_DivAdc0Clk, 1U);           /*!< Set ADC0CLKDIV divider to value 1 */
CLOCK_SetClkDiv(kCLOCK_DivAdc1Clk, 1U);           /*!< Set ADC1CLKDIV divider to value 1 */
CLOCK_SetClkDiv(kCLOCK_DivDac0Clk, 1U);           /*!< Set DAC0CLKDIV divider to value 1 */
CLOCK_SetClkDiv(kCLOCK_DivCtimer0Clk, 1U);           /*!< Set CTIMER0CLKDIV divider to value 1 */
