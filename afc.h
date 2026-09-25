/* Automatic receive frequency correction for moving transmitters. */

#ifndef AFC_H
#define AFC_H

#include <stdbool.h>
#include <stdint.h>

enum {
  AFC_RANGE_STANDARD = 0,
  AFC_RANGE_MAX = 1,
};

void AFC_Process10ms(void);
void AFC_Reset(void);
bool AFC_HasLock(void);
int16_t AFC_GetOffsetHz(void);

#endif
