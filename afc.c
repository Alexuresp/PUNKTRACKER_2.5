/* Automatic receive frequency correction for moving transmitters.
 *
 * The BK4819 FM demodulator performs the actual correction in hardware.
 * REG_6D exposes its signed residual frequency error, so no frequency-scanner
 * cycle is needed and normal modulated speech cannot prevent AFC lock.
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
  AFC_SAMPLE_TICKS = 10, /* Read the hardware AFC every 100 ms. */
};

static int16_t sOffsetHz;
static uint8_t sCountdown;
static bool sOffsetValid;

static int16_t AFC_GetLimitHz(void)
{
  return gEeprom.AFC_RANGE == AFC_RANGE_MAX ? 10000 : 7000;
}

static void AFC_ClearState(void)
{
  const bool WasValid = sOffsetValid;

  sOffsetHz = 0;
  sCountdown = 0;
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

  return gCurrentFunction == FUNCTION_INCOMING ||
         gCurrentFunction == FUNCTION_RECEIVE;
}

void AFC_Process10ms(void)
{
  int32_t Sample;
  int32_t Filtered;
  int16_t DisplayOffset;
  const int16_t Limit = AFC_GetLimitHz();

  if (!AFC_CanRun()) {
    AFC_ClearState();
    return;
  }

  if (sCountdown != 0) {
    sCountdown--;
    return;
  }
  sCountdown = AFC_SAMPLE_TICKS - 1;

  Sample = BK4819_GetAFCOffsetHz();
  if (Sample > Limit) {
    Sample = Limit;
  } else if (Sample < -Limit) {
    Sample = -Limit;
  }

  /* REG_6D is already filtered by the hardware AFC loop.  A light software
   * filter keeps speech modulation and the last display digit from jittering. */
  Filtered = sOffsetValid ? ((int32_t)sOffsetHz * 3 + Sample) / 4 : Sample;
  DisplayOffset =
      (int16_t)((Filtered >= 0 ? Filtered + 5 : Filtered - 5) / 10 * 10);

  if (!sOffsetValid || DisplayOffset != sOffsetHz) {
    sOffsetHz = DisplayOffset;
    sOffsetValid = true;
    gUpdateDisplay = true;
  }
}

void AFC_Reset(void)
{
  AFC_ClearState();
}

bool AFC_HasLock(void)
{
  return sOffsetValid;
}

int16_t AFC_GetOffsetHz(void)
{
  return sOffsetHz;
}
