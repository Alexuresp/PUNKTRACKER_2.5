/* Automatic receive frequency correction for moving transmitters.
 *
 * The BK4819 performs FM AFC in hardware.  REG_6D contains the signed current
 * correction, which is displayed directly.  It must never be accumulated:
 * the register already represents the complete offset from the RX reference.
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

  return gCurrentFunction == FUNCTION_FOREGROUND ||
         gCurrentFunction == FUNCTION_INCOMING ||
         gCurrentFunction == FUNCTION_RECEIVE ||
         gCurrentFunction == FUNCTION_MONITOR;
}

void AFC_Process10ms(void)
{
  int32_t Sample;
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

  /* Robzyl displays the absolute hardware value immediately, without
   * accumulating it and without waiting for the squelch to open. */
  DisplayOffset = (int16_t)Sample;

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
