"""Makes the fireworks sounds of the game from a recording of a real fireworks show (Fireworks.mp3 -> WAV with
tools/mp3_to_wav.cpp; the times of the whistle and of the bangs were found with tools/fwanalyze.cpp):
  swist1..5.wav   the rocket's whistle, the one clean whistle of the recording at 5 speeds (different pitch and length)
  wybuch1..5.wav  five bangs, cut with a short fade in and a long fade out
All are mono 16-bit 22.05 kHz (small). Usage:  python tools/fwsounds.py build/ref/fireworks_full.wav res/sounds"""
import wave, array, math, sys, os

src, outdir = sys.argv[1], sys.argv[2]
w = wave.open(src)
n = w.getnframes(); sr = w.getframerate(); ch = w.getnchannels()
raw = array.array('h'); raw.frombytes(w.readframes(n))
mono = [sum(raw[i*ch + c] for c in range(ch)) / ch / 32768.0 for i in range(n)]
OUT_SR = 22050

def write(path, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    data = array.array('h', [int(max(-1, min(1, s)) * 32767) for s in samples])
    f = wave.open(path, 'wb'); f.setnchannels(1); f.setsampwidth(2); f.setframerate(OUT_SR); f.writeframes(data.tobytes()); f.close()

def resample(seg, speed):
    """plays seg `speed` times faster (pitch changes with it), to OUT_SR, with a small low-pass"""
    out = []; step = sr / OUT_SR * speed; pos = 0.0; L = len(seg)
    while pos + 2 < L:
        i = int(pos)
        out.append(0.25 * seg[i] + 0.5 * seg[i+1] + 0.25 * seg[i+2])
        pos += step
    return out

def shape(samples, peak, fade_in, fade_out):
    m = max(1e-9, max(abs(s) for s in samples))
    g = peak / m; N = len(samples); fi = int(fade_in * OUT_SR); fo = int(fade_out * OUT_SR)
    out = []
    for i, s in enumerate(samples):
        a = 1.0
        if i < fi: a = 0.5 - 0.5 * math.cos(math.pi * i / fi)
        if i > N - fo: a = min(a, 0.5 + 0.5 * math.cos(math.pi * (i - (N - fo)) / fo))
        out.append(s * g * a)
    return out

# ---- the whistle: 0.00 .. 0.86 s of the recording (a tone from 5.6 kHz sliding down, up to the first bang at 0.85 s)
whistle = mono[0:int(0.86 * sr)]
for k, speed in enumerate([1.0, 0.9, 0.8, 0.7, 0.62], 1):
    clip = shape(resample(whistle, speed), 0.55, 0.01, 0.07)
    write(os.path.join(outdir, 'swist%d.wav' % k), clip)
    print('swist%d: %.2f s (speed %.2f)' % (k, len(clip) / OUT_SR, speed))

# ---- the bangs: (time of the onset, length)
for k, (t, length) in enumerate([(3.46, 2.0), (10.89, 2.0), (14.27, 1.25), (20.39, 1.35), (22.93, 2.0)], 1):
    a = int((t - 0.06) * sr); b = int((t + 0.12) * sr)
    pk = max(abs(x) for x in mono[a:b])
    on = next(i for i in range(a, b) if abs(mono[i]) > 0.3 * pk)               # the real start of the bang
    start = max(0, on - int(0.012 * sr)); end = min(n, start + int(length * sr))
    clip = shape(resample(mono[start:end], 1.0), 0.92, 0.003, 0.35)
    write(os.path.join(outdir, 'wybuch%d.wav' % k), clip)
    print('wybuch%d: %.2f s (from %.2f s of the recording)' % (k, len(clip) / OUT_SR, start / sr))
