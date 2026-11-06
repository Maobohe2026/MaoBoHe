# eval_modes.py —— 在真实战斗录音上比较不同打分方式的命中/误报
import os
import re
import sys

BASE = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\build\snd_mix'

DODGE = [4.7, 8.7, 18.8, 22.2, 25.6, 26.7, 30.7, 32.6, 33.8]
COUNTER = [10.7, 36.2]


def load(path):
    ts, sc = [], []
    for line in open(path, 'r', errors='ignore'):
        m = re.match(r'\s*([\d.]+)\s+(-?[\d.]+)', line)
        if m:
            ts.append(float(m.group(1)))
            sc.append(float(m.group(2)))
    return ts, sc


def evaluate(path, events, pre=0.4, post=0.9, thresh=None):
    """返回 (每个事件的窗口最高分, 非事件分数列表, 峰值时间)"""
    ts, sc = load(path)
    mask = [False] * len(ts)
    ev_best = []
    for t in events:
        best, bt = -1.0, -1.0
        for i, ft in enumerate(ts):
            if t - pre <= ft <= t + post:
                mask[i] = True
                if sc[i] > best:
                    best, bt = sc[i], ft
        ev_best.append((t, best, bt))
    nonev = sorted(sc[i] for i in range(len(sc)) if not mask[i])
    return ev_best, nonev


def report(tag, path, events):
    ev, nonev = evaluate(path, events)

    def pct(p):
        if not nonev:
            return 0.0
        return nonev[min(len(nonev) - 1, int(p * len(nonev)))]

    lo_ev = min(b for _, b, _ in ev)
    seps = '事件最低 %.4f | 非事件 p99 %.4f max %.4f' % (lo_ev, pct(0.99), nonev[-1])
    # 用"事件最低分"和"非事件最高分"的中点当阈值，看能命中几个、误报几帧
    thr_mid = (lo_ev + nonev[-1]) / 2.0
    hits = sum(1 for _, b, _ in ev if b >= thr_mid)
    fp = sum(1 for v in nonev if v >= thr_mid)
    print('%-28s %s' % (tag, seps))
    print('%-28s 中点阈值 %.4f → 命中 %d/%d，非事件误报帧 %d/%d'
          % ('', thr_mid, hits, len(ev), fp, len(nonev)))
    detail = '  '.join('%.1fs:%.3f' % (t, b) for t, b, _ in ev)
    print('%-28s 各事件: %s' % ('', detail))
    return thr_mid, hits, len(ev), fp


print('=== 闪避模板（事件 9 个）===')
for m in (1, 2, 3):
    p = os.path.join(BASE, 'v_dodge_m%d.txt' % m)
    if os.path.exists(p):
        report('mode%d (窗=模板长)' % m, p, DODGE)

print('\n=== 现行算法（sndtest.exe，窗=max(模板,0.5s)）===')
p = os.path.join(BASE, 'video_dodge.txt')
if os.path.exists(p):
    report('mode0 (现状)', p, DODGE)

print('\n=== 反击模板（事件 2 个，仅供参考）===')
p = os.path.join(BASE, 'video_counter.txt')
if os.path.exists(p):
    report('mode0 (现状)', p, COUNTER)
