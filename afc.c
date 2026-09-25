/* Automatic receive frequency correction for moving transmitters.
 *
 * The BK4819 hardware AFC reports the residual error in REG_6D.  Software
 * moves the RX synthesizer by that error so an off-frequency carrier is also
 * centred for the squelch detector.  Channel memory and TX stay unchanged.
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
  AFC_SAMPLE_TICKS = 10,       /* Read AFC and RSSI every 100 ms. */
  AFC_SETTLE_SAMPLES = 4,      /* Allow 400 ms after a synthesizer move. */
  AFC_SIGNAL_HOLD_SAMPLES = 30,/* Keep correction through a 3 s fade. */
  AFC_RSSI_MARGIN = 8,         /* Track slightly below the SQL threshold. */
  AFC_MIN_RSSI = 36,           /* About -124 dBm; rejects the noise floor. */
  AFC_RETUNE_THRESHOLD_HZ = 80,
};

static uint32_t sBaseFrequency;
static int16_t sOffsetHz;
static uint8_t sCountdown;
static uint8_t sSettleSamples;
static uint8_t sSignalLostSamples;
static bool sOffsetValid;

static int16_t AFC_GetLimitHz(void)
{
  return gEeprom.AFC_RANGE == AFC_RANGE_MAX ? 10000 : 7000;
}

static void AFC_ClearState(bool Retune)
{
  const bool WasValid = sOffsetValid;

  if (Retune && sOffsetHz != 0 && sBaseFrequency != 0) {
    BK4819_SetFrequency(sBaseFrequency);
  }
  sOffsetHz = 0;
  sCountdown = 0;
  sSettleSamples = 0;
  sSignalLostSamples = 0;
  sOffsetValid = false;
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

  return gCurrentFunction == FUNCTION_FOREGROUND ||
         gCurrentFunction == FUNCTION_INCOMING ||
         gCurrentFunction == FUNCTION_RECEIVE ||
         gCurrentFunction == FUNCTION_MONITOR;
}

static bool AFC_HasUsableSignal(void)
{
  uint16_t Minimum = gRxVfo->SquelchOpenRSSIThresh;

  Minimum = Minimum > AFC_RSSI_MARGIN ? Minimum - AFC_RSSI_MARGIN : 0;
  if (Minimum < AFC_MIN_RSSI) {
    Minimum = AFC_MIN_RSSI;
  }
  return (BK4819_GetRSSI() >> 1) >= Minimum;
}

void AFC_Process10ms(void)
{
  int32_t ResidualHz;
  int32_t NewOffsetHz;
  uint32_t Base = gRxVfo != 0 ? gRxVfo->pRX->Frequency : 0;
  const int16_t Limit = AFC_GetLimitHz();

  if (Base != sBaseFrequency) {
    /* Normal radio setup has already tuned the newly selected channel. */
    AFC_ClearState(false);
    sBaseFrequency = Base;
  }

  if (!AFC_CanRun()) {
    AFC_ClearState(gCurrentFunction != FUNCTION_TRANSMIT);
    return;
  }

  if (sCountdown != 0) {
    sCountdown--;
    return;
  }
  sCountdown = AFC_SAMPLE_TICKS - 1;

  if (!AFC_HasUsableSignal()) {
    if (sSignalLostSamples < AFC_SIGNAL_HOLD_SAMPLES) {
      sSignalLostSamples++;
    } else {
      AFC_ClearState(true);
    }
    return;
  }
  sSignalLostSamples = 0;

  if (sSettleSamples != 0) {
    sSettleSamples--;
    return;
  }

  ResidualHz = BK4819_GetAFCOffsetHz();
  if (ResidualHz > Limit) {
    ResidualHz = Limit;
  } else if (ResidualHz < -Limit) {
    ResidualHz = -Limit;
  }

  /* Ignore tiny residual movement so modulation cannot make the PLL hunt. */
  if (ResidualHz > -AFC_RETUNE_THRESHOLD_HZ &&
      ResidualHz < AFC_RETUNE_THRESHOLD_HZ) {
    if (!sOffsetValid) {
      sOffsetValid = true;
      gUpdateDisplay = true;
    }
    return;
  }

  NewOffsetHz = (int32_t)sOffsetHz + ResidualHz;
  if (NewOffsetHz > Limit) {
    NewOffsetHz = Limit;
  } else if (NewOffsetHz < -Limit) {
    NewOffsetHz = -Limit;
  }
  NewOffsetHz = (NewOffsetHz >= 0 ? NewOffsetHz + 5 : NewOffsetHz - 5) / 10 * 10;

  if (NewOffsetHz != sOffsetHz) {
    sOffsetHz = (int16_t)NewOffsetHz;
    BK4819_SetFrequency((uint32_t)((int32_t)sBaseFrequency + sOffsetHz / 10));
    sSettleSamples = AFC_SETTLE_SAMPLES;
    sOffsetValid = true;
    gUpdateDisplay = true;
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
  return sOffsetHz;
}
