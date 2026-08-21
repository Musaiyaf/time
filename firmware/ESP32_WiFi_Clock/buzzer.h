#pragma once
#include <Arduino.h>

// Optional piezo buzzer on BUZZER_PIN (config.h): a short click on every
// button press, so the physical buttons get audible feedback the same way
// they already get a debounced tap/long-press detection. On/off state is
// persisted (Settings > Button Sound) so it survives a power cycle.
namespace Buzzer {

void begin();

// Fires a short click - safe to call from Menu's button poll on every
// press; a no-op when disabled.
void click();

void setEnabled(bool on);
bool isEnabled();

} // namespace Buzzer
