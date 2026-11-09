#include "service.hpp"

namespace {
void vblank() { inputGetAndSend(); }
void powerButton() { native_audio::service::exiting = true; }
}

int main() {
    enableSound();
    readUserSettings(); ledBlink(LED_ALWAYS_ON); touchInit();
    irqInit(); fifoInit();
    installSystemFIFO();
    // The native service is the only sound owner.
    native_audio::service::init();
    setPowerButtonCB(powerButton);
    initClockIRQTimer(LIBNDS_DEFAULT_TIMER_RTC);
    irqSet(IRQ_VBLANK, vblank); irqEnable(IRQ_VBLANK);
    while (!native_audio::service::exiting) {
        native_audio::service::poll();
        swiWaitForVBlank();
    }
    native_audio::service::shutdown();
    return 0;
}
