/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * P25 RX noise-tolerance sweep: drives the full TX->RX loopback chain
 * with additive white Gaussian noise between the modulator and the
 * demodulator, and reports how many voice frames survive at each
 * noise-to-signal ratio.  Calibration target: on the CS7000P bench the
 * idle discriminator hiss peaks around 2600 counts while a mid-distance
 * signal swings ~1500, i.e. Gaussian sigma ~= 0.37 x signal peak.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <random>
#include <vector>

#include <state.h>
#include <P25/vendor/RingBuffer.h>
#include <P25/P25_IO.hpp>

/* Globals defined by OpMode_P25.cpp / P25_IO.cpp */
extern CRingBuffer<uint8_t> p25PacketBuffer;
extern CRingBuffer<uint8_t> p25LDUBuffer;
extern CRingBuffer<uint16_t> p25_rxBuffer;
extern CRingBuffer<uint16_t> p25_txBuffer;

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

static double gauss(std::mt19937_64 &rng)
{
    static std::uniform_real_distribution<double> u(0.0, 1.0);
    static bool cached = false;
    static double cache = 0.0;

    if (cached) {
        cached = false;
        return cache;
    }

    double v1 = u(rng);
    double v2 = u(rng);
    if (v1 <= 0.0)
        v1 = 1e-12;
    double r = std::sqrt(-2.0 * std::log(v1));
    double theta = 2.0 * 3.14159265358979323846 * v2;
    cache = r * std::sin(theta);
    cached = true;
    return r * std::cos(theta);
}

static int16_t g_txAbsPeak = 0; /* max |sample| seen on the wire */

static void pumpLoopback(P25_IO &io, double sigma, std::mt19937_64 &rng)
{
    uint16_t sample;

    while (p25_txBuffer.getData() > 0U) {
        p25_txBuffer.get(sample);
        {
            int16_t a = (int16_t)sample;
            if (a < 0)
                a = -a;
            if (a > g_txAbsPeak)
                g_txAbsPeak = a;
        }
        if (sigma > 0.0) {
            int32_t s = (int32_t)(int16_t)sample
                      + (int32_t)std::lround(gauss(rng) * sigma);
            if (s > 32767)
                s = 32767;
            if (s < -32768)
                s = -32768;
            sample = (uint16_t)(int16_t)s;
        }
        p25_rxBuffer.put(sample);
        io.process();
    }
}

static void drainRxFrames(std::vector<uint8_t> &dest)
{
    uint8_t frame[FRAME_BYTES];

    while (p25PacketBuffer.getData() >= FRAME_BYTES) {
        for (size_t i = 0U; i < FRAME_BYTES; i++)
            p25PacketBuffer.get(frame[i]);

        dest.insert(dest.end(), frame, frame + FRAME_BYTES);
    }
}

static void pumpDecode(P25_IO &io, std::vector<uint8_t> &rxFrames)
{
    while (p25LDUBuffer.getData() >= (P25_LDU_FRAME_LENGTH_BYTES + 1U)) {
        io.decode();
        drainRxFrames(rxFrames);
    }
}

/* longest contiguous in-order match of rx inside tx; returns length */
static size_t longestRun(const std::vector<uint8_t> &tx,
                         const std::vector<uint8_t> &rx)
{
    size_t nTx = tx.size() / FRAME_BYTES;
    size_t nRx = rx.size() / FRAME_BYTES;
    size_t bestLen = 0U;

    for (size_t pre = 0U; pre < nRx; pre++) {
        for (size_t skip = 0U; skip < nTx; skip++) {
            size_t len = 0U;
            while ((pre + len) < nRx && (skip + len) < nTx) {
                if (std::memcmp(&rx[(pre + len) * FRAME_BYTES],
                                &tx[(skip + len) * FRAME_BYTES], FRAME_BYTES)
                    != 0)
                    break;
                len++;
            }
            if (len > bestLen)
                bestLen = len;
        }
    }
    return bestLen;
}

int main()
{
    std::vector<uint8_t> txFrames;
    char path[512];
    std::snprintf(path, sizeof(path), "%s/golden_mixed_enc.frames", assetDir());
    txFrames = readFile(path);
    if (txFrames.empty() || (txFrames.size() % FRAME_BYTES) != 0U) {
        std::fprintf(stderr, "missing/bad golden frames asset\n");
        return 1;
    }

    size_t totalFrames = txFrames.size() / FRAME_BYTES;
    while ((totalFrames % FRAMES_PER_LDU_PAIR) != 0U)
        totalFrames--; /* asset is padded in practice */

    std::printf("TX frames: %zu\n", totalFrames);

    /* radio-like settings: RX gain maxed (Steve's bench), default TX */
    std::memset(&state, 0x00, sizeof(state));
    state.settings.p25_rx_level = 255;
    state.settings.p25_tx_level = 160;

    /*
     * ONE P25_IO for the whole program: repeated construction tears up
     * shared vocoder state (double-free).  reset() between runs also
     * clears the demodulator so no run inherits a previous lock.
     */
    P25_IO io;
    std::mt19937_64 rng(1U);
    std::vector<uint8_t> rxFrames;

    /* calibration pass: clean chain, capture the post-DC peak level */
    int16_t cleanPeak;
    {
        p25PacketBuffer.reset();
        p25LDUBuffer.reset();
        p25_rxBuffer.reset();
        p25_txBuffer.reset();

        io.reset();
        io.init_tx();
        io.setSrcId(1234567U);
        io.setDstId(1U);
        io.writeSyncFrames();
        pumpLoopback(io, 0.0, rng);
        io.createTxHeader();
        pumpLoopback(io, 0.0, rng);
        p25PacketBuffer.reset();
        io.createTxLDU1();
        pumpLoopback(io, 0.0, rng);
        io.createTxLDU2();
        pumpLoopback(io, 0.0, rng);

        /* io.update() (where m_bbPeak is tracked) never runs in the
           loopback, so measure the wire peak directly here */
        cleanPeak = g_txAbsPeak;
        std::printf("clean TX wire peak: %d\n", cleanPeak);
    }

    const double ratios[] = { 0.005, 0.01, 0.02, 0.04, 0.06, 0.08, 0.12 };
    const size_t nRatios = sizeof(ratios) / sizeof(ratios[0]);

    std::printf("%8s %10s %12s %14s\n", "k", "sigma", "rxLDU", "frames-ok");
    for (size_t r = 0U; r < nRatios; r++) {
        double sigma = ratios[r] * (double)cleanPeak;
        size_t bestFrames = 0U;
        uint16_t bestLdu = 0U;

        uint16_t lduAtStart = io.getRxLduCount();
        for (uint64_t seed = 1U; seed <= 3U; seed++) {
            rxFrames.clear();
            rng.seed(seed);

            p25PacketBuffer.reset();
            p25LDUBuffer.reset();
            p25_rxBuffer.reset();
            p25_txBuffer.reset();

            io.reset();
            io.init_tx();
            io.setSrcId(1234567U);
            io.setDstId(1U);
            io.writeSyncFrames();
            pumpLoopback(io, sigma, rng);
            io.createTxHeader();
            pumpLoopback(io, sigma, rng);

            size_t framePtr = 0U;
            while (framePtr < totalFrames) {
                p25PacketBuffer.reset();
                for (size_t i = 0U; i < FRAMES_PER_LDU_PAIR * FRAME_BYTES; i++)
                    p25PacketBuffer.put(txFrames[framePtr * FRAME_BYTES + i]);
                framePtr += FRAMES_PER_LDU_PAIR;

                io.createTxLDU1();
                pumpLoopback(io, sigma, rng);
                io.createTxLDU2();
                pumpLoopback(io, sigma, rng);
                pumpDecode(io, rxFrames);
            }

            p25PacketBuffer.reset();
            io.createTxLDU1();
            pumpLoopback(io, sigma, rng);
            io.createTxLDU2();
            pumpLoopback(io, sigma, rng);
            io.createTxTerminator();
            pumpLoopback(io, sigma, rng);
            pumpDecode(io, rxFrames);

            size_t run = longestRun(txFrames, rxFrames);
            uint16_t runLdu = io.getRxLduCount();
            /* per-run delta: io.reset() does not clear the LDU counter */
            if (run > bestFrames) {
                bestFrames = run;
                bestLdu = runLdu;
            }
            if (runLdu < lduAtStart)
                lduAtStart = runLdu;
        }

        std::printf("%8.2f %10.0f %12u %14zu/%zu\n", ratios[r], sigma, bestLdu,
                    bestFrames, totalFrames);
    }

    /*
     * Skip static teardown: the global p25Mode instance's exit-time
     * destructors interact badly with a manually constructed P25_IO
     * (double-free).  Results are printed above; nothing to flush.
     */
    std::fflush(stdout);
    std::_Exit(0);
}
