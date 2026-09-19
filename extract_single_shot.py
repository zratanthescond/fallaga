"""
Extract a single gunshot from an MP40 burst sound file.
Approach: The burst is ~960ms long. We'll try to isolate a single shot by
taking the first portion up to where the sound naturally decays before the 
next shot, or just take the first ~200ms which captures one full shot 
(attack + sustain + natural decay tail).
"""
import miniaudio
import array
import wave
import os

INPUT_FILE = "assets/guns/mp40/mp40burst.mp3"
OUTPUT_DIR = "assets/guns/mp40"
OUTPUT_SINGLE = os.path.join(OUTPUT_DIR, "mp40_single_shot.wav")

# 1. Decode the full MP3 to raw PCM
print(f"[1] Decoding {INPUT_FILE}...")
decoded = miniaudio.decode_file(INPUT_FILE, output_format=miniaudio.SampleFormat.SIGNED16)
sample_rate = decoded.sample_rate
num_channels = decoded.nchannels
samples = array.array('h', decoded.samples)

duration_ms = len(samples) / num_channels / sample_rate * 1000
print(f"    Sample rate: {sample_rate} Hz")
print(f"    Channels: {num_channels}")
print(f"    Duration: {duration_ms:.1f} ms")

# 2. Convert to mono for analysis
if num_channels == 2:
    mono = []
    for i in range(0, len(samples), 2):
        mono.append((samples[i] + samples[i+1]) // 2)
else:
    mono = list(samples)

# 3. Find the first significant amplitude (attack onset)
# Then compute RMS in larger windows to find the inter-shot gaps

window_ms = 5
window_samples = int(sample_rate * window_ms / 1000)

# Compute RMS envelope
envelope = []
for i in range(0, len(mono) - window_samples, window_samples):
    rms = sum(s*s for s in mono[i:i+window_samples]) / window_samples
    rms = rms ** 0.5
    envelope.append((i, rms))

max_rms = max(e[1] for e in envelope)
print(f"\n[2] Envelope analysis (window={window_ms}ms):")
print(f"    Max RMS: {max_rms:.1f}")
print(f"    Envelope samples: {len(envelope)}")

# Print the full envelope for inspection
print(f"\n    Time(ms)  RMS        Bar")
for i, (sample_idx, rms) in enumerate(envelope):
    t = sample_idx / sample_rate * 1000
    bar = '#' * int(rms / max_rms * 50)
    print(f"    {t:7.0f}   {rms:8.1f}   {bar}")

# 4. Strategy: The MP40 fires at ~500 RPM = one shot every ~120ms.
#    Take the first ~150ms of the burst which should contain one full shot
#    with its natural decay/reverb tail. Apply a smooth fade-out at the end.

# Find the onset (first window with > 10% of max)
onset_idx = 0
for i, (sample_idx, rms) in enumerate(envelope):
    if rms > max_rms * 0.10:
        onset_idx = sample_idx
        break

# Start 2ms before onset
start_sample = max(0, onset_idx - int(0.002 * sample_rate))

# The MP40 fires at ~500 RPM. Given a ~960ms burst with multiple shots,
# let's estimate the number of shots from the burst length.
# If 4-5 shots in 960ms: ~200-240ms per shot cycle
# Take 180ms for a single shot (enough for the transient + decay, cuts before next shot)
shot_duration_ms = 180
end_sample = start_sample + int(shot_duration_ms * sample_rate / 1000)
if end_sample > len(mono):
    end_sample = len(mono)

actual_duration = (end_sample - start_sample) / sample_rate * 1000
print(f"\n[3] Extracting single shot:")
print(f"    Onset at: {onset_idx / sample_rate * 1000:.0f} ms")
print(f"    Start: {start_sample / sample_rate * 1000:.0f} ms")
print(f"    End: {end_sample / sample_rate * 1000:.0f} ms")
print(f"    Duration: {actual_duration:.0f} ms")

# 5. Extract and apply fade-out (last 30ms)
fade_out_ms = 30
fade_out_samples = int(fade_out_ms * sample_rate / 1000)
fade_start_in_shot = (end_sample - start_sample) - fade_out_samples

if num_channels == 2:
    shot_data = array.array('h', samples[start_sample*2:end_sample*2])
    # Apply fade-out
    for i in range(fade_out_samples):
        fade = 1.0 - (i / fade_out_samples)
        fade = fade * fade  # Quadratic fade for smoother tail
        idx_l = (fade_start_in_shot + i) * 2
        idx_r = idx_l + 1
        if 0 <= idx_l < len(shot_data) and 0 <= idx_r < len(shot_data):
            shot_data[idx_l] = int(shot_data[idx_l] * fade)
            shot_data[idx_r] = int(shot_data[idx_r] * fade)
else:
    shot_data = array.array('h', mono[start_sample:end_sample])
    for i in range(fade_out_samples):
        fade = 1.0 - (i / fade_out_samples)
        fade = fade * fade
        idx = fade_start_in_shot + i
        if 0 <= idx < len(shot_data):
            shot_data[idx] = int(shot_data[idx] * fade)

# 6. Write WAV
print(f"\n[4] Writing {OUTPUT_SINGLE}...")
with wave.open(OUTPUT_SINGLE, 'w') as wf:
    wf.setnchannels(num_channels)
    wf.setsampwidth(2)
    wf.setframerate(sample_rate)
    wf.writeframes(shot_data.tobytes())

output_size = os.path.getsize(OUTPUT_SINGLE)
print(f"    Output size: {output_size} bytes")
print(f"    Done!")
