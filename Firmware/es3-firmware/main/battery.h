#pragma once
/* Battery voltage through the 1:2 divider on IO1 (ADC1 channel 0). */

void battery_init(void);
float battery_voltage(void);          /* volts at the cell, averaged */
int battery_percent(float volts);     /* rough LiPo estimate, 0..100 */
