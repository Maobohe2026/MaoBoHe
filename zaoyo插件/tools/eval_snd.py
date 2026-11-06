import os
import re
import sys

BASE = r'E:\Documents\deepseek-harness\default-workspace\qmfindpic-single\build\snd_mix'

EVENTS = [
    (4.7, 'dodge'), (8.7, 'dodge'), (10.7, 'counter'), (18.8, 'dodge_weak'),
    (22.2, 'dodge'), (25.6, 'dodge'), (26.7, 'dodge'), (30.7, 'dodge'),
    (32.6, 'dodge'), (33.8, 'dodge'), (36.2, 'counter'),
]


def load_frames(path):
    ts, sc = [], []
    with open(path, 'r', errors='ignore') as f:
        for line in f:
            m = re.match(r'\s*([\d.]+)\s+([\d.]+)', line)
            if m:
                ts.append(float(m.group(1)))
                sc.append(float(m.group(2)))
    return ts, sc


def analyze(name, tpl_kind, thr):
    path = os.path.join(BASE, '%s_frames.txt' % tpl_kind)
    ts, sc = load_frames(path)
    if not ts:
        print('%s: 没解析到帧' % name)
        return
    print('\n===== %s （阈值 %.2f，共 %d 帧）=====' % (name, thr, len(ts)))
    ev_idx = set()
    rows = []
    for t, kind in EVENTS:
        if kind not in tpl_kind:
            continue
        lo, hi = t - 0.5, t + 1.0
        best, bestt = -1.0, -1.0
        for i, ft in enumerate(ts):
            if lo <= ft <= hi:
                ev_idx.add(i)
                if sc[i] > best:
                    best, bestt = sc[i], ft
        hit = '命中' if best >= thr else '漏检'
        rows.append((t, kind, best, bestt, hit))
        print('  %5.1fs %-10s 窗口内最高分 %.4f @ %.2fs  -> %s' % (t, kind, best, bestt, hit))
    # 非事件帧的分数分布
    nonev = [sc[i] for i in range(len(sc)) if i not in ev_idx]
    nonev_sorted = sorted(nonev)
    def pct(p):
        if not nonev_sorted:
            return 0.0
        k = min(len(nonev_sorted) - 1, int(p * len(nonev_sorted)))
        return nonev_sorted[k]
    print('  非事件帧: 中位数 %.4f  p95 %.4f  p99 %.4f  最大 %.4f'
          % (pct(0.5), pct(0.95), pct(0.99), nonev_sorted[-1] if nonev_sorted else 0))
    hits = sum(1 for r in rows if r[4] == '命中')
    print('  命中 %d/%d' % (hits, len(rows)))
    # 给出建议阈值：事件最低分与非事件最高分之间
    if rows:
        lo_ev = min(r[2] for r in rows)
        print('  事件最低分 %.4f ，非事件最大分 %.4f' % (lo_ev, nonev_sorted[-1] if nonev_sorted else 0))
    return rows


analyze('闪避模板', 'dodge', 0.35)
analyze('反击模板', 'counter', 0.25)
