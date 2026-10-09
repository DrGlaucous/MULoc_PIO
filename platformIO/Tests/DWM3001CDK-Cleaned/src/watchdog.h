#pragma once
#include <Arduino.h>



//platform specific!
class NRF52WatchdogHandler {

    public:

    //todo: allow RR[number] to be changed so we can have multiple watchdogs
    //NRF52WatchdogHandler() {}

    //start the watchdog with a timeout in milliseconds
    //`pause_on_deep_sleep` will halt the watchdog when deep_sleep_ms() is active.
    //`pause_on_debug` will halt the watchdog when breakpoint debugging is active.
    static void init(uint32_t timeout_ms, bool pause_on_deep_sleep = true, bool pause_on_debug = true)
    {
        //ensure the Low-Frequency Clock (LFCLK) is running, as the WDT relies on it
        if ((NRF_CLOCK->LFCLKSTAT & CLOCK_LFCLKSTAT_STATE_Msk) == CLOCK_LFCLKSTAT_STATE_NotRunning) {
            //not running, start it
            NRF_CLOCK->TASKS_LFCLKSTART = 1;
            while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0) {
                //wait for clock to stabilize
            }
            //clear flag
            NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
        }

        //set the Counter Reload Value
        //formula: timeout seconds * 32768 Hz
        //to handle milliseconds: (ms * 32768) / 1000 => (ms * 4096) / 125
        uint32_t crv_ticks = (timeout_ms * 4096) / 125;
        NRF_WDT->CRV = crv_ticks;

        //configure Watchdog behavior (CONFIG)
        //bit 0: Sleep behavior (0 = Pause watchdog when CPU is sleeping)
        //bit 1: Halt behavior  (0 = Pause watchdog when CPU is halted by debugger)
        NRF_WDT->CONFIG = ((pause_on_deep_sleep ? WDT_CONFIG_SLEEP_Pause : WDT_CONFIG_SLEEP_Run) << WDT_CONFIG_SLEEP_Pos) | 
                        ((pause_on_debug ? WDT_CONFIG_HALT_Pause : WDT_CONFIG_HALT_Run) << WDT_CONFIG_HALT_Pos);

        //enable Reload Request channel 0 (RREN)
        NRF_WDT->RREN = (WDT_RREN_RR0_Enabled << WDT_RREN_RR0_Pos);

        //start the Watchdog task
        //after this line, CONFIG, CRV, and RREN are locked down until the next reset
        NRF_WDT->TASKS_START = 1;
    }

    //rest the watchdog timer
    static void feed() {
        NRF_WDT->RR[0] = WDT_RR_RR_Reload;
    }

    //true if the last reset was caused by the wdt
    static bool caused_reset() {
        uint32_t reason = NRF_POWER->RESETREAS;
        if (reason & POWER_RESETREAS_DOG_Msk) {
            //clear flag
            NRF_POWER->RESETREAS = POWER_RESETREAS_DOG_Msk;
            return true;
        }
        return false;
    }



};









