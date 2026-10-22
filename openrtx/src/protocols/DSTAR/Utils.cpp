/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hex dump helper used by the P25 modem (MMDVM lineage).
 */
#include <cstdio>
#include <DSTAR/Utils.h>

void CUtils::dump(const char *title, const uint8_t *data, uint32_t length)
{
    ::fprintf(stderr, "%s\n", title);

    for (uint32_t i = 0U; i < length; i += 16U) {
        ::fprintf(stderr, "%04u: ", i);

        for (uint32_t j = 0U; j < 16U; j++) {
            if ((i + j) < length)
                ::fprintf(stderr, "%02X ", data[i + j]);
            else
                ::fprintf(stderr, "   ");
        }

        ::fprintf(stderr, "\n");
    }

    ::fprintf(stderr, "\n");
}
