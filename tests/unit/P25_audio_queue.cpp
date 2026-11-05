/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <catch2/catch_test_macros.hpp>
#include <SDL2/SDL.h>
#include <chrono>
#include <thread>
#include "protocols/P25/modem/AudioCodec.hpp"

using P25::AudioCodec;

/**
 * Wait for the codec queue of a path to empty.
 *
 * @param path: audio path of the decoding operation.
 * @return true if the queue emptied within two seconds.
 */
static bool waitQueueEmpty(const pathId path)
{
    for (int i = 0; i < 2000; i++) {
        if (AudioCodec::queueEmpty(path))
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

TEST_CASE("P25 codec queue empties as the decoded frames play", "[p25][codec]")
{
    // The speaker is an SDL stream: the dummy driver plays it in real time
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    REQUIRE(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0);

    AudioCodec::init();
    pathId path = audioPath_request(SOURCE_MCU, SINK_SPK, PRIO_RX);
    REQUIRE(path > 0);

    // Empty before anything runs, and for a path that is not ours
    REQUIRE(AudioCodec::queueEmpty(path));
    REQUIRE(AudioCodec::queueEmpty(path + 1));

    REQUIRE(AudioCodec::startDecode(path));

    // Push past the queue size, so the decoder is paced by playback.
    // Frames are the standard encoded-silence pattern.
    const uint8_t frame[AudioCodec::FRAME_BYTES] = { 0x04, 0x0C, 0xFD, 0x7B,
                                                     0xFB, 0x7D, 0xF2, 0x7B,
                                                     0x3D, 0x9E, 0x45 };
    for (int i = 0; i < AudioCodec::QUEUE_DEPTH; i++)
        REQUIRE(AudioCodec::pushFrame(frame, false) == 0);

    REQUIRE_FALSE(AudioCodec::queueEmpty(path));
    REQUIRE(waitQueueEmpty(path));

    // Stopped codec reports an empty queue again
    AudioCodec::stop(path);
    REQUIRE(AudioCodec::queueEmpty(path));

    audioPath_release(path);
    AudioCodec::terminate();
}
