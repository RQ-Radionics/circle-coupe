// Part of SimCoupe - A SAM Coupe emulator
//
// Audio.cpp: SDL sound implementation
//
//  Copyright (c) 1999-2015 Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include "SimCoupe.h"

#include "Audio.h"
#include "Options.h"
#include "Sound.h"

constexpr auto MIN_LATENCY_FRAMES = 4;

static SDL_AudioStream* stream;

////////////////////////////////////////////////////////////////////////////////

bool Audio::Init()
{
    Exit();

    SDL_AudioSpec desired{};
    desired.freq = SAMPLE_FREQ;
    desired.format = SDL_AUDIO_S16LE;
    desired.channels = SAMPLE_CHANNELS;

    stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &desired,
        nullptr,    // no callback — push data manually
        nullptr);

    if (!stream)
    {
        TRACE("SDL_OpenAudioDeviceStream failed: {}\n", SDL_GetError());
        return false;
    }

    SDL_ResumeAudioStreamDevice(stream);
    return true;
}

void Audio::Exit()
{
    if (stream)
    {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
}

float Audio::AddData(uint8_t* pData_, int len_bytes)
{
    SDL_PutAudioStreamData(stream, pData_, len_bytes);

    auto buffer_frames = std::max(GetOption(latency), MIN_LATENCY_FRAMES);
    int buffer_size = SAMPLES_PER_FRAME * buffer_frames * BYTES_PER_SAMPLE;

#ifndef __circle__
    // On bare-metal the audio stream may never drain if the audio callback
    // is not firing. Skip this blocking wait entirely.
    while (SDL_GetAudioStreamQueued(stream) >= buffer_size)
    {
        SDL_Delay(1);
    }
#endif

    return static_cast<float>(SDL_GetAudioStreamQueued(stream)) / buffer_size;
}
