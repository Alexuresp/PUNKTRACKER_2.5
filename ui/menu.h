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

#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdbool.h>
#include <stdint.h>

enum {
	MENU_SQL		= 0,
	MENU_STEP,
	MENU_1_CALL,
	MENU_S_LIST,
	MENU_TDR,
	MENU_WX,
	MENU_ABR,
	MENU_D_PRE,
	MENU_MEM_CH,
	MENU_SFT_D,
	MENU_OFFSET,
	MENU_R_DCS,
	MENU_R_CTCS,
	MENU_T_DCS,
	MENU_T_CTCS,
	MENU_W_N,
	MENU_TXP,
	MENU_AM,
	MENU_AFC,
	MENU_VOX,
	MENU_SC_REV,
	MENU_D_HOLD,
	MENU_PONMSG,
	MENU_350TX,
	MENU_VOICE,
	MENU_SLOWS,
	MENU_MDF,
	MENU_SCR,
	MENU_ROGER,
	MENU_AMFT,
	MENU_SKIP_A,
	MENU_END_A,
	MENU_SKIP_B,
	MENU_END_B,
#if defined(ENABLE_MEMSKIP)
	MENU_SKIP_C,
	MENU_END_C,
	MENU_SKIP_D,
	MENU_END_D,
	MENU_SKIP_E,
	MENU_END_E,
	MENU_SKIP_F,
	MENU_END_F,
	MENU_SKIP_G,
	MENU_END_G,
	MENU_SKIP_H,
	MENU_END_H,
	MENU_SKIP_J,
	MENU_END_J,
	MENU_SKIP_K,
	MENU_END_K,
#endif
	MENU_AUTOLK,
	MENU_BCL,
	MENU_SAVE,
	MENU_BEEP,
	MENU_TOT,
/*	MENU_S_ADD1,
	MENU_S_ADD2,*/
	MENU_STE,
	MENU_RP_STE,
	MENU_MIC,
/*	MENU_SLIST1,
	MENU_SLIST2,
	MENU_ANI_ID,
	MENU_UPCODE,
	MENU_DWCODE,
	MENU_D_ST,
	MENU_D_RSP,*/
/*	MENU_PTT_ID,
	MENU_D_DCD,
	MENU_D_LIST,*/
//	MENU_VOL,
	MENU_DEL_CH,
//	MENU_RESET,
	MENU_F_LOCK,
//	MENU_200TX,
	MENU_ALL_TX,
	MENU_SCREN,
	MENU_500TX,
};

extern bool gIsInSubMenu;

extern uint8_t gMenuCursor;
extern int8_t gMenuScrollDirection;
extern uint32_t gSubMenuSelection;

void UI_DisplayMenu(void);

#endif
