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

#include "app/aircopy.h"
#include "app/action.h"
#include "app/spectrum.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk4819.h"
#include "driver/keyboard.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "helper/boot.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/menu.h"
#include "ui/ui.h"

BOOT_Mode_t BOOT_GetMode(void)
{
	KEY_Code_t Keys[2];
	uint8_t i;

	for (i = 0; i < 2; i++) {
		if (GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
			return BOOT_MODE_NORMAL;
		}
		Keys[i] = KEYBOARD_Poll();
		SYSTEM_DelayMs(20);
	}
	if (Keys[0] == Keys[1]) {
		gKeyReading0 = Keys[0];
		gKeyReading1 = Keys[0];
		gDebounceCounter = 2;
		if (Keys[0] == KEY_SIDE1) {
			return BOOT_MODE_F_LOCK;
		}
		if (Keys[0] == KEY_EXIT) {
			return BOOT_MODE_AIRCOPY;
		}
	}

	return BOOT_MODE_NORMAL;
}

void BOOT_ProcessMode(BOOT_Mode_t Mode)
{
	if (Mode == BOOT_MODE_F_LOCK) {
		gMenuCursor = MENU_F_LOCK;
		gSubMenuSelection = gSetting_F_LOCK;
		GUI_SelectNextDisplay(DISPLAY_MENU);
		gMenuListCount = 63;
		gF_LOCK = true;
	} else if (Mode == BOOT_MODE_AIRCOPY) {
		BOARD_FactoryReset(true);
	} else {
	GUI_SelectNextDisplay(DISPLAY_MAIN);
	if (gEeprom.POWER_ON_DISPLAY_MODE != POWER_ON_DISPLAY_MODE_FULL_SCREEN) {
	gEeprom.KEY_LOCK = false;
	gUpdateStatus = true;
	if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_MESSAGE) {
	ACTION_Scan(true);
	} else if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_VOLTAGE) {
	APP_RunSpectrum();
	} else {
	ACTION_FM();
	GUI_SelectNextDisplay(DISPLAY_FM);
	}	
	}
} 
}
