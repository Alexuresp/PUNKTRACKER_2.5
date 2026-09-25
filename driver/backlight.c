/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "backlight.h"
#include "../bsp/dp32g030/gpio.h"
#include "../driver/gpio.h"
#include "../settings.h"
#include "bsp/dp32g030/pwmplus.h"
#include "bsp/dp32g030/portcon.h"

uint8_t gBacklightCountdown;

void BACKLIGHT_set_brightness(unsigned int brightness)
{
	if (brightness > 9)
		brightness = 9;

	// 0 ~ 1023  logify it though
//	PWM_PLUS0_CH0_COMP = (1023ul * brightness * brightness * brightness) / (BACKLIGHT_MAX_BRIGHTNESS * BACKLIGHT_MAX_BRIGHTNESS * BACKLIGHT_MAX_BRIGHTNESS);
//	PWM_PLUS0_SWLOAD = 1;
	
	PWM_PLUS0_CH0_COMP = (1 << brightness) - 1;
}

void BACKLIGHT_init(void)
{
//	extern uint8_t b;
//	uint32_t PWM_FREQUENCY_HZ =  b * 30;
//	const uint32_t PWM_FREQUENCY_HZ =  3000;
	const uint16_t PWM_FREQUENCY_HZ =  16000;
	
	// 48MHz / 94 / 1024 ~ 500Hz
//	const uint32_t PWM_FREQUENCY_HZ =  1000;
	PWM_PLUS0_CLKSRC |= ((48000000 / 1024 / PWM_FREQUENCY_HZ) << 16);
	PWM_PLUS0_PERIOD = 1023;

//	PORTCON_PORTB_SEL0 &= ~(0 | PORTCON_PORTB_SEL0_B6_MASK);
//	PORTCON_PORTB_SEL0 |= (0 | PORTCON_PORTB_SEL0_B6_BITS_PWMP0_CH0);
	
//	PWM_PLUS0_GEN = PWMPLUS_GEN_CH0_OE_BITS_ENABLE  | PWMPLUS_GEN_CH0_OUTINV_BITS_ENABLE | 0;
//	PWM_PLUS0_CFG = PWMPLUS_CFG_CNT_REP_BITS_ENABLE | PWMPLUS_CFG_COUNTER_EN_BITS_ENABLE | 0;

	PORTCON_PORTB_SEL0 &= ~(PORTCON_PORTB_SEL0_B6_MASK);
	PORTCON_PORTB_SEL0 |= PORTCON_PORTB_SEL0_B6_BITS_PWMP0_CH0;

	PWM_PLUS0_GEN = PWMPLUS_GEN_CH0_OE_BITS_ENABLE  | PWMPLUS_GEN_CH0_OUTINV_BITS_ENABLE;
	PWM_PLUS0_CFG = PWMPLUS_CFG_CNT_REP_BITS_ENABLE | PWMPLUS_CFG_COUNTER_EN_BITS_ENABLE;
}


void BACKLIGHT_TurnOn(void) {
  if (gEeprom.BACKLIGHT > 0) {
//	BACKLIGHT_init();
//	extern uint8_t c;
	BACKLIGHT_set_brightness(5);
//	BACKLIGHT_set_brightness(BACKLIGHT_MAX_BRIGHTNESS);

    switch (gEeprom.BACKLIGHT) {
    default:
    case 1: // 3 sec
      gBacklightCountdown = 3;
      break;
    case 2: // 7 sec
      gBacklightCountdown = 7;
      break;
    case 3: // 10 sec
      gBacklightCountdown = 10;
	  BACKLIGHT_set_brightness(9);
      break;
    case 4: // 15 sec
      gBacklightCountdown = 15;
	  BACKLIGHT_set_brightness(9);
      break;
    case 5: // 30 sec
      gBacklightCountdown = 30;
      break;
    case 6: // 60 sec
      gBacklightCountdown = 60;
      break;
    }

    gBacklightCountdown *= 2;
  }
}

void BACKLIGHT_TurnOff(void){
	BACKLIGHT_set_brightness(0);
}