/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * P25 Phase 1 full-chain loopback test: IMBE frames -> LDU/FEC -> C4FM
 * modulator -> loopback -> demodulator -> FEC -> IMBE -> PCM, all without
 * hardware.
 */
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <utility>

#include <state.h>
#include <DSTAR/RingBuffer.h>
#include <P25/P25_IO.hpp>
#include <imbe_audio_codec.h>

/* Globals defined by OpMode_P25.cpp / P25_IO.cpp */
extern CRingBuffer<uint8_t> p25PacketBuffer;
extern CRingBuffer<uint8_t> p25LDUBuffer;
extern CRingBuffer<uint16_t> p25_rxBuffer;
extern CRingBuffer<uint16_t> p25_txBuffer;

/* imbe_audio_codec.cpp */
void encodeIMBE(int16_t *pcm, uint8_t *imbe);
void decodeIMBE(int16_t *pcm, uint8_t *imbe);

static const size_t FRAME_BYTES = 11U;
static const size_t FRAMES_PER_LDU_PAIR = 18U;

static const char *assetDir()
{
#ifndef P25_ASSETS_DIR
#define P25_ASSETS_DIR "tests/unit/assets"
#endif
    return P25_ASSETS_DIR;
}

static std::vector<uint8_t> readFile(const char *path)
{
    std::vector<uint8_t> out;
    FILE *f = std::fopen(path, "rb");
    if (f == NULL)
        return out;

    std::fseek(f, 0, SEEK_END);
    long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(len);
    if (std::fread(out.data(), 1U, len, f) != (size_t)len)
        out.clear();
    std::fclose(f);
    return out;
}

/* Move all complete RX IMBE frames out of p25PacketBuffer */
static void drainRxFrames(std::vector<uint8_t> &dest)
{
    uint8_t frame[FRAME_BYTES];

    while (p25PacketBuffer.getData() >= FRAME_BYTES) {
        for (size_t i = 0U; i < FRAME_BYTES; i++)
            p25PacketBuffer.get(frame[i]);

        dest.insert(dest.end(), frame, frame + FRAME_BYTES);
    }
}

/* Pump every TX baseband sample through the RX chain */
static FILE *dumpFile = NULL;

static void pumpLoopback(P25_IO &io)
{
    uint16_t sample;

    while (p25_txBuffer.getData() > 0U) {
        p25_txBuffer.get(sample);
        if (dumpFile != NULL) {
            int16_t s = (int16_t)sample;
            std::fwrite(&s, sizeof(s), 1U, dumpFile);
        }
        p25_rxBuffer.put(sample);
        io.process();
    }
}

/* Decode every complete LDU sitting in p25LDUBuffer */
static void pumpDecode(P25_IO &io, std::vector<uint8_t> &rxFrames)
{
    while (p25LDUBuffer.getData() >= (P25_LDU_FRAME_LENGTH_BYTES + 1U)) {
        io.decode();
        drainRxFrames(rxFrames);
    }
}

static std::vector<uint8_t> goldenToneFrames()
{
    char path[512];
    std::snprintf(path, sizeof(path), "%s/golden_burst_tone_enc.frames",
                  assetDir());
    return readFile(path);
}

TEST_CASE("P25 IMBE vocoder is bit-exact against the golden encoder",
          "[p25][vocoder]")
{
    char path[512];
    std::snprintf(path, sizeof(path), "%s/burst_tone.raw", assetDir());
    std::vector<uint8_t> pcm = readFile(path);
    std::vector<uint8_t> golden = goldenToneFrames();

    REQUIRE(pcm.size() >= 320U);
    REQUIRE(golden.size() >= pcm.size() / 320U * FRAME_BYTES);

    size_t nFrames = pcm.size() / 320U;
    size_t encErrs = 0U;

    for (size_t f = 0U; f < nFrames; f++) {
        uint8_t frame[FRAME_BYTES];
        encodeIMBE((int16_t *)(pcm.data() + f * 320U), frame);
        for (size_t i = 0U; i < FRAME_BYTES; i++)
            if (frame[i] != golden[f * FRAME_BYTES + i])
                encErrs++;
    }

    INFO("byte errors vs golden: " << encErrs << " of "
                                   << nFrames * FRAME_BYTES);
    REQUIRE(encErrs == 0U);
}

TEST_CASE("P25 audio codec round-trips clean LDUs", "[p25][audio]")
{
    unsigned char ldu[216];
    unsigned char imbe[11] = { 0x75, 0x5F, 0xA1, 0x78, 0xCF, 0xFF,
                               0x03, 0x6B, 0x01, 0x5F, 0x01 };
    unsigned char back[11];

    std::memset(ldu, 0x00, sizeof(ldu));
    CP25Audio audio;
    for (unsigned n = 0U; n < 9U; n++) {
        audio.encode(ldu, imbe, n);
        std::memset(back, 0xAA, sizeof(back));
        audio.decode(ldu, back, n);
        size_t e = 0U;
        for (size_t i = 0U; i < 11U; i++)
            if (back[i] != imbe[i])
                e++;
        INFO("slot " << n << ": " << e << "/11 bytes differ");
        REQUIRE(e == 0U);
    }
}

TEST_CASE("P25 FEC repairs injected codeword errors without collateral damage",
          "[p25][fec]")
{
    unsigned char ldu[216];
    unsigned char frames9[9][11];
    CP25Audio audio;
    std::vector<uint8_t> golden = goldenToneFrames();

    REQUIRE(golden.size() >= 9U * FRAME_BYTES);

    /* nine distinct, valid frames (from the golden stream) */
    for (unsigned n = 0U; n < 9U; n++)
        std::memcpy(frames9[n], &golden[n * FRAME_BYTES], FRAME_BYTES);

    for (unsigned nerrs = 1U; nerrs <= 2U; nerrs++) {
        std::memset(ldu, 0x00, sizeof(ldu));
        for (unsigned n = 0U; n < 9U; n++)
            audio.encode(ldu, frames9[n], n);

        /* flip bits inside voice slot 0: LDU bits 114..262 */
        unsigned bitpos[2] = { 114U, 180U };
        for (unsigned e = 0U; e < nerrs; e++)
            ldu[(bitpos[e]) >> 3] ^= 0x80U >> ((bitpos[e]) & 7U);

        audio.process(ldu);

        for (unsigned n = 0U; n < 9U; n++) {
            unsigned char back[11];
            std::memset(back, 0xAA, sizeof(back));
            audio.decode(ldu, back, n);
            size_t wrong = 0U;
            for (size_t i = 0U; i < FRAME_BYTES; i++)
                if (back[i] != frames9[n][i])
                    wrong++;
            INFO(nerrs << " injected errors, slot " << n << ": " << wrong
                       << "/11 bytes wrong");
            REQUIRE(wrong == 0U);
        }
    }
}

TEST_CASE("P25 full-chain loopback recovers voice frames bit-exactly",
          "[p25][loopback]")
{
    /*
     * P25_AUDIO_IN (raw s16le 8 kHz mono) replaces the golden frames with
     * live encoding through the radio's own encodeIMBE() - useful for
     * auditioning real speech through the complete chain.
     */
    std::vector<uint8_t> txFrames;
    char path[512];

    if (getenv("P25_AUDIO_IN") != NULL) {
        std::vector<uint8_t> pcm = readFile(getenv("P25_AUDIO_IN"));
        REQUIRE(pcm.size() >= 320U);
        size_t n = pcm.size() / 320U;
        for (size_t f = 0U; f < n; f++) {
            uint8_t frame[FRAME_BYTES];
            encodeIMBE((int16_t *)(pcm.data() + f * 320U), frame);
            txFrames.insert(txFrames.end(), frame, frame + FRAME_BYTES);
        }
    } else {
        std::snprintf(path, sizeof(path), "%s/golden_mixed_enc.frames",
                      assetDir());
        txFrames = readFile(path);
    }

    REQUIRE(!txFrames.empty());
    REQUIRE((txFrames.size() % FRAME_BYTES) == 0U);

    size_t totalFrames = txFrames.size() / FRAME_BYTES;

    /* pad to whole LDU pairs with the standard silence frame */
    static const uint8_t SILENCE[FRAME_BYTES] = { 0x04, 0x0C, 0xFD, 0x7B,
                                                  0xFB, 0x7D, 0xF2, 0x7B,
                                                  0x3D, 0x9E, 0x45 };

    while ((totalFrames % FRAMES_PER_LDU_PAIR) != 0U) {
        txFrames.insert(txFrames.end(), SILENCE, SILENCE + FRAME_BYTES);
        totalFrames++;
    }

    /* radio state defaults */
    std::memset(&state, 0x00, sizeof(state));
    state.settings.p25_rx_level = 100;
    state.settings.p25_tx_level = 160;

    P25_IO io;
    io.init_tx();
    io.setSrcId(0x0BAD); /* source radio id 2989 */
    io.setDstId(1U);     /* talkgroup 1 */
    io.writeSyncFrames();
    pumpLoopback(io);
    io.createTxHeader();
    pumpLoopback(io);

    /* stream the voice */
    std::vector<uint8_t> rxFrames;
    size_t framePtr = 0U;
    while (framePtr < totalFrames) {
        /* fill the TX packet buffer with one LDU pair worth of audio */
        p25PacketBuffer.reset();
        for (size_t i = 0U; i < FRAMES_PER_LDU_PAIR * FRAME_BYTES; i++)
            p25PacketBuffer.put(txFrames[framePtr * FRAME_BYTES + i]);
        framePtr += FRAMES_PER_LDU_PAIR;

        /* pump between frames: one writeData fills 4320 samples into a
           5000-sample TX ring, so two back-to-back frames would overflow */
        io.createTxLDU1();
        pumpLoopback(io);
        io.createTxLDU2();
        pumpLoopback(io);
        pumpDecode(io, rxFrames);
    }

    /* terminate the transmission */
    p25PacketBuffer.reset();
    io.createTxLDU1();
    pumpLoopback(io);
    io.createTxLDU2();
    pumpLoopback(io);
    io.createTxTerminator();
    pumpLoopback(io);

    /* flush RX side */
    pumpDecode(io, rxFrames);

    /*
     * The RX stream is a contiguous subsequence of the TX stream: it may
     * be missing frames at the head (sync acquisition) and may carry
     * extra frames at the tail (silence after PTT release), but nothing
     * inside the transmission may differ.  Find the TX offset `skip` and
     * RX offset `pre` of the longest matching contiguous run.
     */
    size_t nRx = rxFrames.size() / FRAME_BYTES;
    INFO("TX frames: " << totalFrames << ", RX frames: " << nRx);
    REQUIRE(nRx > 0U);

    size_t bestSkip = 0U, bestPre = 0U, bestLen = 0U;

    for (size_t pre = 0U; pre < nRx; pre++) {
        for (size_t skip = 0U; skip < totalFrames; skip++) {
            size_t len = 0U;

            while ((pre + len) < nRx && (skip + len) < totalFrames) {
                size_t e = 0U;
                for (size_t k = 0U; k < FRAME_BYTES; k++)
                    if (rxFrames[(pre + len) * FRAME_BYTES + k]
                        != txFrames[(skip + len) * FRAME_BYTES + k])
                        e++;
                if (e != 0U)
                    break;

                len++;
            }

            if (len > bestLen) {
                bestLen = len;
                bestSkip = skip;
                bestPre = pre;
            }

            if (len > 0U && (skip + len) >= totalFrames)
                break;
        }
    }

    /* sync acquisition eats at most the first LDU pair */
    INFO("longest clean run: " << bestLen << " frames (TX offset " << bestSkip
                               << ", RX offset " << bestPre << ") of "
                               << totalFrames << " TX/" << nRx << " RX");
    REQUIRE(bestSkip <= FRAMES_PER_LDU_PAIR);
    REQUIRE(bestLen >= totalFrames - FRAMES_PER_LDU_PAIR);

    /* decode the recovered audio for audition */
    std::vector<int16_t> audioOut;
    audioOut.reserve(nRx * 160U);
    for (size_t j = 0U; j < nRx; j++) {
        int16_t pcmOut[160];
        decodeIMBE(pcmOut, &rxFrames[j * FRAME_BYTES]);
        audioOut.insert(audioOut.end(), pcmOut, pcmOut + 160U);
    }

    std::snprintf(path, sizeof(path), "%s/p25_loopback_out.raw", assetDir());
    FILE *of = std::fopen(path, "wb");
    if (of != NULL) {
        std::fwrite(audioOut.data(), sizeof(int16_t), audioOut.size(), of);
        std::fclose(of);
    }

    if (dumpFile != NULL) {
        std::fflush(dumpFile);
        std::fclose(dumpFile);
    }
}

/*
 * Custom entry point: the emulator sources linked into this test own static
 * objects whose destructors are not exit-safe, so static teardown is skipped
 * with quick_exit() once Catch2 has reported.
 */
int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

    if (getenv("P25_DUMP_TX") != NULL)
        dumpFile = std::fopen(getenv("P25_DUMP_TX"), "wb");

    Catch::Session session;
    int rc = session.run(argc, argv);

    if (dumpFile != NULL) {
        std::fflush(dumpFile);
        std::fclose(dumpFile);
    }

    std::quick_exit(rc);
}
