/* Fragmento de source/TP1.c: color del LED RGB segun la velocidad (LED activo en bajo) */
void set_state(int state_number){
    switch (state_number) {
            case 0:
                GPIO_PinWrite(GPIO0, LED_RED_PIN, 1);
                GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
                GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
                break;
            case 1:
                GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
                GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
                GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
                break;
            case 2:
                GPIO_PinWrite(GPIO0, LED_RED_PIN, 1);
                GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 1);
                GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 0);
                break;
            case 3:
                GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
                GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 1);
                GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 1);
                break;
            default:
                GPIO_PinWrite(GPIO0, LED_RED_PIN, 0);
                GPIO_PinWrite(GPIO0, LED_GREEN_PIN, 0);
                GPIO_PinWrite(GPIO1, LED_BLUE_PIN, 0);
                break;
        }
}
