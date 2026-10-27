/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * P25 off-air replay: feed a 24 kHz s16le discriminator-baseband capture
 * (extracted from an IQ recording) through the real P25_IO demod + frame
 * decoder and report the decoded link-control content and frame counts.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include <state.h>
#include <P25/vendor/RingBuffer.h>
#include <P25/P25_IO.hpp>
#include <P25/P25NID.h>
#include <P25/P25Utils.h>
#include <P25/P25Data.h>

extern CRingBuffer<uint8_t> p25PacketBuffer;
extern CRingBuffer<uint8_t> p25LDUBuffer;
extern CRingBuffer<uint16_t> p25_rxBuffer;
extern CRingBuffer<uint16_t> p25_txBuffer;

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <baseband.s16>\n", argv[0]);
        return 1;
    }

    FILE *f = std::fopen(argv[1], "rb");
    if (f == NULL) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    std::memset(&state, 0x00, sizeof(state));
    state.settings.p25_rx_level = 255;
    state.settings.p25_tx_level = 160;
    state.settings.p25_nac = 0x293;
    if (getenv("P25_NAC") != NULL)
        state.settings.p25_nac = (uint16_t)strtoul(getenv("P25_NAC"), NULL, 0);

    P25_IO io;
    io.reset();

    long bruteforced = 0L;
    const size_t BLK = 180U;
    std::vector<int16_t> pcm(BLK);
    long blocks = 0U;
    unsigned prevLdu = 0U;
    unsigned prevSrc = 0U;

    while (std::fread(pcm.data(), sizeof(int16_t), BLK, f) == BLK) {
        for (size_t i = 0U; i < BLK; i++)
            p25_rxBuffer.put((uint16_t)pcm[i]);
        /* process() consumes RX_BLOCK_SIZE(=2) samples per call */
        while (p25_rxBuffer.getData() >= 2U)
            io.process();

        while (p25LDUBuffer.getData() >= (P25_LDU_FRAME_LENGTH_BYTES + 1U)) {
            if (bruteforced < 3) {
                unsigned char raw[P25_LDU_FRAME_LENGTH_BYTES + 1];
                for (unsigned i = 0U; i < P25_LDU_FRAME_LENGTH_BYTES + 1U; i++)
                    p25LDUBuffer.get(raw[i]);
                for (unsigned i = 0U; i < P25_LDU_FRAME_LENGTH_BYTES + 1U; i++)
                    p25LDUBuffer.put(raw[i]);
                unsigned char nid_bits[P25_NID_LENGTH_BYTES];
                CP25Utils::decode(raw + 1, nid_bits, 48U, 114U);

                CP25NID probe(0U);
                for (unsigned nac = 0U; nac < 4096U; nac++) {
                    probe.setNAC(nac);
                    unsigned char scratch[120];
                    const unsigned duids[5] = { P25_DUID_LDU1, P25_DUID_LDU2,
                                                P25_DUID_TERM, P25_DUID_TERM_LC,
                                                P25_DUID_HEADER };
                    for (unsigned di = 0U; di < 5U; di++) {
                        unsigned d = duids[di];
                        std::memset(scratch, 0, sizeof(scratch));
                        probe.encode(scratch, d);
                        unsigned char want[P25_NID_LENGTH_BYTES];
                        CP25Utils::decode(scratch, want, 48U, 114U);
                        if (CP25Utils::compare(nid_bits, want,
                                               P25_NID_LENGTH_BYTES)
                            < 3) {
                            std::printf("NID BRUTE: frame %ld NAC=$%03X "
                                        "duid-slot=%u\n",
                                        bruteforced, nac, d);
                            nac = 4096U;
                            break;
                        }
                    }
                }
                bruteforced++;
            }
            io.decode();
            unsigned src = io.getSrcId();
            unsigned ldu = io.getRxLduCount();
            if (src != prevSrc || ldu > prevLdu + 1U) {
                std::printf("block %ld: LDU decoded src=%u dst=%u "
                            "lcf=0x%02X (ldu#%u)\n",
                            blocks, io.getSrcId(), io.getDstId(), io.getLcf(),
                            ldu);
            }
            prevSrc = src;
            prevLdu = ldu;
        }
        blocks++;
    }

    std::printf("== replay done: %ld blocks (%.2f s), rxLDU=%u, "
                "last src=%u dst=%u ==\n",
                blocks, (double)blocks * 180.0 / 24000.0, io.getRxLduCount(),
                io.getSrcId(), io.getDstId());
    std::fclose(f);
    std::fflush(stdout);
    std::_Exit(0);
}
