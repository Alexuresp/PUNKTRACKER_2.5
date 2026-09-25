/*
 * Automatic receive frequency correction.
 *
 * The BK4819 has an internal FM AFC, but its correction value is not exposed
 * by the documented register interface.  The frequency scanner is therefore
 * used as a frequency counter.  Three close measurements are required before
 * the RX synthesizer is moved.  Channel memory and TX frequency stay intact.
 */

#include "afc.h"

#include "app/scanner.h"
#include "driver/bk4819.h"
#include "functions.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/ui.h"

#include <stdbool.h>

enum {
  AFC_SCAN_WAIT_TICKS = 22,       /* REG_32 uses the 0.2 second scan time. */
  AFC_RETRY_TICKS = 5,
  AFC_UPDATE_TICKS = 50,
  AFC_STABLE_WINDOW_10HZ = 20,    /* Carrier readings must agree within 200 Hz. */
  AFC_REQUIRED_HITS = 3,
};

static uint32_t sBaseFrequency;
static uint32_t sLastResult;
static int16_t sOffset10Hz;
static uint8_t sCountdown;
static uint8_t sStableHits;
static bool sScanRunning;
static bool sOffsetValid;

static int16_t AFC_GetLimit10Hz(void)
{
  return gEeprom.AFC_RANGE == AFC_RANGE_MAX ? 1000 : 700;
}

static void AFC_ClearState(bool Retune)
{
  bool WasValid = sOffsetValid;

  if (sScanRunning && Retune) {
    BK4819_DisableFrequencyScan();
  }
  sScanRunning = false;
  sCountdown = 0;
  sStableHits = 0;
  sLastResult = 0;
  sOffsetValid = false;

  if (sOffset10Hz != 0) {
    sOffset10Hz = 0;
    if (Retune && sBaseFrequency != 0) {
      BK4819_SetFrequency(sBaseFrequency);
    }
  }
  if (WasValid) {
    gUpdateDisplay = true;
  }
}

static bool AFC_CanRun(void)
{
  if (gRxVfo == 0 || gRxVfo->ModulationType != MOD_FM || gRxIdleMode) {
    return false;
  }

  if (gScanState != SCAN_OFF || gCssScanMode != CSS_SCAN_MODE_OFF ||
      gScreenToDisplay == DISPLAY_SCANNER) {
    return false;
  }

  return gCurrentFunction == FUNCTION_INCOMING ||
         gCurrentFunction == FUNCTION_RECEIVE;
}

void AFC_Process10ms(void)
{
  uint32_t Result;
  uint32_t Base;
  int32_t Delta;

  Base = gRxVfo != 0 ? gRxVfo->pRX->Frequency : 0;
  if (Base != sBaseFrequency) {
    /* RADIO_SetupRegisters() already tuned the new channel: only stop scan. */
    if (sScanRunning) {
      BK4819_DisableFrequencyScan();
    }
    AFC_ClearState(false);
    sBaseFrequency = Base;
  }

  if (!AFC_CanRun()) {
    /* Never touch synthesizer registers after the transmitter has started. */
    AFC_ClearState(gCurrentFunction != FUNCTION_TRANSMIT);
    return;
  }

  if (sCountdown != 0) {
    sCountdown--;
    return;
  }

  if (!sScanRunning) {
    BK4819_EnableFrequencyScan();
    sScanRunning = true;
    sCountdown = AFC_SCAN_WAIT_TICKS;
    return;
  }

  if (!BK4819_GetFrequencyScanResult(&Result)) {
    sCountdown = AFC_RETRY_TICKS;
    return;
  }

  BK4819_DisableFrequencyScan();
  sScanRunning = false;

  Delta = (int32_t)Result - (int32_t)sBaseFrequency;
  const int16_t Limit = AFC_GetLimit10Hz();
  if (Delta < -Limit || Delta > Limit) {
    sStableHits = 0;
    sCountdown = AFC_UPDATE_TICKS;
    return;
  }

  if (sLastResult != 0) {
    int32_t Difference = (int32_t)Result - (int32_t)sLastResult;
    if (Difference < 0) {
      Difference = -Difference;
    }
    sStableHits = Difference <= AFC_STABLE_WINDOW_10HZ
                      ? (uint8_t)(sStableHits + 1)
                      : 1;
  } else {
    sStableHits = 1;
  }
  sLastResult = Result;

  if (sStableHits >= AFC_REQUIRED_HITS) {
    int16_t NewOffset = (int16_t)Delta;
    bool WasValid = sOffsetValid;

    /* Suppress frequency-counter jitter once tracking has locked. */
    if (sOffsetValid) {
      NewOffset = (int16_t)((sOffset10Hz + NewOffset) / 2);
    }

    if (NewOffset != sOffset10Hz) {
      sOffset10Hz = NewOffset;
      BK4819_SetFrequency((uint32_t)((int32_t)sBaseFrequency + sOffset10Hz));
      gUpdateDisplay = true;
    }
    sOffsetValid = true;
    if (!WasValid) {
      gUpdateDisplay = true;
    }
    sStableHits = 0;
    sLastResult = 0;
    sCountdown = AFC_UPDATE_TICKS;
  } else {
    sCountdown = AFC_RETRY_TICKS;
  }
}

void AFC_Reset(void)
{
  AFC_ClearState(true);
}

bool AFC_HasLock(void)
{
  return sOffsetValid;
}

int16_t AFC_GetOffsetHz(void)
{
  return (int16_t)(sOffset10Hz * 10);
}
