#!/usr/bin/env python3
"""Legion Classic - the sound effects and ambience, made from scratch.

Every sound here is synthesised: shaped noise, swept tones and inharmonic
partials, with outdoor echoes baked in (the game adds a large hall on top).
Nothing is sampled, so all of it is ours (CC0, see content/audio/LICENSES.md).

    python3 tools/audio/sfx.py content/audio

needs numpy, scipy and oggenc (vorbis-tools). Deterministic: the same seed
makes the same files.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
rng = np.random.default_rng(1944)


# ---------------------------------------------------------------------------
# Building blocks
# ---------------------------------------------------------------------------
def n_(sec):
    return max(1, int(sec * SR))


def t_(sec):
    return np.arange(n_(sec)) / SR


def noise(sec):
    return rng.standard_normal(n_(sec))


def pink(sec):
    x = noise(sec)
    # Voss-ish: filter white noise with a -3 dB/octave approximation.
    b = [0.049922035, -0.095993537, 0.050612699, -0.004408786]
    a = [1, -2.494956002, 2.017265875, -0.522189400]
    return signal.lfilter(b, a, x) * 4


def brown(sec):
    x = np.cumsum(noise(sec))
    x = signal.sosfilt(signal.butter(1, 15, 'hp', fs=SR, output='sos'), x)
    return x / (np.max(np.abs(x)) + 1e-9)


def lp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, min(f, SR * 0.45), 'lp', fs=SR, output='sos'), x)


def hp(x, f, order=2):
    return signal.sosfilt(signal.butter(order, f, 'hp', fs=SR, output='sos'), x)


def bp(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, min(hi, SR * 0.45)], 'bp', fs=SR, output='sos'), x)


def expenv(sec, tau, attack=0.0005):
    t = t_(sec)
    e = np.exp(-t / tau)
    a = n_(attack)
    if a > 1:
        e[:a] *= np.linspace(0, 1, a)
    return e


def sweep(sec, f0, f1, curve='exp'):
    t = t_(sec)
    if curve == 'exp':
        f = f0 * (f1 / f0) ** (t / sec)
    else:
        f = f0 + (f1 - f0) * (t / sec)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def pad(x, sec):
    n = n_(sec)
    if len(x) >= n:
        return x[:n]
    return np.concatenate([x, np.zeros(n - len(x))])


def mix(*parts):
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def at(x, sec, total=None):
    """x delayed by sec."""
    out = np.concatenate([np.zeros(n_(sec)), x])
    return pad(out, total) if total else out


def norm(x, peak=0.89):
    m = np.max(np.abs(x)) + 1e-9
    return x * (peak / m)


def fade(x, fin=0.0, fout=0.02):
    x = x.copy()
    a, b = n_(fin), n_(fout)
    if fin > 0:
        x[:a] *= np.linspace(0, 1, a)
    if fout > 0 and b < len(x):
        x[-b:] *= np.linspace(1, 0, b) ** 2
    return x


def outdoors(x, size=1.0, wet=0.35, darkness=2500):
    """Echoes off woods and hills: a few slapbacks and a dark diffuse tail."""
    total = len(x) / SR + 0.35 * size + 1.2 * size
    out = pad(x, total)
    for delay, gain in ((0.11, 0.35), (0.19, 0.25), (0.31, 0.18), (0.47, 0.12)):
        d = delay * size * rng.uniform(0.85, 1.15)
        out += at(lp(x, darkness * 0.8), d, total) * gain * wet * 2
    tail = noise(1.2 * size) * expenv(1.2 * size, 0.35 * size, attack=0.03)
    tail = lp(tail, darkness * 0.6)
    conv = signal.fftconvolve(x, tail)[:n_(total)]
    out += pad(conv, total) * wet * 0.02
    return out


def crack(sec=0.006, f=2500, amp=1.0):
    return hp(noise(sec), f) * expenv(sec, sec / 3) * amp


def body(sec, lo, hi, tau, amp=1.0):
    return bp(noise(sec), lo, hi) * expenv(sec, tau) * amp


def thump(sec, f0, f1, tau, amp=1.0):
    return sweep(sec, f0, f1) * expenv(sec, tau, attack=0.002) * amp


def rumble(sec, f, tau, amp=1.0, attack=0.01):
    return lp(brown(sec), f, 4) * expenv(sec, tau, attack) * amp


def debris(sec, count, start=0.05, f=(1500, 6000), amp=0.2):
    out = np.zeros(n_(sec))
    for _ in range(count):
        p = rng.uniform(start, sec * 0.8)
        g = rng.uniform(0.2, 1.0) * amp * np.exp(-p / (sec * 0.5))
        lo = rng.uniform(*f) * 0.5
        c = bp(noise(0.02), lo, lo * 3) * expenv(0.02, 0.004)
        i = n_(p)
        out[i:i + len(c)] += c[:max(0, len(out) - i)] * g
    return out


# ---------------------------------------------------------------------------
# Weapons
# ---------------------------------------------------------------------------
def rifle(v):
    s = mix(crack(0.005, 2200, 1.0),
            body(0.06, 250, 3500, 0.012, 0.9),
            thump(0.09, 160 + v * 10, 55, 0.025, 0.8))
    return norm(outdoors(s, size=1.1 + 0.1 * v, wet=0.5, darkness=2600))


def sniper(v):
    s = mix(crack(0.004, 3000, 1.2),
            body(0.08, 300, 4000, 0.015, 1.0),
            thump(0.12, 140, 48, 0.03, 0.9))
    return norm(outdoors(s, size=1.6 + 0.1 * v, wet=0.65, darkness=2200))


def mg_round(heavy):
    return mix(crack(0.004, 2400, 0.9),
               body(0.04, 200 if heavy else 260, 3000, 0.009, 0.9),
               thump(0.06, 130 if heavy else 170, 60, 0.018, 0.7 if heavy else 0.5))


def mg_burst(v, rounds, spacing, heavy=False):
    total = rounds * spacing + 0.1
    s = np.zeros(n_(total))
    for i in range(rounds):
        r = mg_round(heavy) * rng.uniform(0.8, 1.0)
        start = n_(i * spacing * rng.uniform(0.97, 1.03))
        s[start:start + len(r)] += r[:len(s) - start]
    return norm(outdoors(s, size=1.0, wet=0.45, darkness=2400))


def cannon(size, v):
    """size 0 small (37-57 mm) .. 2 large (88 mm and up)."""
    sub = (95, 70, 52)[size]
    sec = (0.5, 0.9, 1.3)[size]
    s = mix(crack(0.006, 1800, 1.0),
            body(0.25, 120, 2400, (0.03, 0.05, 0.07)[size], 1.0),
            thump(sec, sub * 2.2, sub * 0.8, (0.06, 0.1, 0.14)[size], 1.3),
            rumble(sec * 1.5, 220, (0.12, 0.2, 0.3)[size], 0.6))
    return norm(outdoors(s, size=(1.4, 1.9, 2.4)[size] + 0.1 * v, wet=0.6, darkness=(2000, 1600, 1300)[size]))


def howitzer(v):
    s = mix(crack(0.008, 1500, 0.8),
            body(0.4, 80, 1500, 0.09, 1.0),
            thump(1.6, 110, 34, 0.2, 1.5),
            rumble(2.5, 160, 0.55, 0.8, attack=0.02))
    return norm(outdoors(s, size=2.8 + 0.2 * v, wet=0.7, darkness=1100))


def rocket(v):
    sec = 1.4
    whoosh = bp(noise(sec), 400, 4000) * np.interp(t_(sec), [0, 0.05, 0.4, sec], [0, 1, 0.6, 0])
    roar = lp(noise(sec), 900) * np.interp(t_(sec), [0, 0.04, 0.3, sec], [0, 1, 0.4, 0]) * 1.2
    pop = mix(crack(0.008, 1200, 0.7), thump(0.2, 120, 50, 0.04, 0.8))
    # Pitch falls as it leaves (Doppler).
    s = mix(pop, whoosh * 0.7, roar)
    s = signal.resample(s, int(len(s) * (1.0 + 0.08 * v)))
    return norm(outdoors(s, size=1.6, wet=0.45, darkness=2200))


def at_launcher(v):
    pop = mix(crack(0.006, 1500, 0.8), thump(0.15, 140, 60, 0.03, 0.9))
    sec = 0.6
    whoosh = bp(noise(sec), 500, 5000) * np.interp(t_(sec), [0, 0.02, 0.15, sec], [0, 1, 0.5, 0])
    return norm(outdoors(mix(pop, at(whoosh * 0.8, 0.01)), size=1.2, wet=0.45))


def at_rifle(v):
    s = mix(crack(0.006, 1800, 1.2), body(0.1, 180, 3000, 0.02, 1.0), thump(0.2, 120, 45, 0.05, 1.1))
    return norm(outdoors(s, size=1.7, wet=0.6, darkness=2000))


# ---------------------------------------------------------------------------
# Impacts and destruction
# ---------------------------------------------------------------------------
def explosion(size, v):
    sec = (1.0, 1.8, 3.0)[size]
    s = mix(crack(0.01, 1000, 0.8),
            body(0.3, 60, 3000, (0.04, 0.07, 0.1)[size], 1.0),
            thump(sec, (90, 70, 50)[size], (40, 32, 26)[size], (0.12, 0.22, 0.35)[size], 1.6),
            rumble(sec, (500, 350, 250)[size], (0.18, 0.35, 0.7)[size], 1.0),
            debris(sec, (25, 50, 90)[size], 0.08, amp=(0.25, 0.3, 0.35)[size]))
    return norm(outdoors(s, size=(1.6, 2.2, 3.0)[size] + 0.15 * v, wet=0.55, darkness=(1800, 1400, 1100)[size]))


def armour_hit(v):
    sec = 1.2
    parts = (520, 1370, 2210, 3420, 4810)
    ring = sum(np.sin(2 * np.pi * f * (1 + 0.02 * v) * t_(sec) + rng.uniform(0, 6)) * expenv(sec, 0.25 / (1 + k * 0.6))
               * (0.6 / (1 + k)) for k, f in enumerate(parts))
    s = mix(crack(0.004, 2000, 1.0), thump(0.2, 180, 70, 0.03, 0.9), ring * 0.5)
    return norm(outdoors(s, size=1.0, wet=0.35, darkness=3500))


def ricochet(v):
    sec = 0.45
    s = sweep(sec, 2600 + v * 300, 900) * expenv(sec, 0.12, 0.003) * 0.6
    s = mix(crack(0.003, 3000, 0.5), s)
    return norm(outdoors(s, size=0.8, wet=0.3, darkness=4000), 0.6)


def vehicle_destroyed(v):
    base = explosion(2, v)
    pops = np.zeros(n_(3.5))
    for _ in range(14):  # ammunition cooking off
        p = rng.uniform(0.6, 3.0)
        c = mix(crack(0.004, 2000, 0.6), thump(0.08, 200, 80, 0.02, 0.4)) * rng.uniform(0.3, 0.8)
        i = n_(p)
        pops[i:i + len(c)] += c[:len(pops) - i]
    s = mix(base, outdoors(pops, 1.2, 0.4) * 0.6, debris(3.5, 40, 0.1, (2000, 7000), 0.3))
    return norm(s)


def collapse(v):
    sec = 3.5
    crumble = np.zeros(n_(sec))
    for _ in range(400):
        p = min(rng.uniform(0.0, sec * 0.85) ** 1.2, sec * 0.9)
        g = rng.uniform(0.1, 1.0) * np.exp(-p / 1.5)
        lo = rng.uniform(150, 1800)
        c = bp(noise(0.05), lo, lo * 2.5) * expenv(0.05, rng.uniform(0.005, 0.02))
        i = n_(p)
        crumble[i:i + len(c)] += c[:len(crumble) - i] * g
    s = mix(rumble(sec, 180, 1.2, 1.5, 0.05), crumble * 0.8, thump(1.5, 70, 30, 0.3, 1.0))
    return norm(outdoors(s, size=2.0, wet=0.45, darkness=1500))


# ---------------------------------------------------------------------------
# Machines and men (loops)
# ---------------------------------------------------------------------------
def make_loop(x, xfade=0.5):
    """Crossfades the end into the start so it repeats without a seam."""
    n = n_(xfade)
    head, body_, tail = x[:n], x[n:-n], x[-n:]
    w = np.linspace(0, 1, n)
    return np.concatenate([tail * (1 - w) + head * w, body_])


def engine_tone(sec, rpm_hz, harmonics, wobble=0.04):
    t = t_(sec)
    f = rpm_hz * (1 + wobble * np.sin(2 * np.pi * 0.31 * t) + 0.02 * lp(noise(sec), 3))
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = np.zeros_like(t)
    for k, a in enumerate(harmonics, 1):
        s += a * np.sin(k * ph + rng.uniform(0, 6))
    # Cylinder firing: amplitude pulses at the firing rate.
    s *= 0.7 + 0.3 * np.maximum(0, np.sin(ph * 2)) ** 2
    return s


def tank_engine():
    sec = 6.5
    eng = engine_tone(sec, 28, [1.0, 0.8, 0.55, 0.4, 0.25, 0.15, 0.1])
    eng = lp(eng, 700, 2) + lp(noise(sec), 300) * 0.25
    # Tracks: link clatter at ~9 Hz, uneven, and squeal.
    clatter = np.zeros(n_(sec))
    p = 0.0
    while p < sec:
        g = rng.uniform(0.4, 1.0)
        c = bp(noise(0.03), 900, 4500) * expenv(0.03, 0.006) * g
        i = n_(p)
        clatter[i:i + len(c)] += c[:len(clatter) - i]
        p += rng.uniform(0.09, 0.13)
    squeal = bp(noise(sec), 2400, 3200) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.7 * t_(sec))) * 0.15
    s = mix(eng * 1.0, clatter * 0.55, squeal)
    return norm(make_loop(s), 0.8)


def truck_engine():
    sec = 6.5
    eng = engine_tone(sec, 42, [1.0, 0.6, 0.45, 0.3, 0.2, 0.12], wobble=0.05)
    eng = lp(eng, 1100, 2) + lp(noise(sec), 500) * 0.15
    rattle = bp(noise(sec), 1500, 3500) * 0.05
    return norm(make_loop(mix(eng, rattle)), 0.7)


def march():
    sec = 8.0
    s = np.zeros(n_(sec))
    # Forty men, out of step: boots on dirt and kit rattling.
    for man in range(40):
        p = rng.uniform(0, 0.6)
        step = rng.uniform(0.52, 0.6)
        g = rng.uniform(0.3, 1.0)
        while p < sec:
            c = mix(bp(noise(0.06), 120, 900) * expenv(0.06, 0.018) * 0.8,
                    bp(noise(0.05), 1500, 5000) * expenv(0.05, 0.008) * 0.25) * g
            i = n_(p)
            s[i:i + len(c)] += c[:len(s) - i]
            p += step * rng.uniform(0.97, 1.03)
    jingle = bp(noise(sec), 3000, 7000) * (0.5 + 0.5 * np.abs(np.sin(2 * np.pi * 1.8 * t_(sec)))) * 0.03
    return norm(make_loop(mix(s, jingle)), 0.6)


def fire_loop():
    sec = 8.0
    roar = lp(pink(sec), 600) * (0.7 + 0.3 * lp(noise(sec), 2) * 8) * 0.6
    crackle = np.zeros(n_(sec))
    for _ in range(600):
        p = rng.uniform(0, sec)
        c = hp(noise(0.006), 2000) * expenv(0.006, 0.0015) * rng.uniform(0.05, 1.0) ** 2
        i = n_(p)
        crackle[i:i + len(c)] += c[:len(crackle) - i]
    return norm(make_loop(mix(roar, crackle * 0.9)), 0.7)


def construction(v):
    sec = 2.6
    s = np.zeros(n_(sec))
    p = 0.05
    for k in range(6):  # hammer blows
        blow = mix(bp(noise(0.08), 200, 3500) * expenv(0.08, 0.012),
                   sum(np.sin(2 * np.pi * f * t_(0.3)) * expenv(0.3, 0.05) for f in (820, 1930, 3100)) * 0.25)
        i = n_(p)
        s[i:i + len(blow)] += blow[:len(s) - i] * rng.uniform(0.6, 1.0)
        p += rng.uniform(0.28, 0.4)
    saw_t = t_(1.0)
    saw = bp(noise(1.0), 1500, 6000) * (0.5 + 0.5 * np.sin(2 * np.pi * 3.2 * saw_t)) ** 2 * 0.4
    s += at(saw, 1.4, sec)[:len(s)] * expenv(sec, 5)[:len(s)]
    thunk = thump(0.4, 90, 40, 0.08, 1.0)
    s = mix(thunk, s)
    return norm(outdoors(s, 1.0, 0.3, 3000))


# ---------------------------------------------------------------------------
# Orders: whistles and the radio
# ---------------------------------------------------------------------------
def whistle(v):
    sec = 0.9 if v % 2 == 0 else 0.45
    t = t_(sec)
    pea = 1 + 0.08 * np.sign(np.sin(2 * np.pi * (24 + 4 * v) * t))  # the pea rattles the pitch
    f = (2850 + 120 * v) * pea
    tone = np.sin(2 * np.pi * np.cumsum(f) / SR)
    s = (tone + 0.3 * bp(noise(sec), 2500, 4000)) * np.interp(t, [0, 0.02, sec - 0.08, sec], [0, 1, 0.9, 0])
    if v % 2 == 1:  # two short blasts
        s = mix(s, at(s, 0.55))
    return norm(outdoors(s, 1.4, 0.4, 5000), 0.7)


def radio(v):
    """Squelch, a burst of carrier and a click: an order acknowledged."""
    sec = 0.55
    sq = bp(noise(0.12), 800, 3500) * expenv(0.12, 0.05) * 0.5
    carrier = bp(noise(0.25) * (0.6 + 0.4 * np.sin(2 * np.pi * 37 * t_(0.25))), 400, 2800)
    tone = np.sin(2 * np.pi * (1000 + 150 * v) * t_(0.08)) * 0.3
    click = hp(noise(0.004), 1000) * 0.8
    s = mix(sq, at(carrier * np.interp(t_(0.25), [0, 0.03, 0.2, 0.25], [0, 0.5, 0.4, 0]), 0.05),
            at(tone, 0.32), at(click, 0.45))
    s = bp(s, 300, 3400)
    return norm(pad(s, sec), 0.55)


def engine_rev(v):
    sec = 1.6
    t = t_(sec)
    f = 28 * np.interp(t, [0, 0.25, 0.9, sec], [1, 1.9 + 0.2 * v, 1.4, 1.1])
    ph = 2 * np.pi * np.cumsum(f) / SR
    s = sum(a * np.sin(k * ph) for k, a in enumerate([1, 0.8, 0.5, 0.35, 0.2], 1))
    s = lp(s * (0.7 + 0.3 * np.maximum(0, np.sin(2 * ph)) ** 2), 900) * np.interp(t, [0, 0.1, 1.3, sec], [0, 1, 0.8, 0])
    s = mix(s, lp(noise(sec), 400) * 0.2 * np.interp(t, [0, 0.2, sec], [0, 1, 0]))
    return norm(outdoors(s, 1.0, 0.25), 0.7)


# ---------------------------------------------------------------------------
# Interface
# ---------------------------------------------------------------------------
def ui_click():
    s = mix(hp(noise(0.004), 1500) * 0.6, np.sin(2 * np.pi * 2300 * t_(0.03)) * expenv(0.03, 0.006) * 0.4,
            at(hp(noise(0.003), 2500) * 0.3, 0.022))
    return norm(pad(s, 0.06), 0.5)


def ui_tab():
    s = mix(bp(noise(0.02), 500, 2500) * expenv(0.02, 0.005), np.sin(2 * np.pi * 900 * t_(0.04)) * expenv(0.04, 0.01) * 0.3)
    return norm(pad(s, 0.06), 0.4)


def bell(f, sec=1.6):
    parts = ((1.0, 1.0), (2.76, 0.5), (5.4, 0.25), (8.9, 0.12))
    return sum(np.sin(2 * np.pi * f * r * t_(sec)) * expenv(sec, 0.6 / (1 + r * 0.4), 0.001) * a for r, a in parts)


def ui_ready():
    return norm(fade(mix(bell(1320), at(bell(1760) * 0.8, 0.12)), 0, 0.2), 0.5)


def ui_denied():
    t = t_(0.35)
    s = np.sign(np.sin(2 * np.pi * 150 * t)) * 0.4 + np.sin(2 * np.pi * 157 * t) * 0.4
    s = lp(s, 1800) * np.interp(t, [0, 0.01, 0.12, 0.13, 0.17, 0.18, 0.3, 0.35], [0, 1, 1, 0, 0, 1, 1, 0])
    return norm(s, 0.4)


def ui_place():
    return norm(mix(thump(0.35, 110, 45, 0.06, 1.0), bp(noise(0.1), 150, 1500) * expenv(0.1, 0.02) * 0.6), 0.6)


def ui_sell():
    s = np.zeros(n_(0.5))
    for k in range(6):
        c = bp(noise(0.02), 1500, 5000) * expenv(0.02, 0.004)
        i = n_(0.04 + k * 0.06)
        s[i:i + len(c)] += c[:len(s) - i] * (1 - k * 0.1)
    return norm(s, 0.45)


def ui_message():
    """A teleprinter chattering a short message."""
    s = np.zeros(n_(0.9))
    p = 0.0
    while p < 0.8:
        c = mix(bp(noise(0.015), 800, 4500) * expenv(0.015, 0.003), np.sin(2 * np.pi * 1450 * t_(0.012)) * 0.1)
        i = n_(p)
        s[i:i + len(c)] += c[:len(s) - i] * rng.uniform(0.5, 1.0)
        p += rng.uniform(0.04, 0.07)
    return norm(s, 0.4)


def ui_objective():
    return norm(fade(mix(bell(880, 2.2), at(bell(1108.7, 2.2), 0.18), at(bell(1318.5, 2.2), 0.36)), 0, 0.4), 0.5)


# ---------------------------------------------------------------------------
# Ambience (stereo loops)
# ---------------------------------------------------------------------------
def wind(sec, low=200, high=1200, gust=0.12, howl=False):
    out = []
    for ch in range(2):
        g = 0.55 + 0.45 * lp(noise(sec), gust, 1) / 0.02
        g = np.clip(0.4 + 0.6 * (g - g.min()) / (np.ptp(g) + 1e-9), 0, 1)
        w = bp(pink(sec), low, high) * g
        if howl:
            f = 520 + 180 * lp(noise(sec), 0.2, 1) / 0.01
            f = np.clip(f, 300, 900)
            h = np.sin(2 * np.pi * np.cumsum(f) / SR) * bp(noise(sec), 300, 1200) * 0.0
            res = np.zeros(n_(sec))
            # Whistling through branches: narrow band noise around a slowly moving centre.
            for k in range(0, n_(sec), n_(0.25)):
                seg = slice(k, min(k + n_(0.25), n_(sec)))
                c = float(np.clip(f[k], 300, 900))
                res[seg] = bp(noise(0.3), c * 0.95, c * 1.05)[: seg.stop - seg.start]
            w = w + lp(res, 1500) * g ** 2 * 0.6 + h
        out.append(w)
    return np.stack(out, 1)


def bird_call(kind):
    if kind == 0:  # a run of quick chirps
        s = np.zeros(n_(0.9))
        p = 0.0
        f0 = rng.uniform(3000, 4500)
        for _ in range(rng.integers(3, 7)):
            c = sweep(0.05, f0, f0 * rng.uniform(1.2, 1.6)) * np.hanning(n_(0.05))
            i = n_(p)
            s[i:i + len(c)] += c[:len(s) - i]
            p += rng.uniform(0.07, 0.11)
        return s
    if kind == 1:  # a two-note whistle (a blackbird or thrush)
        f = rng.uniform(1800, 2600)
        a = np.sin(2 * np.pi * f * t_(0.18)) * np.hanning(n_(0.18))
        b = np.sin(2 * np.pi * f * 1.25 * t_(0.25)) * np.hanning(n_(0.25))
        return mix(a, at(b, 0.22))
    # a trill
    t = t_(0.6)
    f = rng.uniform(4000, 5500) * (1 + 0.15 * np.sin(2 * np.pi * 28 * t))
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.hanning(len(t)) * 0.7


def scatter(sec, maker, count, gain=(0.05, 0.25), pan=True):
    out = np.zeros((n_(sec), 2))
    for _ in range(count):
        x = maker()
        p = rng.uniform(0, sec - len(x) / SR - 0.1)
        g = rng.uniform(*gain)
        side = rng.uniform(0.2, 0.8) if pan else 0.5
        i = n_(p)
        out[i:i + len(x), 0] += x * g * np.sqrt(1 - side)
        out[i:i + len(x), 1] += x * g * np.sqrt(side)
    return out


def stereo_loop(x, xfade=1.5):
    return np.stack([make_loop(x[:, 0], xfade), make_loop(x[:, 1], xfade)], 1)


def amb_temperate():
    sec = 45
    w = wind(sec, 150, 1100) * 0.5
    leaves = np.stack([hp(pink(sec), 2500) * 0.04 * (0.5 + 0.5 * np.sin(2 * np.pi * 0.07 * t_(sec) + c)) for c in (0, 1.3)], 1)
    birds = scatter(sec, lambda: lp(bird_call(int(rng.integers(0, 3))), 7000), 38, (0.03, 0.12))
    return norm(stereo_loop(w + leaves + birds), 0.5)


def amb_winter():
    sec = 45
    w = wind(sec, 250, 2200, gust=0.2, howl=True) * 0.6
    snow = np.stack([hp(noise(sec), 5000) * 0.008 for _ in range(2)], 1)
    return norm(stereo_loop(w + snow), 0.5)


def amb_desert():
    sec = 45
    w = wind(sec, 300, 3000, gust=0.08) * 0.45
    sand = np.stack([hp(pink(sec), 4000) * 0.05 * (0.4 + 0.6 * np.abs(lp(noise(sec), 0.3, 1)) / 0.01).clip(0, 1.5)
                     for _ in range(2)], 1)
    return norm(stereo_loop(w + sand), 0.45)


def cicada():
    sec = rng.uniform(2.0, 4.0)
    t = t_(sec)
    f = rng.uniform(4200, 6000)
    am = 0.5 + 0.5 * np.sign(np.sin(2 * np.pi * rng.uniform(40, 70) * t))
    return np.sin(2 * np.pi * f * t) * am * np.interp(t, [0, 0.3, sec - 0.5, sec], [0, 1, 1, 0]) * 0.5


def frog():
    s = np.zeros(n_(0.8))
    for k in range(rng.integers(2, 5)):
        c = (np.sin(2 * np.pi * rng.uniform(250, 420) * t_(0.06)) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 90 * t_(0.06))))
             * np.hanning(n_(0.06)))
        i = n_(k * 0.12)
        s[i:i + len(c)] += c[:len(s) - i]
    return lp(s, 1500)


def amb_tropical():
    sec = 45
    w = wind(sec, 150, 800, gust=0.05) * 0.2
    insects = np.stack([bp(noise(sec), 6000, 9000) * 0.02 * (0.7 + 0.3 * np.sin(2 * np.pi * 0.05 * t_(sec) + c))
                        for c in (0, 2)], 1)
    calls = scatter(sec, cicada, 14, (0.02, 0.06)) + scatter(sec, frog, 30, (0.05, 0.15)) + \
        scatter(sec, lambda: bird_call(int(rng.integers(0, 3))), 20, (0.02, 0.07))
    return norm(stereo_loop(w + insects + calls), 0.45)


def creak():
    sec = rng.uniform(0.6, 1.2)
    t = t_(sec)
    f = rng.uniform(300, 500) * (1 + 0.2 * np.sin(2 * np.pi * 1.3 * t))
    pulses = (np.sin(2 * np.pi * np.cumsum(f / 6) / SR) > 0.7).astype(float)
    return bp(pulses * noise(sec), 400, 2500) * np.hanning(len(t)) * 0.6


def amb_urban():
    sec = 45
    w = wind(sec, 200, 1500, gust=0.15, howl=True) * 0.45
    far = np.stack([lp(brown(sec), 120, 2) * 0.12 for _ in range(2)], 1)
    events = scatter(sec, creak, 10, (0.04, 0.1)) + scatter(sec, lambda: lp(collapse(0) * 0.3, 900), 2, (0.05, 0.1))
    return norm(stereo_loop(w + far + events), 0.45)


def distant_battle():
    """The front, heard from far away: guns and machine guns over the hills."""
    sec = 50
    out = np.zeros((n_(sec), 2))
    sources = [(lambda: howitzer(0), 26, (0.15, 0.5)), (lambda: cannon(1, 0), 30, (0.1, 0.35)),
               (lambda: explosion(1, 0), 30, (0.1, 0.35)),
               (lambda: mg_burst(0, int(rng.integers(5, 14)), 0.07), 50, (0.05, 0.2)),
               (lambda: rifle(0), 70, (0.03, 0.12))]
    for make, count, gain in sources:
        out += scatter(sec, lambda: lp(make(), 700, 4), count, gain)
    rumble_bed = np.stack([lp(brown(sec), 90, 2) * 0.25 for _ in range(2)], 1)
    out = out + rumble_bed
    # Soften it all into the distance.
    out = np.stack([lp(out[:, c], 900, 2) for c in range(2)], 1)
    return norm(stereo_loop(out, 2.0), 0.6)


# ---------------------------------------------------------------------------
def trim(x, floor=0.0015):
    """Cuts the silence after a one-shot sound has died away."""
    env = np.abs(x) if x.ndim == 1 else np.abs(x).max(1)
    loud = np.nonzero(env > floor)[0]
    if len(loud) == 0:
        return x
    end = min(len(x), loud[-1] + n_(0.05))
    return fade(x[:end], 0, 0.05) if x.ndim == 1 else x[:end]


def write_ogg(path, x, quality=4, loop=False):
    if not loop:
        x = trim(x)
    x = np.clip(x, -1, 1)
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
        tmp = f.name
    try:
        wavfile.write(tmp, SR, (x * 32767).astype(np.int16))
        subprocess.run(['oggenc', '-Q', '-q', str(quality), '-o', path, tmp], check=True)
    finally:
        os.unlink(tmp)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'content/audio'
    sfx = os.path.join(root, 'sfx')
    amb = os.path.join(root, 'ambience')
    os.makedirs(sfx, exist_ok=True)
    os.makedirs(amb, exist_ok=True)
    jobs = []
    for v in range(4):
        jobs += [(f'rifle_{v}', lambda v=v: rifle(v)),
                 (f'mg_{v}', lambda v=v: mg_burst(v, int(rng.integers(4, 7)), 0.065)),
                 (f'lmg_{v}', lambda v=v: mg_burst(v, int(rng.integers(3, 5)), 0.115)),
                 (f'hmg_{v}', lambda v=v: mg_burst(v, int(rng.integers(5, 8)), 0.09, heavy=True))]
    for v in range(3):
        jobs += [(f'cannon_small_{v}', lambda v=v: cannon(0, v)), (f'cannon_medium_{v}', lambda v=v: cannon(1, v)),
                 (f'cannon_large_{v}', lambda v=v: cannon(2, v)), (f'howitzer_{v}', lambda v=v: howitzer(v)),
                 (f'explosion_small_{v}', lambda v=v: explosion(0, v)),
                 (f'explosion_medium_{v}', lambda v=v: explosion(1, v)),
                 (f'explosion_large_{v}', lambda v=v: explosion(2, v)),
                 (f'armour_hit_{v}', lambda v=v: armour_hit(v)), (f'ricochet_{v}', lambda v=v: ricochet(v)),
                 (f'rocket_{v}', lambda v=v: rocket(v)), (f'radio_{v}', lambda v=v: radio(v))]
    for v in range(2):
        jobs += [(f'sniper_{v}', lambda v=v: sniper(v)), (f'at_launcher_{v}', lambda v=v: at_launcher(v)),
                 (f'at_rifle_{v}', lambda v=v: at_rifle(v)), (f'vehicle_destroyed_{v}', lambda v=v: vehicle_destroyed(v)),
                 (f'collapse_{v}', lambda v=v: collapse(v)), (f'construction_{v}', lambda v=v: construction(v)),
                 (f'whistle_{v}', lambda v=v: whistle(v)), (f'engine_rev_{v}', lambda v=v: engine_rev(v))]
    jobs += [('tank_engine', tank_engine), ('truck_engine', truck_engine), ('march', march), ('fire', fire_loop),
             ('ui_click', ui_click), ('ui_tab', ui_tab), ('ui_ready', ui_ready), ('ui_denied', ui_denied),
             ('ui_place', ui_place), ('ui_sell', ui_sell), ('ui_message', ui_message), ('ui_objective', ui_objective)]
    loops = {'tank_engine', 'truck_engine', 'march', 'fire'}
    for name, make in jobs:
        write_ogg(os.path.join(sfx, name + '.ogg'), make(), loop=name in loops)
        print('sfx', name, flush=True)
    for name, make in (('temperate', amb_temperate), ('winter', amb_winter), ('desert', amb_desert),
                       ('tropical', amb_tropical), ('urban', amb_urban), ('distant_battle', distant_battle)):
        write_ogg(os.path.join(amb, name + '.ogg'), make(), quality=3, loop=True)
        print('ambience', name, flush=True)


if __name__ == '__main__':
    main()
