#!/usr/bin/env python3

"""Generates the in-call notification tones that NMPBX registers with the SDK.

Why these files exist
---------------------
The SDK synthesises its telephony tones, and it builds every one of them at full
amplitude: ToneManager::generateToneFromId sets `def.amplitude = 1` and the call-waiting
and call-on-hold cases never lower it. The generator then renders at
`amplitude * 0.7 * 32767`, so the beep peaks around -3 dBFS, which is startling to hear
part-way through a call. `[sound] dtmf_player_amp` does not help: it is only read when a
standalone ring stream is created, and a custom tone's own amplitude overrides it anyway.

Registering a file with Core::setTone diverts the tone to ToneManager::playFile instead of
the synthesiser, so the level becomes whatever we record here. Nothing on that path applies
any gain, so the level has to be baked in.

Three constraints from the SDK shape the output, so do not change these casually:

* Only 16-bit mono PCM WAV is safe. The tone path always uses MSFilePlayer, and its
  `read_wav_header` never inspects the format tag, treating the data chunk as little-endian
  signed 16-bit PCM whatever the header claims. A mu-law, 24-bit or float file plays as
  noise rather than failing, and a header it cannot parse is played as headerless raw audio.
* The repetition has to be baked in. `playFile` forces `MS_PLAYER_SET_LOOP` to -1, which
  means play once, whereas the synthesised call-waiting tone repeats indefinitely. So the
  beeps are laid out in the file itself, and the beeping simply ends when the file does.
  Playback is cut off cleanly the moment the call is answered or the caller gives up.
* The gap is the silence *after* each beep, not the period. The SDK's `interval` counts down
  separately from the beep, so its 300 ms beep with a 2000 ms interval is a 2300 ms period.

The level matches the SDK's own ringback.wav, which peaks at -13.9 dBFS, so our tones sit
consistently with the other audio the app plays.

Output is byte-for-byte deterministic, so re-running this without changing the constants
below leaves the committed files untouched. Regenerate and commit only when the spec changes.

Usage: python generate-tones.py
"""

import math
import struct
import wave
from pathlib import Path

# Matches the SDK's own tone definitions, so the cadence is unchanged and only the level
# differs. See ToneManager::generateToneFromId.
FREQUENCY_HZ = 440
BEEP_MS = 300
GAP_MS = 2000

# 8 kHz mono is the smallest sensible format for a 440 Hz tone, which sits far below the
# 4 kHz Nyquist limit, and it is what ringback.wav uses. The call's audio graph resamples to
# whatever rate the call runs at, so there is nothing to gain from a higher rate here.
SAMPLE_RATE = 8000
CHANNELS = 1
SAMPLE_WIDTH = 2

# Peak level, chosen to match ringback.wav (measured peak -13.9 dBFS). This is the one number
# to change if the tones turn out too loud or too quiet in practice.
PEAK_DBFS = -14.0

# The synthesised tone starts and stops mid-cycle, which clicks. A few milliseconds of
# raised-cosine fade removes that, and stops the resampler ringing on the discontinuity.
FADE_MS = 5

# Beep counts mirror the SDK's repeat_count for each tone: call-waiting repeats indefinitely
# (repeat_count 0), so we pick a length that covers a realistic wait, while call-on-hold is
# defined as exactly three beeps.
TONES = {
    "nmpbx-call-waiting.wav": 15,
    "nmpbx-call-on-hold.wav": 3,
}


def ms_to_samples(milliseconds):
    return int(round(SAMPLE_RATE * milliseconds / 1000.0))


def build_beep():
    """One faded sine beep, as a list of signed 16-bit sample values."""
    length = ms_to_samples(BEEP_MS)
    fade = min(ms_to_samples(FADE_MS), length // 2)
    peak = 10.0 ** (PEAK_DBFS / 20.0)
    samples = []
    for i in range(length):
        value = math.sin(2.0 * math.pi * FREQUENCY_HZ * i / SAMPLE_RATE)
        if fade > 0:
            if i < fade:
                value *= 0.5 * (1.0 - math.cos(math.pi * i / fade))
            elif i >= length - fade:
                remaining = length - 1 - i
                value *= 0.5 * (1.0 - math.cos(math.pi * remaining / fade))
        samples.append(int(round(value * peak * 32767.0)))
    return samples


def build_tone(beep_count):
    """The full tone: beeps separated by silence, ending on the last beep."""
    beep = build_beep()
    gap = [0] * ms_to_samples(GAP_MS)
    samples = []
    for index in range(beep_count):
        if index > 0:
            samples.extend(gap)
        samples.extend(beep)
    return samples


def write_wav(path, samples):
    # Packed explicitly little-endian rather than through array(), whose byte order follows
    # the host, so the committed files are identical whoever generates them.
    frames = struct.pack("<%dh" % len(samples), *samples)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(CHANNELS)
        handle.setsampwidth(SAMPLE_WIDTH)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(frames)


def main():
    output_dir = Path(__file__).resolve().parent
    for filename, beep_count in TONES.items():
        samples = build_tone(beep_count)
        path = output_dir / filename
        write_wav(path, samples)
        peak = max(abs(sample) for sample in samples)
        print(
            "%s: %d beeps, %.1f s, %d bytes, peak %d (%.2f dBFS)"
            % (
                filename,
                beep_count,
                len(samples) / float(SAMPLE_RATE),
                path.stat().st_size,
                peak,
                20.0 * math.log10(peak / 32768.0),
            )
        )


if __name__ == "__main__":
    main()
