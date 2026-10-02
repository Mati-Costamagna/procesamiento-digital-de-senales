/* Fragmento de source/TP1.c: constantes, variables globales y buffer circular */
#define MAX_FREQ CLOCK_GetCTimerClkFreq(0)

#define VEL_0     9375
#define VEL_1     4687
#define VEL_2    3409
#define VEL_3    1704
#define VEL_4    1562

#define LED_RED_PIN BOARD_INITLEDSPINS_LED_RED_PIN
#define LED_GREEN_PIN BOARD_INITLEDSPINS_LED_GREEN_PIN
#define LED_BLUE_PIN BOARD_INITLEDSPINS_LED_BLUE_PIN

#define SAMPLES_NUM 512

volatile int state_counter = 0;
volatile int flag_adc = 1;

volatile uint32_t last_adc = 0;   /* global */
volatile uint32_t last_dac = 0;   /* global */

uint32_t sample_vel[5] = {VEL_0, VEL_1, VEL_2, VEL_3, VEL_4};
volatile q15_t samples[SAMPLES_NUM];
