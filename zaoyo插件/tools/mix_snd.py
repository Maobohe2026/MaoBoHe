import os
import wave
import numpy as np

BASE = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-native\build'
OUT = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\build\snd_mix'
os.makedirs(OUT, exist_ok=True)


def read_wav(path):
    with wave.open(path, 'rb') as w:
        nch = w.getnchannels()
        sw = w.getsampwidth()
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    if sw != 2:
        raise SystemExit('only 16-bit supported: %s' % path)
    a = np.frombuffer(raw, dtype='<i2').astype(np.float32) / 32768.0
    if nch > 1:
        a = a.reshape(-1, nch).mean(axis=1)
    return a, sr, nch


def write_wav(path, data, sr, nch=1):
    d = np.clip(data, -1.0, 1.0)
    pcm = (d * 32767.0).astype('<i2')
    if nch > 1:
        pcm = np.repeat(pcm, nch)
    with wave.open(path, 'wb') as w:
        w.setnchannels(nch)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


noise, sr1, _ = read_wav(os.path.join(BASE, 'snd_noise.wav'))
needle, sr2, nch2 = read_wav(os.path.join(BASE, 'snd_needle.wav'))
print('noise', len(noise) / sr1, 's', 'needle', len(needle) / sr2, 's', 'sr', sr1, sr2)
n = min(len(noise), len(needle))
noise = noise[:n]
needle = needle[:n]

rms_noise = float(np.sqrt(np.mean(noise ** 2)) + 1e-9)
rms_needle = float(np.sqrt(np.mean(needle ** 2)) + 1e-9)
print('rms_noise %.4f rms_needle %.4f' % (rms_noise, rms_needle))

# needle 相对 noise 的 SNR（大致）
snr0 = 20 * np.log10(rms_needle / rms_noise)
print('baseline SNR of the recording: %.1f dB' % snr0)

# 逐档压低 needle、抬高 noise，模拟"战斗里其它音效很多"
for delta in (0, -6, -12, -18, -24, -30):
    g = 10 ** (delta / 20.0)
    mix = noise + needle * g
    # 归一化避免削顶
    peak = float(np.max(np.abs(mix))) + 1e-9
    if peak > 0.99:
        mix = mix / peak * 0.99
    out = os.path.join(OUT, 'mix_%+03ddB.wav' % delta)
    write_wav(out, mix, sr1, 1)
    print('wrote', os.path.basename(out), 'SNR=%ddB' % delta)
