/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "hwconfig.h"
#include <stdbool.h>

typedef enum {
    TIMER_OFF = 0,
    TIMER_5S = 1,
    TIMER_10S = 2,
    TIMER_15S = 3,
    TIMER_20S = 4,
    TIMER_25S = 5,
    TIMER_30S = 6,
    TIMER_1M = 7,
    TIMER_2M = 8,
    TIMER_3M = 9,
    TIMER_4M = 10,
    TIMER_5M = 11,
    TIMER_15M = 12,
    TIMER_30M = 13,
    TIMER_45M = 14,
    TIMER_1H = 15
} display_timer_t;

typedef struct {
    uint8_t brightness;        // Display brightness
    uint8_t contrast;          // Display contrast
    uint8_t sqlLevel;          // Squelch level
    uint8_t voxLevel;          // Vox level
    int8_t utc_timezone;       // Timezone, in units of half hours
    bool gps_enabled;          // GPS active
    char callsign[10];         // Plaintext callsign
    uint8_t display_timer : 4, // Standby timer
        m17_can           : 4; // M17 CAN
    uint8_t vpLevel       : 3, // Voice prompt level
        vpPhoneticSpell   : 1, // Phonetic spell enabled
        macroMenuLatch    : 1, // Automatic latch of macro menu
        _reserved         : 3;
    bool m17_can_rx;           // Check M17 CAN on RX
    char m17_dest[10];         // M17 destination
    bool showBatteryIcon;      // Battery display true: icon, false: percentage
    bool gpsSetTime;           // Use GPS to ajust RTC time
    char M17_meta_text[53];    // M17 Meta Text to send
#ifdef CONFIG_P25
    uint8_t p25_preamble;      // P25 TX preamble option, bring-up aid
    uint8_t p25_tx_level;      // P25 TX BB level
    uint32_t p25_srcId;        // P25 Source ID (DMR ID)
    uint32_t p25_dstId;        // P25 Destination ID (DMR ID or TG)
    uint16_t p25_nac;          // P25 NAC
    uint8_t p25_rx_invert : 1, // P25 RX signal invert
        p25_tx_invert     : 1, // P25 TX signal invert
        p25_dst_check     : 1; // Check P25 destination ID on RX
#endif
} __attribute__((packed)) settings_t;

#ifdef CONFIG_P25
// P25 TX preamble options: field-test/bring-up aid, short/long variants of
// three patterns at equal duration (short = 576 symbols = 120 ms, long =
// 1020 symbols = 212.5 ms). EFJ is the unmodulated carrier a factory EF
// Johnson subscriber transmits (measured off air, spectrum-verified, 2026-11
// bench captures); Kenwood is the official fielded +3,+3,-3,-3 square wave;
// TIA is the alternating +-3 preamble. Stale settings blobs remap in place
// (old TIA/MMDVM values become the EFJ options, old sweep values the TIA
// options); Kenwood keeps its slots and stays the default: zero-mean and
// clock-rich, between the EFJ's no-deviation carrier and the TIA all-outer
// alternating pattern.
enum p25_preamble_t {
    P25_PREAMBLE_EFJ_SHORT = 0,
    P25_PREAMBLE_EFJ_LONG,
    P25_PREAMBLE_KENWOOD_SHORT,
    P25_PREAMBLE_KENWOOD_LONG,
    P25_PREAMBLE_TIA_SHORT,
    P25_PREAMBLE_TIA_LONG,
    P25_PREAMBLE_NUM
};
#endif

static const settings_t default_settings = {
    100,                     // Brightness
#ifdef CONFIG_SCREEN_CONTRAST
    CONFIG_DEFAULT_CONTRAST, // Contrast
#else
    255, // Contrast
#endif
    4,         // Squelch level, 4 = S3
    0,         // Vox level
    0,         // UTC Timezone
    false,     // GPS enabled
    "N0CALL",  // Default callsign
    TIMER_30S, // 30 seconds
    0,         // M17 CAN
    0,         // Voice prompts off
    0,         // Phonetic spell off
    1,         // Automatic latch of macro menu enabled
    0,         // not used
    false,     // Check M17 CAN on RX
    "",        // Empty M17 destination
    false,     // Display battery icon
    false,     // Update RTC with GPS
    "OpenRTX"  // Default M17 meta text
#ifdef CONFIG_P25
    ,
    P25_PREAMBLE_KENWOOD_SHORT // Default P25 TX preamble: official Kenwood
                               // configuration, +3,+3,-3,-3 x 144 (120 ms)
    ,
    166 // Default P25 TX BB level: 96 counts/step; on-air calibration of
        // the CS7000P (HR_C6000 LINE_IN, deviation coefficient 0x25) gives
        // 10.8 Hz per step, so 166 = +-1800 Hz outer deviation (TIA-102)
    ,
    0   // Empty P25 Source ID
    ,
    0   // Empty P25 Destination ID
    ,
    293 // Default P25 NAC
    ,
    1   // P25 RX invert
    ,
    0   // P25 TX invert
    ,
    0   // Check P25 destination ID on RX
#endif

};

#endif /* SETTINGS_H */
