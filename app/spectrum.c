/* Copyright 2023 fagci
 * https://github.com/fagci
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

#include "audio.h"
#include "../app/spectrum.h"
#include "finput.h"
#include "../misc.h"
#include "../driver/backlight.h"
#include <string.h>

#define F_MIN FrequencyBandTable[0].lower
#define F_MAX FrequencyBandTable[ARRAY_SIZE(FrequencyBandTable) - 1].upper

const uint16_t RSSI_MAX_VALUE = 65535;

static uint32_t initialFreq;
static char String[32];

bool isInitialized = false;
bool monitorMode = false;
bool redrawStatus = true;
bool redrawScreen = false;
bool newScanStart = true;
bool preventKeypress = false;

bool isListening = false;
bool isTransmitting = false;
bool PageScan = false;
bool ScanPaus = false;
bool AutoPreset = true;
uint8_t PageTick = 0;
uint8_t TempRssi = 0;
uint16_t AverRssi = 0;
uint16_t delu = 800;
int8_t ManRssi;
uint16_t tmp13 = 0;
uint32_t bgnFreq = 0;
uint32_t endFreq = 0;
uint32_t memf = 0;
uint32_t fbl[100] = {0};
uint8_t fbi = 0;
bool blck = false;
uint32_t loot[100] = {0};
uint8_t loi = 0;
bool ltck = false;
uint8_t iol = 0; 
uint16_t p_rssi=0;
bool RScan = false;

State currentState = SPECTRUM, previousState = SPECTRUM;

PeakInfo peak;
ScanInfo scanInfo;
KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

const char *bwOptions[] = {"  25k", "12.5k", "6.25k"};
const uint8_t modulationTypeTuneSteps[] = {100, 50, 10};

SpectrumSettings settings = {
    .stepsCount = STEPS_128,
    .scanStepIndex = STEP_12_5kHz,
    .frequencyChangeStep = 160000,
    .rssiTriggerLevel = 100,
    .backlightState = true,
    .listenBw = BK4819_FILTER_BW_WIDE,
    .modulationType = MOD_FM,
};

uint32_t fMeasure = 0;
uint32_t fTx = 0;
uint32_t currentFreq;
uint16_t rssiHistory[128] = {0};
bool blacklist[128] = {false};

static const RegisterSpec registerSpecs[] = {
    {},
    {"LNAs", 0x13, 8, 0b11, 1},
    {"LNA", 0x13, 5, 0b111, 1},
    {"PGA", 0x13, 0, 0b111, 1},
    {"MIX", 0x13, 3, 0b11, 1},

    {"IF", 0x3D, 0, 0xFFFF, 100},
    {"DEV", 0x40, 0, 0xFFF, 10},
    {"CMP", 0x31, 3, 1, 1},
    {"MIC", 0x7D, 0, 0xF, 1},
};

static uint16_t registersBackup[128];
static const uint8_t registersToBackup[] = {
    0x13, 0x30, 0x31, 0x37, 0x3D, 0x40, 0x43, 0x47, 0x48, 0x7D, 0x7E,
};

static MovingAverage mov = {{128}, {}, 255, 128, 0, 0};
static const uint8_t MOV_N = ARRAY_SIZE(mov.buf);

uint8_t menuState = 0;
#ifdef ENABLE_ALL_REGISTERS
uint8_t hiddenMenuState = 0;
#endif

uint16_t listenT = 0;

uint16_t batteryUpdateTimer = 0;
bool isMovingInitialized = false;
uint8_t lastStepsCount = 0;

VfoState_t txAllowState;

static void UpdateRegMenuValue(RegisterSpec s, bool add) {
  uint16_t v = BK4819_GetRegValue(s);

  if (add && v <= s.mask - s.inc) {
    v += s.inc;
  } else if (!add && v >= 0 + s.inc) {
    v -= s.inc;
  }

  BK4819_SetRegValue(s, v);
  redrawScreen = true;
}

// Utility functions

KEY_Code_t GetKey() {
  KEY_Code_t btn = KEYBOARD_Poll();
  if (btn == KEY_INVALID && !GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_PTT)) {
    btn = KEY_PTT;
  }
  return btn;
}

void SetState(State state) {
  previousState = currentState;
  currentState = state;
  redrawScreen = true;
  redrawStatus = true;
}

// Radio functions

static void BackupRegisters() {
  for (uint8_t i = 0; i < ARRAY_SIZE(registersToBackup); ++i) {
    uint8_t regNum = registersToBackup[i];
    registersBackup[regNum] = BK4819_ReadRegister(regNum);
  }
}

static void RestoreRegisters() {
  for (uint8_t i = 0; i < ARRAY_SIZE(registersToBackup); ++i) {
    uint8_t regNum = registersToBackup[i];
    BK4819_WriteRegister(regNum, registersBackup[regNum]);
	if (gSetting_500TX == true){
	BK4819_WriteRegister(0x3E, 36458);
	}
  }
}

static void SetF(uint32_t f, bool precise) {
  BK4819_TuneTo(fMeasure = f, precise);
}
static void SetTxF(uint32_t f) { BK4819_TuneTo(fTx = f, true); }

// Spectrum related

bool IsPeakOverLevel() { return peak.rssi >= settings.rssiTriggerLevel; }

static void ResetPeak() {
  peak.t = 0;
  peak.rssi = 0;
}

bool IsCenterMode() { return settings.scanStepIndex < STEP_1_0kHz; }
uint8_t GetStepsCount() { return 128 >> settings.stepsCount; }
uint16_t GetScanStep() { return StepFrequencyTable[settings.scanStepIndex]; }
uint32_t GetBW() { return GetStepsCount() * GetScanStep(); }
uint32_t GetFStart() {
  return IsCenterMode() ? currentFreq - (GetBW() >> 1) : currentFreq;
}
uint32_t GetFEnd() { return currentFreq + GetBW(); }

static void MovingCp(uint16_t *dst, uint16_t *src) {
  memcpy(dst, src, GetStepsCount() * sizeof(uint16_t));
}

static void ResetMoving() {
  for (uint8_t i = 0; i < MOV_N; ++i) {
    MovingCp(mov.buf[i], rssiHistory);
  }
}

static void MoveHistory() {
  const uint8_t XN = GetStepsCount();

  uint32_t midSum = 0;

  mov.min = RSSI_MAX_VALUE;
  mov.max = 0;
  
  if (PageScan == true) {
  //RssiScan = settings.rssiTriggerLevel;
  PageTick++;
  }

  if (lastStepsCount != XN) {
    ResetMoving();
    lastStepsCount = XN;
  }
  for (uint8_t i = MOV_N - 1; i > 0; --i) {
    MovingCp(mov.buf[i], mov.buf[i - 1]);
  }
  MovingCp(mov.buf[0], rssiHistory);

  uint8_t skipped = 0;

  for (uint8_t x = 0; x < XN; ++x) {
    if (blacklist[x]) {
      skipped++;
      continue;
    }
    uint32_t sum = 0;
    for (uint8_t i = 0; i < MOV_N; ++i) {
      sum += mov.buf[i][x];
    }

    uint16_t pointV = mov.mean[x] = sum / MOV_N;

    midSum += pointV;

    if (pointV > mov.max) {
      mov.max = pointV;
    }

    if (pointV < mov.min) {
      mov.min = pointV;
    }
  }
  if (skipped == XN) {
    return;
  }

  mov.mid = midSum / (XN - skipped);
}

static void TuneToPeak() {
  scanInfo.f = peak.f;
  scanInfo.rssi = peak.rssi;
  scanInfo.i = peak.i;
  SetF(scanInfo.f, true);
}

uint16_t GetBWRegValueForScan() { return 0b0000000110111100; }

uint16_t GetBWRegValueForListen() {
  return listenBWRegValues[settings.listenBw];
}

// Needed to cleanup RSSI if we're hurry (< 10ms)
static void ResetRSSI() {
  uint32_t Reg = BK4819_ReadRegister(BK4819_REG_30);
  Reg &= ~1;
  BK4819_WriteRegister(BK4819_REG_30, Reg);
  Reg |= 1;
  BK4819_WriteRegister(BK4819_REG_30, Reg);
}

uint16_t GetRssi() {
  if (currentState == SPECTRUM) {
uint16_t adds;
if ((currentFreq > 24299990 && currentFreq < 27000000 && AutoPreset == true) || currentFreq > 83999990 || currentFreq < 3000000 || settings.listenBw != BK4819_FILTER_BW_WIDE) {
adds = 1000;
} else {
adds = 0;
}
	settings.delayUS = ((currentFreq / 100000) + delu + adds);
//	redrawStatus = true;
    ResetRSSI();
    SYSTICK_DelayUs(settings.delayUS);
  }
  return BK4819_GetRSSI();
}

static void AddLoot() {
ltck = false;
  for (uint8_t i=0; i<=99; i++) {
  if (loot[i] == memf) {
	ltck = true;
  }
  }
  	if (ltck == false && loi < 99) {
	loi++;
	loot[loi] = memf;
/*	loi = 0;
	memset(loot, 0, 400);*/
  }
}

void print_code(){
  uint32_t Result;
    BK4819_CssScanResult_t ScanResult;
    uint16_t CtcssFreq;
  
  ScanResult = BK4819_GetCxCSSScanResult(&Result, &CtcssFreq);

  uint8_t Code;
  if (ScanResult == BK4819_CSS_RESULT_CDCSS) {       
    Code = DCS_GetCdcssCode(Result);  
  }
  else if (ScanResult == BK4819_CSS_RESULT_CTCSS) {
    Code = DCS_GetCtcssCode(CtcssFreq);
  }
  else {  return;  }


  if (Code != 0xFF) {
    char String[5];
    if (ScanResult == BK4819_CSS_RESULT_CDCSS) {        
      sprintf(String, "D%03o", DCS_Options[Code]);      
    } 
    else if(ScanResult == BK4819_CSS_RESULT_CTCSS) {
      sprintf(String, "%d.%d", CTCSS_Options[Code] / 10, CTCSS_Options[Code] % 10);  
    }
    UI_PrintStringSmallest(String, 30, 0, true, true);
    ST7565_BlitStatusLine();
  }
}

static void ToggleAudio(bool on) {
  if (on) {
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
  } else {
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
  }
}

static void ToggleTX(bool);
static void ToggleRX(bool);

static void ToggleRX(bool on) {
  if (isListening == on) {
    return;
  }
  redrawScreen = true; // HACK: to show when we listening actually or not
  isListening = on;
  if (on) {
    ToggleTX(false);
  }
#ifdef ENABLE_PARTISAN
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_GREEN, on);
  if (settings.backlightState == false && gEeprom.BACKLIGHT > 0) {
  BACKLIGHT_set_brightness(5);
  }
#endif
  BK4819_RX_TurnOn();

  ToggleAudio(on);
  BK4819_ToggleAFDAC(on);
  BK4819_ToggleAFBit(on);
  if (on) {
    listenT = 1000;
    // BK4819_WriteRegister(0x43, GetBWRegValueForListen());
  } else {
  if(gSetting_ScrambleEnable == true && settings.modulationType == MOD_AM){
	AM_fix_reset(0);
	BK4819_SetAGC(0);
  }
  if (settings.backlightState == false) {
  BACKLIGHT_TurnOff();
}
    // BK4819_WriteRegister(0x43, GetBWRegValueForScan());
  }
}

uint16_t registersVault[128] = {0};

static void RegBackupSet(uint8_t num, uint16_t value) {
  registersVault[num] = BK4819_ReadRegister(num);
  BK4819_WriteRegister(num, value);
}

static void RegRestore(uint8_t num) {
  BK4819_WriteRegister(num, registersVault[num]);
}

static void ToggleTX(bool on) {
  if (isTransmitting == on) {
    return;
  }
  isTransmitting = on;
  if (on) {
    ToggleRX(false);
  }
  
#ifdef ENABLE_PARTISAN
  BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, on);
#endif

  if (on) {
    ToggleAudio(false);

    SetTxF(GetOffsetedF(gCurrentVfo, fMeasure));

    RegBackupSet(BK4819_REG_47, 0x6040);
    RegBackupSet(BK4819_REG_7E, 0x302E);
    RegBackupSet(BK4819_REG_50, 0x3B20);
    RegBackupSet(BK4819_REG_37, 0x1D0F);
    RegBackupSet(BK4819_REG_52, 0x028F);
    RegBackupSet(BK4819_REG_30, 0x0000);
    BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
    RegBackupSet(BK4819_REG_51, 0x0000);

    BK4819_SetupPowerAmplifier(gCurrentVfo->TXP_CalculatedSetting,
                               gCurrentVfo->pTX->Frequency);
  } else {
    RADIO_SendEndOfTransmission();
    RADIO_EnableCxCSS();

    BK4819_SetupPowerAmplifier(0, 0);

    RegRestore(BK4819_REG_51);
    BK4819_WriteRegister(BK4819_REG_30, 0);
    RegRestore(BK4819_REG_30);
    RegRestore(BK4819_REG_52);
    RegRestore(BK4819_REG_37);
    RegRestore(BK4819_REG_50);
    RegRestore(BK4819_REG_7E);
    RegRestore(BK4819_REG_47);

    SetF(fMeasure, true);
	}
#ifdef ENABLE_PARTISAN
  BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, !on);
  BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, on);
#endif
}

// Scan info

static void ResetScanStats() {
  scanInfo.rssi = 0;
  scanInfo.rssiMax = 0;
  scanInfo.iPeak = 0;
  scanInfo.fPeak = 0;
}

static void InitScan() {
  ResetScanStats();
  scanInfo.i = 0;
  scanInfo.f = GetFStart();

  scanInfo.scanStep = GetScanStep();
  scanInfo.measurementsCount = GetStepsCount();
}

static void ResetBlacklist() { memset(blacklist, false, 128); }

static void RelaunchScan() {
  InitScan();
  ResetPeak();
  lastStepsCount = 0;
  ToggleRX(false);
#ifdef SPECTRUM_AUTOMATIC_SQUELCH
  if (PageScan == false) {
  settings.rssiTriggerLevel = RSSI_MAX_VALUE;
  }
#endif
  scanInfo.rssiMin = RSSI_MAX_VALUE;
  redrawStatus = true;
}

static void UpdateScanInfo() {
  if (scanInfo.rssi > scanInfo.rssiMax) {
    scanInfo.rssiMax = scanInfo.rssi;
    scanInfo.fPeak = scanInfo.f;
    scanInfo.iPeak = scanInfo.i;
  }

  if (scanInfo.rssi < scanInfo.rssiMin) {
    scanInfo.rssiMin = scanInfo.rssi;
  }
}

static void AutoTriggerLevel() {
  if (currentState == SPECTRUM) {
  TempRssi = ((AverRssi  / (1 << settings.stepsCount)) / 100);
  settings.rssiTriggerLevel = Clamp(TempRssi + ManRssi, 0, RSSI_MAX_VALUE);
  redrawScreen = true;
  AverRssi = 0;
  }
  }

static void UpdatePeakInfoForce() {
  peak.t = 0;
  peak.rssi = scanInfo.rssiMax;
  peak.f = scanInfo.fPeak;
  peak.i = scanInfo.iPeak;
}

static void UpdatePeakInfo() {
  if (peak.f == 0 || peak.t >= 1024 || peak.rssi < scanInfo.rssiMax)
    UpdatePeakInfoForce();
}

static void Measure() {
  for (uint8_t j=0; j<=99; j++) {
  if (scanInfo.f == fbl[j]) {
  blacklist[scanInfo.i] = true; 
  rssiHistory[scanInfo.i] = scanInfo.rssi = scanInfo.rssiMin;
  return;
  } else if (gEeprom.VOICE_PROMPT > 0 && settings.scanStepIndex < 11 && (((scanInfo.f + GetScanStep()) == fbl[j]) || ((scanInfo.f - GetScanStep()) == fbl[j]))) {
  blacklist[scanInfo.i] = true; 
  rssiHistory[scanInfo.i] = scanInfo.rssi = scanInfo.rssiMin;
  return;
  } else if (gEeprom.VOICE_PROMPT > 1 && settings.scanStepIndex < 10 && (((scanInfo.f + (GetScanStep() * 2)) == fbl[j]) || ((scanInfo.f - (GetScanStep() * 2)) == fbl[j]))) {
  blacklist[scanInfo.i] = true; 
  rssiHistory[scanInfo.i] = scanInfo.rssi = scanInfo.rssiMin;
  return;
  }
  }
  // rm harmonics using blacklist for now
  if (PageScan == true) {
  if (scanInfo.f % 1300000 == 0) {
    blacklist[scanInfo.i] = true;
    return;
  } 
	uint16_t cfdiv = currentFreq / 100000; 
	uint16_t scdiv = scanInfo.f / 100000;
	if ((currentFreq < endFreq && scanInfo.f > endFreq) || (cfdiv < gEeprom.SkipC && scdiv >= gEeprom.SkipC) || (cfdiv < gEeprom.SkipD && scdiv >= gEeprom.SkipD) || (cfdiv < gEeprom.SkipE && scdiv >= gEeprom.SkipE) || (cfdiv < gEeprom.SkipF && scdiv >= gEeprom.SkipF) || (cfdiv < gEeprom.SkipG && scdiv >= gEeprom.SkipG) || (cfdiv < gEeprom.SkipH && scdiv >= gEeprom.SkipH) || (cfdiv < gEeprom.SkipJ && scdiv >= gEeprom.SkipJ) || (cfdiv < gEeprom.SkipK && scdiv >= gEeprom.SkipK) || (cfdiv < (gEeprom.SCANLIST_PRIORITY_CH1[0] * 10) && scdiv >= (gEeprom.SCANLIST_PRIORITY_CH1[0] * 10)) || (cfdiv < (gEeprom.SCANLIST_PRIORITY_CH1[1] * 10) && scdiv >= (gEeprom.SCANLIST_PRIORITY_CH1[1] * 10))) {
  blacklist[scanInfo.i] = true;
  return;
  } 
}
  rssiHistory[scanInfo.i] = scanInfo.rssi = GetRssi();
}

// Update things by keypress

static void UpdateRssiTriggerLevel(bool inc) {
  if (inc)
	  ManRssi +=1;
  else
	  ManRssi -=1;
  settings.rssiTriggerLevel = Clamp(TempRssi + ManRssi, 0, RSSI_MAX_VALUE);
  redrawScreen = true;
  SYSTEM_DelayMs(10);
}

static void UpdateRssiTriggerStill(bool inc) {
  if (inc)
      settings.rssiTriggerLevel += 2;
  else
      settings.rssiTriggerLevel -= 2;
  redrawScreen = true;
  SYSTEM_DelayMs(10);
}

static void UpdateDelu(bool inc) {
  if (inc)
    delu += 200;
  else
	delu -= 200;
  if (delu < 600 || delu > 10000) {
	delu = 600;	  
	}
    SYSTEM_DelayMs(100);
	RelaunchScan();
    redrawStatus = true;
}

static void ApplyPreset(FreqPreset p) {
//  currentFreq = p.fStart;
  settings.scanStepIndex = p.stepSizeIndex;
  settings.listenBw = p.listenBW;
  BK4819_SetFilterBandwidth(settings.listenBw);
  settings.modulationType = p.modulationType;
//  settings.stepsCount = p.stepsCountIndex;
  BK4819_SetModulation(settings.modulationType);
//  RelaunchScan();
//  ResetBlacklist();
//  redrawScreen = true;
  settings.frequencyChangeStep = GetBW();
}

/*static void SelectNearestPreset(bool inc) {
  FreqPreset p;
  const uint8_t SZ = ARRAY_SIZE(freqPresets);
  if (inc) {
    for (uint8_t i = 0; i < SZ; ++i) {
      p = freqPresets[i];
      if (currentFreq < p.fStart) {
        ApplyPreset(p);
        return;
      }
    }
  } else {
    for (int i = SZ - 1; i >= 0; --i) {
      p = freqPresets[i];
      if (currentFreq > p.fEnd) {
        ApplyPreset(p);
        return;
      }
    }
  }
  ApplyPreset(p);
}*/

static void AntiPreset() {
	if (settings.scanStepIndex < 9 || settings.modulationType != MOD_FM || settings.listenBw != BK4819_FILTER_BW_WIDE || (currentFreq % 10000) != 0) {
	if (gTxVfo->CHANNEL_BANDWIDTH == BANDWIDTH_WIDE) {
	settings.scanStepIndex = 10;
	} else {
	settings.scanStepIndex = 9;
	}
	uint16_t fcr;
	settings.listenBw = BK4819_FILTER_BW_WIDE;
	BK4819_SetFilterBandwidth(settings.listenBw);
	settings.modulationType = MOD_FM;
	BK4819_SetModulation(MOD_FM);
	settings.frequencyChangeStep = GetBW();
	fcr = currentFreq / 100000;
	currentFreq = fcr * 100000;
	}
}

static void AutomaticPresetChoose(uint32_t f) {
  if (AutoPreset == true) {
  for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
    const FreqPreset *p = &freqPresets[i];
    if (f >= p->fStart && f <= p->fEnd) {
      ApplyPreset(*p);
    }
// Antipreset Enable	
	const FreqPreset *a = NULL;
      for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
        if (currentFreq >= freqPresets[i].fStart &&
            currentFreq < freqPresets[i].fEnd) {
          a = &freqPresets[i];
        }
      }
  if (a == NULL) {
	  AntiPreset();
  }
  }
}
if (currentFreq > 62999990 && currentFreq < 83999990 && gSetting_500TX == true) {
	BK4819_WriteRegister(0x3E, 43515);
} else if (gSetting_500TX == true) {
	BK4819_WriteRegister(0x3E, 36458);
}
}

static void UpdateScanStep(bool inc) {
  if (inc && settings.scanStepIndex < STEP_50_0kHz) {
    ++settings.scanStepIndex;
  } else if (!inc && settings.scanStepIndex > 0) {
    --settings.scanStepIndex;
  } else {
    return;
  }
  settings.frequencyChangeStep = GetBW();
  RelaunchScan();
  ResetBlacklist();
  AutoPreset = false;
  redrawScreen = true;
}

static void UpdateCurrentFreq(bool inc) {
  if (inc && currentFreq < F_MAX) {
    currentFreq += settings.frequencyChangeStep;
  } else if (!inc && currentFreq > F_MIN) {
    currentFreq -= settings.frequencyChangeStep;
  } else {
    return;
  }
  AutomaticPresetChoose(currentFreq);
  RelaunchScan();
  ResetBlacklist();
  redrawScreen = true;
}

static void UpdateLootFreqStill(bool inc) {
	uint32_t f = fMeasure;
  if (inc && iol < 100) {
	iol++;
    f = loot[iol];
  } else if (!inc && iol > 0) {
	iol--;
    f = loot[iol];
  }
  if (f > 0 && iol > 0) {
  SetF(f, false);
  } else {
	iol = loi + 1;
	SetF(memf, false);
  }
  settings.frequencyChangeStep = GetBW();
  SYSTEM_DelayMs(10);
  redrawScreen = true;
}

static void UpdateCurrentFreqStill(bool inc) {
  uint8_t offset = modulationTypeTuneSteps[settings.modulationType];
  uint32_t f = fMeasure;
  if (inc && f < F_MAX) {
    f += offset;
  } else if (!inc && f > F_MIN) {
    f -= offset;
  }
  SetF(f, false);
  settings.frequencyChangeStep = GetBW();
  SYSTEM_DelayMs(10);
  redrawScreen = true;
}

/*static void UpdateFreqChangeStep(bool inc) {
  uint16_t diff = GetScanStep() * 4;
  if (inc && settings.frequencyChangeStep < 1280000) {
    settings.frequencyChangeStep += diff;
  } else if (!inc && settings.frequencyChangeStep > 10000) {
    settings.frequencyChangeStep -= diff;
  }
  SYSTEM_DelayMs(100);
  redrawScreen = true;
}*/

static void ToggleModulation() {
  if (settings.modulationType == MOD_USB) {
    settings.modulationType = MOD_FM;
  } else {
    ++settings.modulationType;
  }
  BK4819_SetModulation(settings.modulationType);
  redrawScreen = true;
}

/*static void ToggleBW() {
if (settings.listenBw = BK4819_FILTER_BW_NARROWER) {
	BK4819_WriteRegister(BK4819_REG_43, 0b0000000000011000);
} else if (settings.listenBw = BK4819_FILTER_BW_NARROW); {
	BK4819_WriteRegister(BK4819_REG_43, 0x4048);
} else {
	BK4819_WriteRegister(BK4819_REG_43, 0x3028);
}*/


static void ToggleListeningBW() {
  if (settings.listenBw == BK4819_FILTER_BW_NARROWER) {
    settings.listenBw = BK4819_FILTER_BW_WIDE;
  } else {
    ++settings.listenBw;
  }
  BK4819_SetFilterBandwidth(settings.listenBw);
  if (currentState == SPECTRUM) {
    RelaunchScan();
  }
  AutoPreset = false;
  redrawScreen = true;
}

static void ToggleBacklight() {
  settings.backlightState = !settings.backlightState;
  if (settings.backlightState) {
	if (gEeprom.BACKLIGHT > 0) {
	BACKLIGHT_TurnOn();
	} else {
	BACKLIGHT_set_brightness(5);
	}    
  } else {
    BACKLIGHT_TurnOff();
  }
}

/*static void ToggleStepsCount() {
  if (settings.stepsCount == STEPS_128) {
    settings.stepsCount = STEPS_16;
	//ManRssi = 72;
  } else {
    --settings.stepsCount;
	//ManRssi -= 18;
  }
  settings.frequencyChangeStep = GetBW();
  RelaunchScan();
  ResetBlacklist();
  redrawScreen = true;
}*/

static void Blacklist() {
//  blacklist[peak.i] = true;
blck = false;
  for (uint8_t k=0; k<=99; k++) {
  if (fbl[k] == memf) {
	blck = true;
  }
  }
  	if (blck == false) {
  fbi++;
  if (fbi > 99){
  fbi = 0;
  memset(fbl, 0, 400);
  }
  fbl[fbi] = memf;
  if (loot[loi] == fbl[fbi]) {
	loot[loi] = 0;
	loi--;
  }
//  fbl[fbi] = peak.f;
  ResetPeak();
  ToggleRX(false);
  PageTick = (gEeprom.DTMF_AUTO_RESET_TIME * 2) - 4;
  RelaunchScan();
//  newScanStart = true;
  redrawScreen = true;
}
AUDIO_PlayBeep(BEEP_1KHZ_60MS_OPTIONAL);
}

// Draw things

static uint8_t Rssi2Y(uint16_t rssi) {
  return DrawingEndY - ConvertDomain(rssi, mov.min - 2,
                                     mov.max + 20 + (mov.max - mov.min) / 2, 0,
                                     DrawingEndY);
}

static void DrawSpectrum() {
  for (uint8_t x = 0; x < LCD_WIDTH; ++x) {
    uint8_t i = x >> settings.stepsCount;
    if (blacklist[i]) {
      continue;
    }
    uint16_t rssi = rssiHistory[i];
    DrawHLine(Rssi2Y(rssi), DrawingEndY, x, true);
  }
}

static void UpdateBatteryInfo() {
  for (uint8_t i = 0; i < 4; i++) {
    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[i], &gBatteryCurrent);
  }

  uint16_t voltage = Mid(gBatteryVoltages, ARRAY_SIZE(gBatteryVoltages));
  gBatteryDisplayLevel = 0;

  for (int i = ARRAY_SIZE(gBatteryCalibration) - 1; i >= 0; --i) {
    if (gBatteryCalibration[i] < voltage) {
      gBatteryDisplayLevel = i + 1;
      break;
    }
  }
}

static void DrawStatus() {

  if (currentState == SPECTRUM) {

#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      RegisterSpec s = hiddenRegisterSpecs[hiddenMenuState];
      sprintf(String, "%x %s: %u", s.num, s.name, BK4819_GetRegValue(s));
      UI_PrintStringSmallest(String, 0, 0, true, true);
    } else {
#endif
/*      const FreqPreset *p = NULL;
      for (uint8_t i = 0; i < ARRAY_SIZE(freqPresets); ++i) {
        if (currentFreq >= freqPresets[i].fStart &&
            currentFreq < freqPresets[i].fEnd) {
          p = &freqPresets[i];
        }
      }
      if (p != NULL) {
        UI_PrintStringSmallest(p->name, 0, 0, true, true);
      } else if (currentFreq < 30000000) {
		UI_PrintStringSmallest("vhf", 0, 0, true, true);
	  } else {
		UI_PrintStringSmallest("uhf", 0, 0, true, true);
	  }*/
	if (currentFreq < 10000000) {
	sprintf(String, "%uM", 29980000 / currentFreq);
	UI_PrintStringSmallest(String, 0, 0, true, true);
	} else {
	sprintf(String, "%ucm", 29980000 / (currentFreq / 100));
	UI_PrintStringSmallest(String, 0, 0, true, true);
	}
	
      sprintf(String, "D: %u us", settings.delayUS);
      UI_PrintStringSmallest(String, 64, 0, true, true);
    }
	if (AutoPreset == true) {
	sprintf(String, "A", 0);
	} else {
	sprintf(String, "M", 0);
	}
    UI_PrintStringSmallest(String, 58, 0, true, true);
	
#ifdef ENABLE_ALL_REGISTERS
  }
#endif
	if (currentState == STILL) {
	sprintf(String, "%ucm", (29980000 / (fMeasure / 100)) / 4);
	UI_PrintStringSmallest(String, 0, 0, true, true);
	}
  UI_DisplayBattery(gBatteryDisplayLevel);
}

static void DrawF(uint32_t f) {
  UI_PrintStringSmallest(modulationTypeOptions[settings.modulationType], 116, 2,
                         false, true);
  UI_PrintStringSmallest(bwOptions[settings.listenBw], 108, 8, false, true);

  if (currentState == SPECTRUM && !f) {
    return;
  }

#ifdef ENABLE_ALL_REGISTERS
  if (currentState == SPECTRUM) {
    sprintf(String, "R%03u S%03u A%03u", scanInfo.rssi,
            BK4819_GetRegValue((RegisterSpec){"snr_out", 0x61, 8, 0xFF, 1}),
            BK4819_GetRegValue((RegisterSpec){"agc_rssi", 0x62, 8, 0xFF, 1}));
    UI_PrintStringSmallest(String, 36, 8, false, true);
  }
#endif

  sprintf(String, "%u.%05u", f / 100000, f % 100000);
//  sprintf(String, "%u.%03u", f / 100000, (f % 100000) / 100);

  if (currentState == STILL && kbd.current == KEY_PTT) {
    if (txAllowState) {
      sprintf(String, vfoStateNames[txAllowState]);
    }/* else if (isTransmitting) {
      f = GetOffsetedF(gCurrentVfo, f);
      sprintf(String, "TX %u.%05u", f / 100000, f % 100000);
//	  sprintf(String, "TX %u.%03u", f / 100000, (f % 100000) / 100);
    }*/
  }
//  UI_PrintStringSmall(String, 8, 127, 0);
	UI_PrintString(String, 8, 127, 0 * 4, 8, true);
}

static void DrawNums() {
  if (currentState == SPECTRUM) {
/*    sprintf(String, "%ux", GetStepsCount());
    UI_PrintStringSmallest(String, 0, 8, false, true);*/
    sprintf(String, "%u.%02uk", GetScanStep() / 100, GetScanStep() % 100);
    UI_PrintStringSmallest(String, 0, 2, false, true);
	sprintf(String, "%u", loi);
    UI_PrintStringSmallest(String, 0, 8, false, true);
	sprintf(String, "/%u", fbi);
    UI_PrintStringSmallest(String, 8, 8, false, true);
  }

  if (IsCenterMode()) {
    sprintf(String, "%u.%05u \xB1%u.%02uk", currentFreq / 100000,
            currentFreq % 100000, settings.frequencyChangeStep / 100,
            settings.frequencyChangeStep % 100);
    UI_PrintStringSmallest(String, 36, 49, false, true);
  } else {
    sprintf(String, "%u.%05u", GetFStart() / 100000, GetFStart() % 100000);
    UI_PrintStringSmallest(String, 0, 49, false, true);

    sprintf(String, "\xB1%uk", settings.frequencyChangeStep / 100);
    UI_PrintStringSmallest(String, 52, 49, false, true);
	
	if (PageScan == false) {
    sprintf(String, "%u.%05u", GetFEnd() / 100000, GetFEnd() % 100000);
    UI_PrintStringSmallest(String, 93, 49, false, true);
	} else {
	sprintf(String, ">%u.%05u", endFreq / 100000, endFreq % 100000);
    UI_PrintStringSmallest(String, 89, 49, false, true);
	}
	if (slowsc == true) {
    UI_PrintStringSmallest("S", 85, 49, false, true);
	}
  }
}

static void DrawRssiTriggerLevel() {
  if (settings.rssiTriggerLevel == RSSI_MAX_VALUE || monitorMode)
    return;
  uint8_t y = Rssi2Y(settings.rssiTriggerLevel);
  for (uint8_t x = 0; x < LCD_WIDTH; ++x) {
    PutPixel(x, y, 2);
  }
}

static void DrawTicks() {
  uint32_t f = GetFStart() % 100000;
  uint32_t step = GetScanStep();
  for (uint8_t x = 0; x < LCD_WIDTH;
       x += (1 << settings.stepsCount), f += step) {
    uint8_t barValue = 0b00000001;
    (f % 10000) < step && (barValue |= 0b00000010);
    (f % 50000) < step && (barValue |= 0b00000100);
    (f % 100000) < step && (barValue |= 0b00011000);

    gFrameBuffer[5][x] |= barValue;
  }

  // center
  if (IsCenterMode()) {
    gFrameBuffer[5][62] = 0x80;
    gFrameBuffer[5][63] = 0x80;
    gFrameBuffer[5][64] = 0xff;
    gFrameBuffer[5][65] = 0x80;
    gFrameBuffer[5][66] = 0x80;
  } else {
    gFrameBuffer[5][0] = 0xff;
    gFrameBuffer[5][1] = 0x80;
    gFrameBuffer[5][2] = 0x80;
    gFrameBuffer[5][3] = 0x80;
    gFrameBuffer[5][124] = 0x80;
    gFrameBuffer[5][125] = 0x80;
    gFrameBuffer[5][126] = 0x80;
    gFrameBuffer[5][127] = 0xff;
  }
}

static void DrawArrow(uint8_t x) {
  for (signed i = -2; i <= 2; ++i) {
    signed v = x + i;
    uint8_t a = i > 0 ? i : -i;
    if (!(v & LCD_WIDTH)) {
      gFrameBuffer[5][v] |= (0b01111000 << a) & 0b01111000;
    }
  }
}

static void DeInitSpectrum() {
  SetF(initialFreq, true);
  ToggleRX(false);
  RestoreRegisters();
  isInitialized = false;
  SYSTEM_DelayMs(500);
//  gBatterySaveCountdownExpired = true;
}

static void BandChange() {	
	AutomaticPresetChoose(currentFreq);
	RelaunchScan();
	ResetBlacklist();
//	redrawScreen = true;
}

static void OnKeyDown(uint8_t key) {
  switch (key) {
  case KEY_3:
    UpdateRssiTriggerLevel(true);
    break;
  case KEY_9:
    UpdateRssiTriggerLevel(false);
    break;
  case KEY_1:
    UpdateScanStep(true);
    break;
  case KEY_7:
    UpdateScanStep(false);
    break;
  case KEY_2:
/*#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      if (hiddenMenuState <= 1) {
        hiddenMenuState = ARRAY_SIZE(hiddenRegisterSpecs) - 1;
      } else {
        hiddenMenuState--;
      }
      redrawStatus = true;
      break;
    }
#endif
    if (0)
      SelectNearestPreset(false);*/
	UpdateDelu(true);
    break;
  case KEY_8:
/*#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      if (hiddenMenuState == ARRAY_SIZE(hiddenRegisterSpecs) - 1) {
        hiddenMenuState = 1;
      } else {
        hiddenMenuState++;
      }
      redrawStatus = true;
      break;
    }
#endif
    if (0)
      SelectNearestPreset(false);*/
    UpdateDelu(false);
    break;
  case KEY_UP:
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      UpdateRegMenuValue(hiddenRegisterSpecs[hiddenMenuState], true);
      redrawStatus = true;
      break;
    }
#endif
    UpdateCurrentFreq(true);
	PageTick = 0;
    ScanPaus = false;
    break;
  case KEY_DOWN:
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      UpdateRegMenuValue(hiddenRegisterSpecs[hiddenMenuState], false);
      redrawStatus = true;
      break;
    }
#endif
	PageScan = false;
    UpdateCurrentFreq(false);
    break;
  case KEY_SIDE1:
	currentFreq = memf - (GetBW() >> 1);
	BandChange();
	ScanPaus = true;
/*    PageScan = false;
    SetState(STILL);
//    TuneToPeak();
	SetF(memf, true);
    settings.rssiTriggerLevel = 120;*/
    break;
  case KEY_STAR:
    if (PageScan == false) {
	  bgnFreq = currentFreq;
	  ScanPaus = false;
	  SYSTEM_DelayMs(10);
	  FreqInput();
	  SetState(FREQ_INPUT);
	  PageScan = true;
    } else {
      PageScan = false;
    }
    break;
  case KEY_F:
   if (AutoPreset == false) {
	  AutoPreset = true;
    } else {
      AutoPreset = false;
    }
	redrawStatus = true;
    break;
  case KEY_5:
    FreqInput();
    SetState(FREQ_INPUT);
	PageScan = false;
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_4:
    if (fbi > 0 ) {
	fbl[fbi] = 0;
	fbi--;
	}
	RelaunchScan();
//  	memset(fbl, 0, 400);
//	fbi = 0;
//    ToggleStepsCount();
    break;
  case KEY_SIDE2:
    ToggleBacklight();
    break;
  case KEY_PTT:
#ifdef ENABLE_ALL_REGISTERS
    if (settings.rssiTriggerLevel != RSSI_MAX_VALUE - 1) {
      settings.rssiTriggerLevel = RSSI_MAX_VALUE - 1;
    } else {
      settings.rssiTriggerLevel = RSSI_MAX_VALUE;
    }
    redrawScreen = true;
#else
    Blacklist();
#endif
    break;
  case KEY_MENU:
	if (PageScan == true) {		
    PageScan = false;
	RScan = true;
	}
    SetState(STILL);
//    TuneToPeak();
	iol = loi + 1;
	SetF(memf, true);
    settings.rssiTriggerLevel = 130;
	monitorMode = true;
#ifdef ENABLE_ALL_REGISTERS
    hiddenMenuState = 1;
    redrawStatus = true;
#endif
    break;
  case KEY_EXIT:
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      hiddenMenuState = 0;
      redrawStatus = true;
      break;
    }
#endif
    if (menuState) {
      menuState = 0;
      redrawScreen = true;
      break;
    }
	PageScan = false;
    DeInitSpectrum();
    break;
  default:
    break;
  }
}

static void OnKeyDownFreqInput(uint8_t key) {
  switch (key) {
  case KEY_0:
  case KEY_1:
  case KEY_2:
  case KEY_3:
  case KEY_4:
  case KEY_5:
  case KEY_6:
  case KEY_7:
  case KEY_8:
  case KEY_9:
  case KEY_STAR:
    UpdateFreqInput(key);
    redrawScreen = true;
    break;
  case KEY_EXIT:
    if (freqInputIndex == 0) {
	  PageScan = false;
      SetState(previousState);
      break;
    }
    UpdateFreqInput(key);
    redrawScreen = true;
    break;
  case KEY_MENU:
    if (tempFreq < F_MIN || tempFreq > F_MAX) {
      break;
    }
    SetState(previousState);
	  if (PageScan == true) {
	endFreq = tempFreq;
	if (PageScan == true && (bgnFreq + (GetBW() / 4)) > endFreq) {
	PageScan = false;
	}
	} else {
    currentFreq = tempFreq;
	}
    FreqInput();
    if (currentState == SPECTRUM) {
      BandChange();
    } else {
      SetF(currentFreq, true);
    }
    redrawScreen = true;
    break;
  default:
    break;
  }
  SYSTEM_DelayMs(90);
}

void OnKeyDownStill(KEY_Code_t key) {
  switch (key) {
#ifdef ENABLE_ALL_REGISTERS
  case KEY_2:
    menuState = 0;
    if (hiddenMenuState <= 1) {
      hiddenMenuState = ARRAY_SIZE(hiddenRegisterSpecs) - 1;
    } else {
      hiddenMenuState--;
    }
    redrawScreen = true;
    SYSTEM_DelayMs(150);
    break;
  case KEY_8:
    menuState = 0;
    if (hiddenMenuState == ARRAY_SIZE(hiddenRegisterSpecs) - 1) {
      hiddenMenuState = 1;
    } else {
      hiddenMenuState++;
    }
    redrawScreen = true;
    SYSTEM_DelayMs(150);
    break;
#endif
  case KEY_UP:
    if (menuState) {
      UpdateRegMenuValue(registerSpecs[menuState], true);
      break;
    }
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      UpdateRegMenuValue(hiddenRegisterSpecs[hiddenMenuState], true);
      break;
    }
#endif
    UpdateLootFreqStill(true);
    break;
  case KEY_DOWN:
    if (menuState) {
      UpdateRegMenuValue(registerSpecs[menuState], false);
      break;
    }
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      UpdateRegMenuValue(hiddenRegisterSpecs[hiddenMenuState], false);
      break;
    }
#endif
    UpdateLootFreqStill(false);
    break;
  case KEY_1:
    UpdateCurrentFreqStill(true);
    break;
  case KEY_7:
    UpdateCurrentFreqStill(false);
    break;
  case KEY_3:
    UpdateRssiTriggerStill(true);
    break;
  case KEY_9:
    UpdateRssiTriggerStill(false);
    break;
  case KEY_F:
    if (menuState == ARRAY_SIZE(registerSpecs) - 1) {
      menuState = 1;
    } else {
      menuState++;
    }
    SYSTEM_DelayMs(100);
    redrawScreen = true;
    break;
  case KEY_5:
    PageScan = false;
    FreqInput();
    SetState(FREQ_INPUT);
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_SIDE1:
    monitorMode = !monitorMode;
    redrawScreen = true;
    break;
  case KEY_SIDE2:
    ToggleBacklight();
    break;
  case KEY_PTT:
    // start transmit
    UpdateBatteryInfo();
    if (gBatteryDisplayLevel == 6) {
      txAllowState = VFO_STATE_VOL_HIGH;
    } else if (IsTXAllowed(GetOffsetedF(gCurrentVfo, fMeasure))) {
      txAllowState = VFO_STATE_NORMAL;
      ToggleTX(true);
    } else {
      txAllowState = VFO_STATE_TX_DISABLE;
    }
    redrawScreen = true;
    break;
  case KEY_MENU:
	gTxVfo->ConfigRX.Frequency = fMeasure;
    gTxVfo->ConfigTX.Frequency = fMeasure;
	gTxVfo->Band = FREQUENCY_GetBand(fMeasure);
	gTxVfo->CHANNEL_SAVE = gTxVfo->Band + FREQ_CHANNEL_FIRST;
	gTxVfo->ModulationType = MOD_FM;
	if(gTxVfo->Band == 1) gTxVfo->ModulationType = MOD_AM;
	gEeprom.ScreenChannel[gEeprom.TX_VFO] = gEeprom.FreqChannel[gEeprom.TX_VFO] = gTxVfo->CHANNEL_SAVE;
	SetState(SPECTRUM);
    ToggleRX(false);
	monitorMode = false;
	RScan = false;
	RestoreRegisters();
	RADIO_SetupRegisters(true);
    isInitialized = false;
	SYSTEM_DelayMs(500);
//	gBatterySaveCountdownExpired = true;
    break;
  case KEY_EXIT:
    if (menuState) {
      menuState = 0;
      redrawScreen = true;
      break;
    }
#ifdef ENABLE_ALL_REGISTERS
    if (hiddenMenuState) {
      hiddenMenuState = 0;
      redrawScreen = true;
      break;
    }
#endif
    SetState(SPECTRUM);
    monitorMode = false;
    currentFreq = fMeasure - (GetBW() >> 1);
	BandChange();
	if (RScan == true) {		
    PageScan = true;
	ScanPaus = true;
	RScan = false;
	}
    break;
  default:
    break;
  }
  redrawStatus = true;
}

static void OnKeysReleased() {
  if (isTransmitting) {
    ToggleTX(false);
  }
}

static void RenderFreqInput() {
  UI_PrintString(freqInputString, 2, 127, 0, 8, true);
}

static void RenderStatus() {
  memset(gStatusLine, 0, sizeof(gStatusLine));
  DrawStatus();
  ST7565_BlitStatusLine();
}

static void RenderSpectrum() {
  DrawTicks();
  DrawArrow(peak.i << settings.stepsCount);
  DrawSpectrum();
  DrawRssiTriggerLevel();
//  DrawF(peak.f);
  DrawF(memf);
  DrawNums();
}

static void RenderStill() {
  DrawF(fMeasure);

  const uint8_t METER_PAD_LEFT = 3;
  uint8_t *ln = gFrameBuffer[2];

  for (uint8_t i = 0; i < 121; i++) {
    ln[i + METER_PAD_LEFT] = i % 10 ? 0b01000000 : 0b11000000;
  }

  uint8_t rssiX = Rssi2PX(scanInfo.rssi, 0, 121);
  for (uint8_t i = 0; i < rssiX; ++i) {
    if (i % 5 && i / 5 < rssiX / 5) {
      ln[i + METER_PAD_LEFT] |= 0b00011100;
    }
  }

  int dbm = Rssi2DBm(scanInfo.rssi);
  uint8_t s = DBm2S(dbm);
  if (s < 10) {
    sprintf(String, "S%u", s);
  } else {
    sprintf(String, "S9+%u0", s - 9);
  }
  UI_PrintStringSmallest(String, 4, 10, false, true);
//  sprintf(String, "%d dBm", dbm);
  sprintf(String, "%d", dbm);
  UI_PrintStringSmallest(String, 4, 4, false, true);
  sprintf(String, "%u", iol);
  UI_PrintStringSmallest(String, 105, 2, false, true);

  if (isTransmitting) {
    uint8_t afDB = BK4819_ReadRegister(0x6F) & 0b1111111;
    uint8_t afPX = ConvertDomain(afDB, 26, 194, 0, 121);
    for (uint8_t i = 0; i < afPX; ++i) {
      gFrameBuffer[3][i + METER_PAD_LEFT] |= 0b00000011;
    }
  }

  if (!monitorMode) {
    uint8_t rssiTriggerX = Rssi2PX(settings.rssiTriggerLevel, METER_PAD_LEFT,
                                   121 + METER_PAD_LEFT);
    ln[rssiTriggerX - 1] |= 0b01000001;
    ln[rssiTriggerX] = 0b01111111;
    ln[rssiTriggerX + 1] |= 0b01000001;
  }

#ifdef ENABLE_ALL_REGISTERS
  if (hiddenMenuState) {
    uint8_t hiddenMenuLen = ARRAY_SIZE(hiddenRegisterSpecs);
    uint8_t offset = Clamp(hiddenMenuState - 2, 1, hiddenMenuLen - 5);
    for (int i = 0; i < 5; ++i) {
      RegisterSpec rs = hiddenRegisterSpecs[i + offset];
      bool isCurrent = hiddenMenuState == i + offset;
      sprintf(String, "%s%x %s: %u", isCurrent ? ">" : " ", rs.num, rs.name,
              BK4819_GetRegValue(rs));
      UI_PrintStringSmallest(String, 0, i * 6 + 26, false, true);
    }
  } else {
#endif
    const uint8_t PAD_LEFT = 4;
    const uint8_t CELL_WIDTH = 30;
    uint8_t row = 3;

    for (int i = 0, idx = 1; idx < ARRAY_SIZE(registerSpecs); ++i, ++idx) {
      if (idx == 5) {
        row += 2;
        i = 0;
      }
const uint8_t offset = PAD_LEFT + i * CELL_WIDTH;
      if (menuState == idx) {
        for (int j = 0; j < CELL_WIDTH; ++j) {
          gFrameBuffer[row][j + offset] = 0xFF;
          gFrameBuffer[row + 1][j + offset] = 0xFF;
        }
      }
      RegisterSpec rs = registerSpecs[idx];
      sprintf(String, "%s", rs.name);
      UI_PrintStringSmallest(String, offset + 2, row * 8 + 2, false,
                             menuState != idx);
      sprintf(String, "%u", BK4819_GetRegValue(rs));
      UI_PrintStringSmallest(String, offset + 2, (row + 1) * 8 + 1, false,
                             menuState != idx);
    }
#ifdef ENABLE_ALL_REGISTERS
  }
#endif
}

static void Render() {
  memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

  switch (currentState) {
  case SPECTRUM:
    RenderSpectrum();
    break;
  case FREQ_INPUT:
    RenderFreqInput();
    break;
  case STILL:
    RenderStill();
    break;
  }

  ST7565_BlitFullScreen();
}

bool HandleUserInput() {
  kbd.prev = kbd.current;
  kbd.current = GetKey();

  if (kbd.current == KEY_INVALID) {
    kbd.counter = 0;
    OnKeysReleased();
    return true;
  }

  if (kbd.current == kbd.prev && kbd.counter <= 40) {
    kbd.counter++;
    SYSTEM_DelayMs(10);
  }

  if (kbd.counter == 5 || kbd.counter > 40) {
    switch (currentState) {
    case SPECTRUM:
      OnKeyDown(kbd.current);
      break;
    case FREQ_INPUT:
      OnKeyDownFreqInput(kbd.current);
      break;
    case STILL:
      OnKeyDownStill(kbd.current);
      break;
    }
  }

  return true;
}

static void Scan() {
  if (blacklist[scanInfo.i]) {
    return;
  }
  SetF(scanInfo.f, true);
  Measure();
  UpdateScanInfo();
}

static void NextScanStep() {
  AverRssi += (scanInfo.rssi);
  ++peak.t;
  ++scanInfo.i;
  scanInfo.f += scanInfo.scanStep;
}

static void UpdateScan() {
  Scan();

  if (scanInfo.i < scanInfo.measurementsCount) {
    NextScanStep();
    return;
  }

  MoveHistory();

  redrawScreen = true;

  AutoTriggerLevel();
  UpdatePeakInfo();
  if (IsPeakOverLevel()) {
	ToggleRX(true);
    TuneToPeak();
	memf = scanInfo.f;
	AddLoot();
    return;
  }

  newScanStart = true;
}


uint16_t screenRedrawT = 0;
static void UpdateStill() {
  Measure();

  peak.rssi = scanInfo.rssi;
  AutoTriggerLevel();

  if (++screenRedrawT >= 1000) {
    screenRedrawT = 0;
    redrawScreen = true;
  }

  ToggleRX(IsPeakOverLevel() || monitorMode);
}

void change_DAC(uint8_t vol){
  const uint16_t reg48 = BK4819_ReadRegister(BK4819_REG_48);
  uint8_t current_vol = reg48 & 0x0F;
#ifdef ENABLE_EMB_MENU
  if( d > ( (current_vol > vol) ? current_vol - vol : vol - current_vol ) ){  return;  }
#endif
  
  if(vol>current_vol){      //++gain
    for(uint8_t i=1; i<=(vol-current_vol); i++){
      SYSTEM_DelayMs(1);
      BK4819_WriteRegister(BK4819_REG_48,
                           (reg48 & 0xFFF0u) | (current_vol + i));
    }
  }
  else if(vol<current_vol){    //--gain
    for(uint8_t i=1; i<=(current_vol-vol); i++){
      SYSTEM_DelayMs(2);
      BK4819_WriteRegister(BK4819_REG_48,
                           (reg48 & 0xFFF0u) | (current_vol - i));
    }
  }
  
}

bool isSignalLost() {
  uint16_t tmp = BK4819_GetRSSI();
  if(
        (   ( ((tmp < p_rssi) ? (p_rssi - tmp) : 0)  >  20  && p_rssi < 160  ) 
        ||  ( ((tmp < p_rssi) ? (p_rssi - tmp) : 0)  >  8  && p_rssi < 100  ) 
        ||  ( ((tmp < p_rssi) ? (p_rssi - tmp) : 0)  >  60   )     )      
        )
        {
          p_rssi = 0;
          return true;
        }
  p_rssi = tmp;
  return false;
}

static void UpdateListening() {
  if (!isListening) {
    ToggleRX(true);
  } 
  print_code();
  if (gSetting_ScrambleEnable == true && settings.modulationType == MOD_AM) {
  if (listenT % 10 == 0) {
    AM_fix_10ms(0);
  }
  }
  if (gSetting_ScrambleEnable == true && listenT % 10 == 0 && isSignalLost()) { 
  listenT = 0;
  }
  if (listenT) {
    listenT--;
    SYSTEM_DelayMs(1);
    return;
  }
  redrawScreen = true;

  if (currentState == SPECTRUM) {
    BK4819_WriteRegister(0x43, GetBWRegValueForScan());
	if(isListening && settings.modulationType == MOD_AM && gSetting_ScrambleEnable == true){
	tmp13 = BK4819_ReadRegister(BK4819_REG_13);
	change_DAC(3);
	BK4819_SetAGC(0);
	}
    Measure();
    // BK4819_WriteRegister(0x43, GetBWRegValueForListen());
	if(isListening && settings.modulationType == MOD_AM && gSetting_ScrambleEnable == true){
	// AM_fix_10ms(0);
	BK4819_WriteRegister(BK4819_REG_13, tmp13);
	change_DAC(14);
	}
  } else {
    Measure();
    // BK4819_WriteRegister(0x43, GetBWRegValueForListen());
  }

  peak.rssi = scanInfo.rssi;
  MoveHistory();

  if (IsPeakOverLevel() || monitorMode) {
    listenT = currentState == SPECTRUM ? 1000 : 10;
    return;
  }

  ToggleRX(false);
  newScanStart = true;
}

static void UpdateTransmitting() {}

static void Tick() {
//uint8_t pt;
#if defined(ENABLE_UART)
  if (UART_IsCommandAvailable()) {
    __disable_irq();
    UART_HandleCommand();
    __enable_irq();
  }
#endif
  if (newScanStart) {
    InitScan();
    newScanStart = false;
  }
  if (isTransmitting) {
    UpdateTransmitting();
  } else if (isListening && currentState != FREQ_INPUT) {
    UpdateListening();
  } else {
    if (currentState == SPECTRUM) {
	UpdateScan();
    } else if (currentState == STILL) {
      UpdateStill();
    }
  }
  if (++batteryUpdateTimer > 4096) {
    batteryUpdateTimer = 0;
    UpdateBatteryInfo();
    redrawStatus = true;
  }
  if (redrawStatus) {
    RenderStatus();
    redrawStatus = false;
  }
  if (redrawScreen) {
    Render();
    redrawScreen = false;
  }
	if (!preventKeypress) {
    HandleUserInput();
	}
//	print_code();
    if (PageScan == true && currentState == SPECTRUM) {
/*	if (slowsc == true) {
	pt = 1;
	} else {
	pt = 0;
	}*/
	if (isListening == true && gEeprom.DTMF_AUTO_RESET_TIME > 1) {
	if (scanInfo.f == currentFreq) {
	for (uint8_t l=0; l<=99; l++) {
	if (scanInfo.f == fbl[l]) {
	ScanPaus = false;
	return;
	}
	} 
	}
	PageTick = 0;
	ScanPaus = true;
	} else {
  if (PageTick > slowsc && ScanPaus == false) {
	UpdateCurrentFreq(true);
	PageTick = 0;
	} else {
	if (PageTick > gEeprom.DTMF_AUTO_RESET_TIME * 2) {
	UpdateCurrentFreq(true);
	PageTick = 0;
    ScanPaus = false;
	}
	}
	if (currentFreq == bgnFreq && (endFreq <= currentFreq + GetBW()) && scanInfo.i == scanInfo.measurementsCount) {
	PageScan = false;
	PageTick = 0;
	}
	if (currentFreq > (endFreq - 1)) {
	currentFreq = bgnFreq;
	BandChange();
	}
	if (currentFreq >= (gEeprom.SCANLIST_PRIORITY_CH1[0] *1000000)  && currentFreq < (gEeprom.SCANLIST_PRIORITY_CH2[0] * 1000000) && endFreq > (gEeprom.SCANLIST_PRIORITY_CH2[0] * 1000000)) {
	currentFreq = (gEeprom.SCANLIST_PRIORITY_CH2[0] * 1000000);	
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SCANLIST_PRIORITY_CH1[1] *1000000)  && currentFreq < (gEeprom.SCANLIST_PRIORITY_CH2[1] * 1000000) && endFreq > (gEeprom.SCANLIST_PRIORITY_CH2[1] * 1000000)) {
	currentFreq = (gEeprom.SCANLIST_PRIORITY_CH2[1] * 1000000);
	AutomaticPresetChoose(currentFreq);
	}
#if defined(ENABLE_MEMSKIP)
	if (currentFreq >= (gEeprom.SkipC * 100000)  && currentFreq < (gEeprom.EndC * 100000) && endFreq > (gEeprom.EndC * 100000)) {
	currentFreq = (gEeprom.EndC * 100000);	
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipD * 100000)  && currentFreq < (gEeprom.EndD * 100000) && endFreq > (gEeprom.EndD * 100000)) {
	currentFreq = (gEeprom.EndD * 100000);	
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipE * 100000)  && currentFreq < (gEeprom.EndE * 100000) && endFreq > (gEeprom.EndE * 100000)) {
	currentFreq = (gEeprom.EndE * 100000);	
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipF * 100000)  && currentFreq < (gEeprom.EndF * 100000) && endFreq > (gEeprom.EndF * 100000)) {
	currentFreq = (gEeprom.EndF * 100000);
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipG * 100000)  && currentFreq < (gEeprom.EndG * 100000) && endFreq > (gEeprom.EndG * 100000)) {
	currentFreq = (gEeprom.EndG * 100000);	
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipH * 100000)  && currentFreq < (gEeprom.EndH * 100000) && endFreq > (gEeprom.EndH * 100000)) {
	currentFreq = (gEeprom.EndH * 100000);
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipJ * 100000)  && currentFreq < (gEeprom.EndJ * 100000) && endFreq > (gEeprom.EndJ * 100000)) {
	currentFreq = (gEeprom.EndJ * 100000);
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= (gEeprom.SkipK * 100000)  && currentFreq < (gEeprom.EndK * 100000) && endFreq > (gEeprom.EndK * 100000)) {
	currentFreq = (gEeprom.EndK * 100000);
	AutomaticPresetChoose(currentFreq);
	}
	if (currentFreq >= 8699999 && currentFreq <=10799999 && endFreq > 10799999 && AutoPreset == true)  {
	currentFreq = 11800000;
	AutomaticPresetChoose(currentFreq);
	}
#endif
	}
  }
}

void APP_RunSpectrum() {
  preventKeypress = true;
  BackupRegisters();
  BK4819_WriteRegister(BK4819_REG_43, 0x3028);
  if (gSetting_ScrambleEnable == true) {
  AM_fix_init();
  }

  // TX here coz it always? set to active VFO
  VFO_Info_t vfo = gEeprom.VfoInfo[gEeprom.TX_VFO];
  initialFreq = vfo.pRX->Frequency;
  currentFreq = initialFreq;
  memf = initialFreq;
  
  	if (vfo.CHANNEL_BANDWIDTH == BANDWIDTH_WIDE && AutoPreset == true) {
	settings.scanStepIndex = 10;
	} else if (AutoPreset == true) {
	settings.scanStepIndex = 9;
	} else {
	settings.scanStepIndex = gStepSettingToIndex[vfo.STEP_SETTING];
	}
/*  settings.listenBw = vfo.CHANNEL_BANDWIDTH == BANDWIDTH_WIDE
                          ? BANDWIDTH_WIDE
                          : BANDWIDTH_NARROW;*/
  settings.listenBw = BK4819_FILTER_BW_WIDE;
  settings.modulationType = vfo.ModulationType;
  settings.frequencyChangeStep = GetBW();

//  AutomaticPresetChoose(currentFreq);

  redrawStatus = true;
  redrawScreen = true;
  newScanStart = true;

  ToggleRX(true), ToggleRX(false); // hack to prevent noise when squelch off
  BK4819_SetModulation(settings.modulationType);
  
  BandChange();
//  RelaunchScan();
//  ResetBlacklist();
  
//  AutoPreset = true;
  ManRssi = 0;
  memset(rssiHistory, 0, 128);

  isInitialized = true;
  
  if (gEeprom.BACKLIGHT > 0) {
	settings.backlightState = true;
	BACKLIGHT_TurnOn();
}

  if (gEeprom.POWER_ON_DISPLAY_MODE == POWER_ON_DISPLAY_MODE_VOLTAGE && (gEeprom.VfoInfo[0].pRX->Frequency + (GetBW() / 4)) < gEeprom.VfoInfo[1].pRX->Frequency) {
	bgnFreq = gEeprom.VfoInfo[0].pRX->Frequency;
	endFreq = gEeprom.VfoInfo[1].pRX->Frequency;
	PageScan = true;
	settings.backlightState = false;
	BACKLIGHT_TurnOff();
	}
	SYSTEM_DelayMs(150);
	ScanPaus = false;
	preventKeypress = false;
	while (isInitialized) {
    Tick();
  }
}
