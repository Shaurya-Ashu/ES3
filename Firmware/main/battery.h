#pragma once
/* Battery voltage through the 1:2 divider on IO1. */

void battery_init(void);
float battery_voltage(void);         
int battery_percent(float volts);     