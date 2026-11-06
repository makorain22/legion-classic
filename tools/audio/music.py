#!/usr/bin/env python3
"""Legion Classic - the score.

Nine orchestral pieces written here note by note (melodies, harmony,
orchestration and dynamics are all in this file), played by the FluidR3 GM
soundfont (MIT licence) through FluidSynth, then given a concert-hall
reverb and mastered. The music is ours; see content/audio/LICENSES.md.

    python3 tools/audio/music.py content/audio [piece ...]

needs fluidsynth, the fluid-soundfont-gm package (or SF2=path/to/FluidR3_GM.sf2),
oggenc, numpy, scipy and mido.
"""
import os
import random
import subprocess
import sys
import tempfile

import mido
import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
SF2 = os.environ.get('SF2', '/usr/share/sounds/sf2/FluidR3_GM.sf2')

# General MIDI programs (0-based).
STRINGS, SLOW_STRINGS, TREMOLO, PIZZ, VIOLIN, CELLO, BASSES = 48, 49, 44, 45, 40, 42, 43
HORN, TRUMPET, TROMBONE, TUBA, BRASS = 60, 56, 57, 58, 61
TIMPANI, HARP, BELLS = 47, 46, 14
CHOIR, OOHS = 52, 53
OBOE, CLARINET, FLUTE, BASSOON, ENGLISH_HORN = 68, 71, 73, 70, 69
# Percussion (channel 10) notes.
SNARE, BASS_DRUM, CRASH, CRASH2, SUS_CYM, LOW_TOM, HI_TOM, GONG = 38, 35, 49, 57, 51, 41, 45, 52

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def pitch(name):
    """'D4', 'Bb3', 'C#5' -> MIDI number; ints pass through; 'r' = rest."""
    if isinstance(name, int) or name == 'r':
        return name
    n = NOTE[name[0]]
    i = 1
    while i < len(name) and name[i] in '#b':
        n += 1 if name[i] == '#' else -1
        i += 1
    return n + 12 * (int(name[i:]) + 1)


def chord_tones(ch):
    """'Dm', 'Bb', 'A7', 'Gm', 'Ebmaj7', 'Dsus4' -> pitch classes, root first."""
    root = NOTE[ch[0]]
    i = 1
    while i < len(ch) and ch[i] in '#b':
        root += 1 if ch[i] == '#' else -1
        i += 1
    q = ch[i:]
    if q.startswith('m') and not q.startswith('maj'):
        ivs = [0, 3, 7]
        q = q[1:]
    elif q.startswith('sus4'):
        ivs = [0, 5, 7]
        q = q[4:]
    elif q.startswith('dim'):
        ivs = [0, 3, 6]
        q = q[3:]
    else:
        ivs = [0, 4, 7]
    if q == '7':
        ivs.append(10)
    elif q == 'maj7':
        ivs.append(11)
    elif q == 'add9':
        ivs.append(14)
    return [(root + v) % 12 for v in ivs]


def near(pc, around):
    """The pitch of class pc nearest to `around`."""
    base = around - ((around - pc) % 12)
    return base if around - base <= 6 else base + 12


class Song:
    def __init__(self, bpm, beats=4, seed=1):
        self.bpm = bpm
        self.beats = beats
        self.notes = []   # (track, start, dur, pitch, vel)
        self.ccs = []     # (track, beat, cc, value)
        self.tempo = []   # (beat, bpm)
        self.tracks = {}  # name -> (channel, program)
        self.rng = random.Random(seed)

    def track(self, name, program, channel=None):
        if channel is None:
            used = {c for c, _ in self.tracks.values()}
            channel = next(c for c in range(16) if c != 9 and c not in used)
        self.tracks[name] = (channel, program)

    def bar(self, b):
        return b * self.beats

    def note(self, tr, p, start, dur, vel=80, human=True):
        p = pitch(p)
        if p == 'r':
            return
        if human:
            start += self.rng.uniform(-0.012, 0.012)
            vel += self.rng.randint(-4, 4)
        self.notes.append((tr, max(0.0, start), dur, p, int(max(1, min(127, vel)))))

    def cc(self, tr, beat, num, val):
        self.ccs.append((tr, beat, num, int(max(0, min(127, val)))))

    def swell(self, tr, b0, b1, v0, v1, num=11, steps=16):
        for k in range(steps + 1):
            t = k / steps
            self.cc(tr, b0 + (b1 - b0) * t, num, v0 + (v1 - v0) * t)

    def set_tempo(self, beat, bpm):
        self.tempo.append((beat, bpm))

    # --- patterns ------------------------------------------------------------
    def melody(self, tr, line, start_bar, shift=0, vel=84, legato=1.0, accent_first=True):
        b = self.bar(start_bar)
        for k, (p, d) in enumerate(line):
            if p != 'r':
                v = vel + (6 if accent_first and k == 0 else 0)
                self.note(tr, pitch(p) + shift, b, d * legato, v)
            b += d
        return b

    def pads(self, tr, chords, start_bar, top, voices=4, vel=64, bars_each=1, spread=False):
        """Sustained chords voiced downward from around `top`."""
        for k, ch in enumerate(chords):
            pcs = chord_tones(ch)
            notes = []
            cur = pitch(top) if isinstance(top, str) else top
            order = [pcs[1], pcs[2], pcs[0]] + pcs[3:]
            for i in range(voices):
                pc = order[i % len(order)]
                n = near(pc, cur)
                while notes and n >= notes[-1]:
                    n -= 12
                notes.append(n)
                cur = n - (7 if spread else 3)
            for n in notes:
                self.note(tr, n, self.bar(start_bar + k * bars_each), self.bar(bars_each) - 0.05, vel, human=False)

    def bass(self, tr, chords, start_bar, octave=2, pattern='whole', vel=80, bars_each=1):
        for k, ch in enumerate(chords):
            r = chord_tones(ch)[0] + 12 * (octave + 1)
            f = chord_tones(ch)[2] + 12 * (octave + 1)
            if f > r + 7:
                f -= 12
            b = self.bar(start_bar + k * bars_each)
            L = self.bar(bars_each)
            if pattern == 'whole':
                self.note(tr, r, b, L - 0.05, vel)
            elif pattern == 'halves':
                self.note(tr, r, b, L / 2 - 0.05, vel)
                self.note(tr, r if k % 2 == 0 else f, b + L / 2, L / 2 - 0.05, vel - 8)
            elif pattern == 'march':  # root - fifth on the beats
                for i in range(int(L)):
                    self.note(tr, r if i % 2 == 0 else f, b + i, 0.8, vel - (0 if i == 0 else 10))
            elif pattern == 'eighths':  # driving
                for i in range(int(L * 2)):
                    self.note(tr, r if i % 8 != 6 else f, b + i * 0.5, 0.42, vel - (0 if i % 2 == 0 else 14))
            elif pattern == 'gallop':  # long-short-short
                for i in range(int(L)):
                    self.note(tr, r, b + i, 0.45, vel)
                    self.note(tr, r, b + i + 0.5, 0.2, vel - 14)
                    self.note(tr, r, b + i + 0.75, 0.2, vel - 14)

    def ostinato(self, tr, chords, start_bar, around, figure, step=0.5, vel=72, dur=0.4, bars_each=1):
        """figure: chord-tone indices (0 root, 1 third, 2 fifth, +3 = octave up)."""
        for k, ch in enumerate(chords):
            pcs = chord_tones(ch)
            root = near(pcs[0], pitch(around))
            b = self.bar(start_bar + k * bars_each)
            n = int(self.bar(bars_each) / step)
            for i in range(n):
                idx = figure[i % len(figure)]
                if idx is None:
                    continue
                octv, j = divmod(idx, 3)
                p = root + ((pcs[j] - pcs[0]) % 12) + 12 * octv
                accent = 10 if i % int(round(1 / step)) == 0 else 0
                self.note(tr, p, b + i * step, dur, vel + accent)

    def arp(self, tr, chords, start_bar, around, step=0.25, vel=58, bars_each=1, up_down=True):
        for k, ch in enumerate(chords):
            pcs = chord_tones(ch)[:3]
            root = near(pcs[0], pitch(around))
            seq = [root + ((pc - pcs[0]) % 12) + 12 * o for o in (0, 1) for pc in pcs] + [root + 24]
            if up_down:
                seq = seq + seq[-2:0:-1]
            b = self.bar(start_bar + k * bars_each)
            n = int(self.bar(bars_each) / step)
            for i in range(n):
                self.note(tr, seq[i % len(seq)], b + i * step, step * 3, vel - (i % 4) * 3)

    def timpani(self, tr, chords, start_bar, pattern='downbeat', vel=90, bars_each=1):
        for k, ch in enumerate(chords):
            pcs = chord_tones(ch)
            r = near(pcs[0], pitch('D3'))
            f = near(pcs[2], pitch('A2'))
            b = self.bar(start_bar + k * bars_each)
            L = self.bar(bars_each)
            if pattern == 'downbeat':
                self.note(tr, r, b, 1.5, vel)
            elif pattern == 'march':
                for i in range(int(L)):
                    self.note(tr, r if i % 2 == 0 else f, b + i, 0.9, vel - (0 if i == 0 else 15))
            elif pattern == 'drive':
                for i in range(int(L * 2)):
                    if i % 8 in (0, 3, 6):
                        self.note(tr, r if i % 8 != 6 else f, b + i * 0.5, 0.4, vel - (0 if i % 8 == 0 else 12))
            elif pattern == 'roll':
                n = int(L / 0.125)
                for i in range(n):
                    self.note(tr, r, b + i * 0.125, 0.12, int(vel * (0.45 + 0.55 * i / n)), human=False)

    def drums(self, start_bar, bars, pattern, vel=80):
        for k in range(bars):
            b = self.bar(start_bar + k)
            if pattern == 'march':  # a side drum march: long-short-short, a roll into the bar
                for i, (off, v) in enumerate(((0, 1.0), (1, 0.7), (1.5, 0.55), (1.75, 0.55), (2, 0.9), (3, 0.7),
                                              (3.5, 0.6), (3.75, 0.6))):
                    self.note('drums', SNARE, b + off, 0.2, int(vel * v))
                self.note('drums', BASS_DRUM, b, 0.5, vel - 10)
                self.note('drums', BASS_DRUM, b + 2, 0.5, vel - 20)
            elif pattern == 'roll':
                n = int(self.beats / 0.125)
                for i in range(n):
                    self.note('drums', SNARE, b + i * 0.125, 0.1, int(vel * (0.35 + 0.65 * (k * n + i) / (bars * n))),
                              human=False)
            elif pattern == 'pulse':  # bass drum heartbeat
                self.note('drums', BASS_DRUM, b, 0.5, vel)
                self.note('drums', BASS_DRUM, b + 0.5, 0.5, vel - 25)
            elif pattern == 'toms':
                for off, nn in ((0, LOW_TOM), (1.5, LOW_TOM), (2, HI_TOM), (3, LOW_TOM), (3.5, LOW_TOM)):
                    self.note('drums', nn, b + off, 0.3, vel - (0 if off == 0 else 12))

    def crash(self, bar, vel=90, note=CRASH):
        self.note('drums', note, self.bar(bar), 3.5, vel)

    # --- output ----------------------------------------------------------------
    def save(self, path):
        tpb = 480
        mid = mido.MidiFile(ticks_per_beat=tpb)
        meta = mido.MidiTrack()
        mid.tracks.append(meta)
        tempos = sorted([(0.0, self.bpm)] + self.tempo)
        last = 0
        for beat, bpm in tempos:
            t = int(beat * tpb)
            meta.append(mido.MetaMessage('set_tempo', tempo=mido.bpm2tempo(bpm), time=t - last))
            last = t
        for name, (ch, prog) in self.tracks.items():
            tr = mido.MidiTrack()
            mid.tracks.append(tr)
            ev = []
            if ch != 9:
                ev.append((0, 0, mido.Message('program_change', channel=ch, program=prog)))
                ev.append((0, 0, mido.Message('control_change', channel=ch, control=7, value=100)))
                ev.append((0, 0, mido.Message('control_change', channel=ch, control=91, value=0)))
                ev.append((0, 0, mido.Message('control_change', channel=ch, control=93, value=0)))
            for (t, b, num, val) in self.ccs:
                if t == name:
                    ev.append((int(b * tpb), 1, mido.Message('control_change', channel=ch, control=num, value=val)))
            for (t, s, d, p, v) in self.notes:
                if t != name:
                    continue
                ev.append((int(s * tpb), 3, mido.Message('note_on', channel=ch, note=p, velocity=v)))
                ev.append((int((s + max(0.05, d)) * tpb), 2, mido.Message('note_off', channel=ch, note=p, velocity=0)))
            ev.sort(key=lambda e: (e[0], e[1]))
            last = 0
            for tick, _, msg in ev:
                tr.append(msg.copy(time=tick - last))
                last = tick
        mid.save(path)

    def length_seconds(self):
        end = max(s + d for _, s, d, _, _ in self.notes)
        tempos = sorted([(0.0, self.bpm)] + self.tempo)
        sec, prev_b, prev_bpm = 0.0, 0.0, self.bpm
        for b, bpm in tempos[1:]:
            if b >= end:
                break
            sec += (b - prev_b) * 60 / prev_bpm
            prev_b, prev_bpm = b, bpm
        return sec + (end - prev_b) * 60 / prev_bpm


# ---------------------------------------------------------------------------
# The themes
# ---------------------------------------------------------------------------
# Legion: the main theme, D minor. i - VI - III - VII | i - iv - V - i.
LEGION_CHORDS_A = ['Dm', 'Bb', 'F', 'C', 'Dm', 'Gm', 'A', 'Dm']
LEGION_A = [('D5', 2), ('A4', 1), ('D5', 1),
            ('F5', 2), ('E5', 0.5), ('D5', 0.5), ('D5', 1),
            ('C5', 2), ('A4', 1), ('C5', 1),
            ('E5', 3), ('D5', 0.5), ('C5', 0.5),
            ('D5', 2), ('F5', 1), ('A5', 1),
            ('G5', 2), ('F5', 1), ('D5', 1),
            ('E5', 2), ('C#5', 1), ('A4', 1),
            ('D5', 4)]
LEGION_CHORDS_B = ['Bb', 'F', 'Gm', 'Dm', 'Bb', 'C', 'A', 'A']
LEGION_B = [('D5', 1.5), ('C5', 0.5), ('Bb4', 1), ('C5', 1),
            ('A4', 3), ('C5', 1),
            ('Bb4', 2), ('D5', 1), ('G5', 1),
            ('F5', 3), ('E5', 1),
            ('D5', 2), ('F5', 1), ('D5', 1),
            ('E5', 2), ('G5', 1), ('E5', 1),
            ('C#5', 2), ('E5', 1), ('A5', 1),
            ('A5', 4)]


def legion():
    """Main menu: the theme, from a distant horn call to the full orchestra."""
    s = Song(70, seed=11)
    for name, prog in (('basses', BASSES), ('cellos', CELLO), ('strings', SLOW_STRINGS), ('violins', STRINGS),
                       ('horns', HORN), ('trumpets', TRUMPET), ('trombones', TROMBONE), ('tuba', TUBA),
                       ('timpani', TIMPANI), ('choir', CHOIR), ('harp', HARP)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    # Intro: a low D under a timpani roll, the choir breathing in.
    s.bass('basses', ['Dm'] * 4, 0, 1, 'whole', 70)
    s.bass('cellos', ['Dm', 'Dm', 'Bb', 'A'], 0, 2, 'whole', 62)
    s.pads('choir', ['Dm', 'Dm', 'Bb', 'A'], 0, 'A4', 3, 50)
    s.swell('choir', 0, 16, 30, 90)
    s.timpani('timpani', ['Dm'], 2, 'roll', 80)
    s.timpani('timpani', ['A'], 3, 'roll', 95)
    s.melody('horns', [('A3', 2), ('D4', 1), ('E4', 1), ('F4', 3), ('E4', 1)], 1, vel=62)
    # A: the theme on the horns over strings and harp.
    s.melody('horns', LEGION_A, 4, -12, vel=84, legato=0.97)
    s.pads('strings', LEGION_CHORDS_A, 4, 'A4', 4, 60)
    s.bass('cellos', LEGION_CHORDS_A, 4, 2, 'halves', 66)
    s.bass('basses', LEGION_CHORDS_A, 4, 1, 'whole', 64)
    s.arp('harp', LEGION_CHORDS_A, 4, 'D3', 0.25, 50)
    for b in (4, 8):
        s.timpani('timpani', [LEGION_CHORDS_A[b - 4]], b, 'downbeat', 78)
    s.swell('strings', 16, 48, 70, 100)
    # B: the strings sing; the horns hold the harmony; a build to the climax.
    s.melody('violins', LEGION_B, 12, 0, vel=80, legato=1.0)
    s.pads('horns', LEGION_CHORDS_B, 12, 'F4', 3, 58)
    s.pads('strings', LEGION_CHORDS_B, 12, 'D4', 3, 62)
    s.bass('cellos', LEGION_CHORDS_B, 12, 2, 'march', 70)
    s.bass('basses', LEGION_CHORDS_B, 12, 1, 'whole', 72)
    s.timpani('timpani', LEGION_CHORDS_B[:6], 12, 'march', 70)
    s.timpani('timpani', ['A', 'A'], 18, 'roll', 100)
    s.drums(18, 2, 'roll', 92)
    s.swell('violins', 48, 80, 80, 120)
    s.pads('choir', ['A', 'A'], 18, 'E5', 3, 70)
    # A, full: trumpets and horns in octaves, choir, brass chords, cymbals.
    s.crash(20, 105)
    s.note('drums', GONG, s.bar(20), 4, 80)
    s.melody('trumpets', LEGION_A, 20, 0, vel=92)
    s.melody('horns', LEGION_A, 20, -12, vel=92)
    s.melody('violins', LEGION_A, 20, 12, vel=82)
    s.pads('trombones', LEGION_CHORDS_A, 20, 'F3', 3, 82)
    s.bass('tuba', LEGION_CHORDS_A, 20, 1, 'halves', 84)
    s.pads('choir', LEGION_CHORDS_A, 20, 'A4', 4, 84)
    s.pads('strings', LEGION_CHORDS_A, 20, 'F4', 4, 76)
    s.bass('cellos', LEGION_CHORDS_A, 20, 2, 'march', 84)
    s.bass('basses', LEGION_CHORDS_A, 20, 1, 'halves', 86)
    s.timpani('timpani', LEGION_CHORDS_A, 20, 'march', 92)
    s.drums(20, 7, 'march', 70)
    s.crash(24, 92)
    s.timpani('timpani', ['A'], 26, 'roll', 108)
    s.crash(27, 112)
    # Outro: the last chord dies away; a lone horn remembers the opening.
    s.pads('strings', ['Dm', 'Bb', 'Dm'], 28, 'A4', 4, 60)
    s.pads('choir', ['Dm', 'Bb', 'Dm'], 28, 'A4', 3, 52)
    s.bass('basses', ['Dm', 'Bb', 'Dm'], 28, 1, 'whole', 60)
    s.melody('horns', [('A3', 2), ('D4', 1), ('E4', 1), ('F4', 2), ('E4', 1), ('D4', 1), ('D4', 4)], 28, vel=58)
    s.swell('strings', 112, 124, 100, 30)
    s.swell('choir', 112, 124, 90, 20)
    s.timpani('timpani', ['Dm'], 30, 'downbeat', 60)
    s.set_tempo(s.bar(30), 62)
    return s


def iron_advance():
    """Battle: driving strings, side drums and the theme in the brass, D minor."""
    s = Song(116, seed=21)
    for name, prog in (('basses', BASSES), ('cellos', CELLO), ('ostinato', STRINGS), ('violins', STRINGS),
                       ('horns', HORN), ('trumpets', TRUMPET), ('trombones', TROMBONE), ('tuba', TUBA),
                       ('timpani', TIMPANI), ('choir', CHOIR)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    fig = [0, 0, 1, 0, 2, 0, 1, 0]
    A, B = LEGION_CHORDS_A, LEGION_CHORDS_B
    C = ['Gm', 'Eb', 'Bb', 'F', 'Gm', 'Eb', 'A', 'A']
    C_LINE = [('G4', 1.5), ('Bb4', 0.5), ('D5', 2), ('Eb5', 1.5), ('D5', 0.5), ('Bb4', 2), ('F5', 3), ('D5', 1),
              ('C5', 2), ('A4', 2), ('G4', 1.5), ('Bb4', 0.5), ('D5', 2), ('G5', 2), ('Eb5', 2), ('E5', 3),
              ('C#5', 1), ('A4', 4)]
    plan = [('intro', ['Dm', 'Dm', 'Bb', 'A']), ('A', A), ('B', B), ('A2', A), ('C', C), ('build', ['A'] * 4),
            ('A3', A), ('end', ['Dm', 'Dm'])]
    bar = 0
    for part, chords in plan:
        n = len(chords)
        s.ostinato('ostinato', chords, bar, 'D3', fig, 0.5, 70 if part == 'intro' else 76, 0.35)
        s.bass('cellos', chords, bar, 2, 'eighths' if part != 'intro' else 'gallop', 74)
        s.bass('basses', chords, bar, 1, 'halves', 78)
        if part == 'intro':
            s.drums(bar, 2, 'pulse', 80)
            s.drums(bar + 2, 2, 'march', 72)
            s.timpani('timpani', chords, bar, 'drive', 84)
        elif part in ('A', 'A2', 'A3'):
            loud = part != 'A'
            s.melody('horns', LEGION_A, bar, -12, vel=90 if loud else 84)
            if loud:
                s.melody('trumpets', LEGION_A, bar, 0, vel=94)
                s.pads('trombones', chords, bar, 'F3', 3, 84)
                s.bass('tuba', chords, bar, 1, 'march', 82)
                s.pads('choir', chords, bar, 'A4', 3, 78 if part == 'A3' else 66)
                s.crash(bar, 104)
                s.crash(bar + 4, 92)
            s.drums(bar, n, 'march', 78 if loud else 70)
            s.timpani('timpani', chords, bar, 'march' if loud else 'downbeat', 92)
            if part == 'A3':
                s.melody('violins', LEGION_A, bar, 12, vel=84)
        elif part == 'B':
            s.melody('violins', LEGION_B, bar, 0, vel=84)
            s.pads('horns', chords, bar, 'F4', 3, 66)
            s.timpani('timpani', chords, bar, 'downbeat', 84)
            s.drums(bar, n - 2, 'march', 64)
            s.drums(bar + n - 2, 2, 'roll', 90)
        elif part == 'C':
            s.melody('trombones', C_LINE, bar, -12, vel=92)
            s.melody('horns', C_LINE, bar, 0, vel=80)
            s.bass('tuba', chords, bar, 1, 'halves', 82)
            s.pads('violins', chords, bar, 'D5', 3, 62)
            s.drums(bar, n, 'toms', 82)
            s.timpani('timpani', chords, bar, 'drive', 90)
        elif part == 'build':
            s.timpani('timpani', chords, bar, 'roll', 104)
            s.drums(bar, n, 'roll', 104)
            s.pads('choir', chords, bar, 'E5', 3, 70)
            s.swell('choir', s.bar(bar), s.bar(bar + n), 40, 120)
            s.pads('horns', chords, bar, 'C#5', 3, 80)
            s.swell('horns', s.bar(bar), s.bar(bar + n), 50, 120)
        elif part == 'end':
            s.note('trumpets', 'D5', s.bar(bar), 6, 100)
            s.note('horns', 'D4', s.bar(bar), 6, 96)
            s.pads('trombones', ['Dm'], bar, 'F3', 3, 96)
            s.note('tuba', 'D2', s.bar(bar), 6, 96)
            s.crash(bar, 115)
            s.note('drums', GONG, s.bar(bar), 6, 100)
            s.timpani('timpani', ['Dm'], bar, 'roll', 112)
        bar += n
    return s


def breakthrough():
    """Battle: C minor, relentless sixteenths, a rising call to the assault."""
    s = Song(128, seed=31)
    for name, prog in (('basses', BASSES), ('cellos', CELLO), ('ostinato', STRINGS), ('tremolo', TREMOLO),
                       ('horns', HORN), ('trumpets', TRUMPET), ('trombones', TROMBONE), ('tuba', TUBA),
                       ('timpani', TIMPANI), ('choir', CHOIR)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    P = ['Cm', 'Ab', 'Eb', 'Bb', 'Cm', 'Ab', 'Fm', 'G']
    Q = ['Ab', 'Bb', 'Gm', 'Cm', 'Ab', 'Bb', 'G', 'G']
    call = [('C4', 1.5), ('G4', 0.5), ('C5', 2), ('Eb5', 1.5), ('D5', 0.5), ('C5', 1), ('Bb4', 1),
            ('G4', 3), ('Bb4', 1), ('Ab4', 1.5), ('C5', 0.5), ('Eb5', 2), ('D5', 1.5), ('Eb5', 0.5), ('F5', 2),
            ('G5', 2), ('F5', 1), ('Eb5', 1), ('D5', 4)]
    answer = [('Eb5', 2), ('F5', 1), ('G5', 1), ('F5', 2), ('D5', 2), ('D5', 1.5), ('Eb5', 0.5), ('F5', 2),
              ('Eb5', 1), ('D5', 1), ('C5', 3), ('Eb5', 1), ('F5', 2), ('G5', 1), ('Ab5', 1), ('G5', 3), ('F5', 0.5),
              ('Eb5', 0.5), ('D5', 2), ('B4', 2), ('G4', 4)]
    fig16 = [0, 2, 3, 2, 0, 2, 3, 2, 1, 2, 3, 2, 1, 2, 3, 2]
    plan = [('intro', ['Cm'] * 4), ('P', P), ('Q', Q), ('P2', P), ('Q2', Q), ('end', ['Cm', 'Cm'])]
    bar = 0
    for part, chords in plan:
        n = len(chords)
        s.ostinato('ostinato', chords, bar, 'C3', fig16, 0.25, 68, 0.2)
        s.bass('cellos', chords, bar, 2, 'gallop', 76)
        s.bass('basses', chords, bar, 1, 'march', 80)
        if part == 'intro':
            s.timpani('timpani', chords, bar, 'drive', 90)
            s.drums(bar, 4, 'toms', 84)
            s.pads('tremolo', ['Cm', 'Cm', 'Ab', 'G'], bar, 'G4', 3, 60)
            s.swell('tremolo', 0, 16, 40, 100)
        elif part in ('P', 'P2'):
            s.melody('horns', call, bar, 0, vel=88)
            if part == 'P2':
                s.melody('trumpets', call, bar, 12, vel=90)
                s.pads('choir', chords, bar, 'G4', 3, 74)
                s.crash(bar, 104)
            s.pads('trombones', chords, bar, 'Eb3', 3, 74)
            s.drums(bar, n, 'march', 76)
            s.timpani('timpani', chords, bar, 'march', 90)
        elif part in ('Q', 'Q2'):
            s.melody('trumpets' if part == 'Q2' else 'horns', answer, bar, 0, vel=90)
            s.melody('trombones', answer, bar, -12, vel=82)
            s.pads('tremolo', chords, bar, 'Eb5', 3, 70)
            s.bass('tuba', chords, bar, 1, 'halves', 80)
            s.drums(bar, n - 1, 'toms', 80)
            s.drums(bar + n - 1, 1, 'roll', 100)
            s.timpani('timpani', chords, bar, 'drive', 92)
            s.crash(bar + 4, 96)
        elif part == 'end':
            for tr, p in (('trumpets', 'C5'), ('horns', 'G4'), ('trombones', 'Eb3'), ('tuba', 'C2'), ('choir', 'C5')):
                s.note(tr, p, s.bar(bar), 6, 100)
            s.crash(bar, 115)
            s.note('drums', GONG, s.bar(bar), 6, 100)
            s.timpani('timpani', ['Cm'], bar, 'roll', 112)
        bar += n
    return s


def hold_the_line():
    """Battle: G minor, heavy and slow, low brass and choir: a defence to the last."""
    s = Song(96, seed=41)
    for name, prog in (('basses', BASSES), ('cellos', CELLO), ('strings', STRINGS), ('violins', STRINGS),
                       ('horns', HORN), ('trombones', TROMBONE), ('tuba', TUBA), ('trumpets', TRUMPET),
                       ('timpani', TIMPANI), ('choir', CHOIR)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    P = ['Gm', 'Gm', 'Eb', 'Eb', 'Cm', 'D', 'Gm', 'D']
    theme = [('G3', 3), ('Bb3', 1), ('D4', 2), ('C4', 1), ('Bb3', 1), ('Eb4', 3), ('D4', 1), ('C4', 4),
             ('C4', 2), ('Eb4', 2), ('D4', 2), ('F#3', 2), ('G3', 4), ('A3', 2), ('F#3', 2)]
    high = [('D5', 3), ('Eb5', 1), ('F5', 2), ('Eb5', 1), ('D5', 1), ('G5', 3), ('F5', 1), ('Eb5', 4),
            ('Eb5', 2), ('G5', 2), ('F#5', 2), ('A5', 2), ('G5', 4), ('F#5', 4)]
    bar = 0
    for k, part in enumerate(['intro', 'low', 'high', 'low2', 'all', 'end']):
        chords = P if part not in ('intro', 'end') else (['Gm', 'Gm', 'Eb', 'D'] if part == 'intro' else ['Gm', 'Gm'])
        n = len(chords)
        s.bass('basses', chords, bar, 1, 'march', 82)
        s.bass('cellos', chords, bar, 2, 'gallop' if part != 'intro' else 'march', 74)
        if part == 'intro':
            s.drums(bar, n, 'pulse', 90)
            s.pads('choir', chords, bar, 'D4', 3, 56)
            s.swell('choir', 0, 16, 40, 100)
            s.timpani('timpani', chords, bar, 'downbeat', 96)
        if part in ('low', 'low2', 'all'):
            s.melody('trombones', theme, bar, 0, vel=94)
            s.melody('tuba', theme, bar, -12, vel=86)
            s.melody('horns', theme, bar, 12, vel=82 if part == 'low' else 90)
            s.pads('strings', chords, bar, 'D5', 3, 62)
            s.drums(bar, n, 'march' if part != 'low' else 'pulse', 78)
            s.timpani('timpani', chords, bar, 'march', 94)
        if part in ('high', 'all'):
            s.melody('violins', high, bar, 0, vel=86)
            s.melody('trumpets' if part == 'all' else 'horns', high, bar, -12 if part != 'all' else 0, vel=88)
            s.pads('choir', chords, bar, 'D5', 4, 78)
            s.timpani('timpani', chords, bar, 'drive', 92)
            s.drums(bar, n - 1, 'toms', 82)
            s.drums(bar + n - 1, 1, 'roll', 104)
            s.crash(bar, 104)
        if part == 'end':
            for tr, p in (('trumpets', 'D5'), ('horns', 'G4'), ('trombones', 'Bb3'), ('tuba', 'G1'), ('choir', 'G4'),
                          ('strings', 'D5')):
                s.note(tr, p, s.bar(bar), 7, 96)
            s.crash(bar, 112)
            s.note('drums', GONG, s.bar(bar), 7, 100)
            s.timpani('timpani', ['Gm'], bar, 'roll', 110)
        bar += n
    return s


def quiet_front():
    """Calm: A minor, a solo oboe over hushed strings and harp."""
    s = Song(62, seed=51)
    for name, prog in (('basses', BASSES), ('strings', SLOW_STRINGS), ('oboe', OBOE), ('horn', HORN),
                       ('harp', HARP), ('flute', FLUTE), ('cellos', CELLO)):
        s.track(name, prog)
    P = ['Am', 'F', 'C', 'G', 'Am', 'Dm', 'E', 'Am']
    Q = ['F', 'G', 'Em', 'Am', 'Dm', 'Am', 'E', 'E']
    line1 = [('E5', 2), ('C5', 1), ('A4', 1), ('A4', 2), ('C5', 1), ('F5', 1), ('E5', 3), ('G4', 1), ('B4', 2),
             ('D5', 2), ('C5', 1.5), ('B4', 0.5), ('A4', 2), ('F5', 2), ('D5', 2), ('E5', 1), ('D5', 1), ('B4', 1),
             ('G#4', 1), ('A4', 4)]
    line2 = [('A5', 2), ('G5', 1), ('F5', 1), ('G5', 3), ('D5', 1), ('E5', 2), ('B4', 2), ('C5', 3), ('E5', 1),
             ('F5', 2), ('E5', 1), ('D5', 1), ('C5', 2), ('A4', 2), ('B4', 3), ('G#4', 1), ('B4', 4)]
    bar = 0
    s.pads('strings', ['Am', 'Am'], 0, 'E4', 3, 50)
    s.arp('harp', ['Am', 'Am'], 0, 'A2', 0.5, 50)
    bar = 2
    for k, (chords, line, solo) in enumerate(((P, line1, 'oboe'), (Q, line2, 'flute'), (P, line1, 'horn'),
                                               (Q, line2, 'oboe'))):
        s.pads('strings', chords, bar, 'E4', 4, 54 + 4 * (k % 2))
        s.bass('basses', chords, bar, 1, 'whole', 52)
        s.bass('cellos', chords, bar, 2, 'halves', 50)
        s.arp('harp', chords, bar, 'A2', 0.5, 48)
        s.melody(solo, line, bar, -12 if solo == 'horn' else 0, vel=70, legato=0.98)
        bar += 8
    s.pads('strings', ['Am', 'Am'], bar, 'E4', 4, 46)
    s.bass('basses', ['Am'], bar, 1, 'whole', 46)
    s.note('harp', 'A3', s.bar(bar), 4, 52)
    s.swell('strings', s.bar(bar), s.bar(bar + 2), 100, 20)
    return s


def before_dawn():
    """Calm: E minor, low strings and a distant horn; the guns are silent."""
    s = Song(56, seed=61)
    for name, prog in (('basses', BASSES), ('cellos', CELLO), ('strings', SLOW_STRINGS), ('horn', HORN),
                       ('clarinet', CLARINET), ('timpani', TIMPANI), ('choir', OOHS)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    P = ['Em', 'C', 'Am', 'B', 'Em', 'G', 'Am', 'B']
    Q = ['C', 'G', 'D', 'Em', 'C', 'Am', 'B', 'B']
    horn = [('B3', 3), ('E4', 1), ('G4', 2), ('F#4', 2), ('E4', 3), ('D4', 1), ('B3', 4), ('B3', 2), ('E4', 1),
            ('F#4', 1), ('G4', 3), ('B4', 1), ('A4', 2), ('G4', 1), ('F#4', 1), ('F#4', 4)]
    clar = [('G4', 2), ('E4', 1), ('C5', 1), ('B4', 3), ('D5', 1), ('A4', 2), ('F#4', 2), ('G4', 3), ('E4', 1),
            ('C5', 2), ('B4', 1), ('A4', 1), ('E4', 2), ('C5', 2), ('B4', 4), ('D#4', 4)]
    bar = 0
    s.bass('basses', ['Em'] * 2, 0, 1, 'whole', 48)
    s.pads('choir', ['Em', 'Em'], 0, 'B3', 3, 40)
    s.drums(0, 2, 'roll', 30)
    bar = 2
    for k, (chords, line, solo) in enumerate(((P, horn, 'horn'), (Q, clar, 'clarinet'), (P, horn, 'clarinet'),
                                               (Q, clar, 'horn'))):
        s.pads('strings', chords, bar, 'B3', 4, 50 + 4 * k)
        s.bass('basses', chords, bar, 1, 'whole', 52)
        s.bass('cellos', chords, bar, 2, 'whole', 48)
        s.melody(solo, line, bar, 0 if solo == 'horn' else (12 if line is horn else 0), vel=66)
        if k == 2:
            s.pads('choir', chords, bar, 'B4', 3, 46)
        s.timpani('timpani', [chords[0]], bar, 'downbeat', 46)
        bar += 8
    s.pads('strings', ['Em', 'Em'], bar, 'B3', 4, 44)
    s.drums(bar, 2, 'roll', 26)
    s.swell('strings', s.bar(bar), s.bar(bar + 2), 100, 10)
    return s


def fields_of_ash():
    """Calm: F major turning to D minor, clarinet and flute, a pastoral elegy."""
    s = Song(66, seed=71)
    for name, prog in (('basses', BASSES), ('strings', SLOW_STRINGS), ('clarinet', CLARINET), ('flute', FLUTE),
                       ('harp', HARP), ('cellos', CELLO), ('bassoon', BASSOON)):
        s.track(name, prog)
    P = ['F', 'Dm', 'Bb', 'C', 'F', 'Am', 'Bb', 'C']
    Q = ['Dm', 'Bb', 'Gm', 'A', 'Dm', 'Gm', 'C', 'F']
    a = [('A4', 2), ('C5', 1), ('F5', 1), ('E5', 2), ('D5', 2), ('D5', 1.5), ('C5', 0.5), ('Bb4', 2), ('G4', 2),
         ('A4', 3), ('C5', 1), ('C5', 2), ('E5', 2), ('D5', 3), ('C5', 0.5), ('Bb4', 0.5), ('C5', 4)]
    b = [('F5', 2), ('E5', 1), ('D5', 1), ('D5', 2), ('Bb4', 2), ('Bb4', 1.5), ('A4', 0.5), ('G4', 2), ('C#5', 2),
         ('E5', 2), ('F5', 1), ('E5', 1), ('D5', 2), ('Bb4', 2), ('G4', 2), ('E5', 2), ('F5', 4)]
    bar = 0
    for k, (chords, line, solo) in enumerate(((P, a, 'clarinet'), (Q, b, 'flute'), (P, a, 'flute'),
                                               (Q, b, 'clarinet'))):
        s.pads('strings', chords, bar, 'C5', 4, 52)
        s.bass('basses', chords, bar, 1, 'whole', 50)
        s.bass('cellos', chords, bar, 2, 'halves', 48)
        s.arp('harp', chords, bar, 'F2', 0.5, 44, up_down=False)
        s.melody(solo, line, bar, 0, vel=68)
        if k >= 2:
            s.melody('bassoon', line, bar, -24, vel=50)
        bar += 8
    s.pads('strings', ['F', 'F'], bar, 'C5', 4, 44)
    s.note('harp', 'F3', s.bar(bar), 4, 50)
    s.swell('strings', s.bar(bar), s.bar(bar + 2), 100, 10)
    return s


def victory():
    s = Song(92, seed=81)
    for name, prog in (('trumpets', TRUMPET), ('horns', HORN), ('trombones', TROMBONE), ('tuba', TUBA),
                       ('strings', STRINGS), ('violins', STRINGS), ('basses', BASSES), ('timpani', TIMPANI),
                       ('choir', CHOIR)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    fan = [('F4', 0.5), ('F4', 0.5), ('Bb4', 1), ('D5', 1), ('F5', 1), ('Eb5', 1.5), ('D5', 0.5), ('C5', 1),
           ('Bb4', 1), ('C5', 3), ('F4', 1), ('Bb4', 1.5), ('C5', 0.5), ('D5', 1), ('Eb5', 1), ('F5', 2), ('G5', 2),
           ('F5', 2), ('Eb5', 1), ('C5', 1), ('D5', 4)]
    ch = ['Bb', 'Eb', 'F', 'F', 'Bb', 'Gm', 'Eb', 'F', 'Bb']
    s.timpani('timpani', ['Bb'], 0, 'roll', 100)
    s.crash(1, 110)
    s.melody('trumpets', fan, 1, 0, vel=98)
    s.melody('horns', fan, 1, -12, vel=92)
    s.melody('violins', fan, 1, 12, vel=82)
    s.pads('trombones', ch[:8], 1, 'F3', 3, 88)
    s.bass('tuba', ch[:8], 1, 1, 'march', 88)
    s.pads('strings', ch[:8], 1, 'D5', 4, 76)
    s.pads('choir', ch[:8], 1, 'F4', 4, 84)
    s.bass('basses', ch[:8], 1, 1, 'halves', 88)
    s.timpani('timpani', ch[:8], 1, 'march', 96)
    s.drums(1, 7, 'march', 82)
    s.crash(5, 100)
    for tr, p in (('trumpets', 'Bb4'), ('horns', 'F4'), ('trombones', 'D3'), ('tuba', 'Bb1'), ('choir', 'D5'),
                  ('strings', 'F5'), ('violins', 'Bb5'), ('basses', 'Bb1')):
        s.note(tr, p, s.bar(9), 8, 104)
    s.crash(9, 120)
    s.note('drums', GONG, s.bar(9), 8, 100)
    s.timpani('timpani', ['Bb', 'Bb'], 9, 'roll', 118)
    s.set_tempo(s.bar(8) + 2, 80)
    return s


def fallen():
    s = Song(52, seed=91)
    for name, prog in (('cello', CELLO), ('strings', SLOW_STRINGS), ('basses', BASSES), ('horn', HORN),
                       ('timpani', TIMPANI), ('choir', OOHS)):
        s.track(name, prog)
    s.track('drums', 0, 9)
    P = ['Dm', 'Bb', 'Gm', 'A', 'Dm', 'Gm', 'A', 'Dm']
    line = [('D4', 2), ('A3', 1), ('D4', 1), ('F4', 3), ('E4', 1), ('D4', 2), ('Bb3', 2), ('A3', 4), ('A3', 1),
            ('D4', 1), ('F4', 1), ('A4', 1), ('G4', 2), ('Bb4', 2), ('A4', 1.5), ('G4', 0.5), ('E4', 2), ('D4', 4)]
    s.drums(0, 1, 'roll', 40)
    s.timpani('timpani', ['Dm'], 0, 'roll', 60)
    s.melody('cello', line, 1, 0, vel=74, legato=1.0)
    s.pads('strings', P, 1, 'F4', 4, 50)
    s.bass('basses', P, 1, 1, 'whole', 52)
    s.pads('choir', P[4:], 5, 'A4', 3, 44)
    s.melody('horn', [('A3', 2), ('D4', 1), ('E4', 1), ('F4', 2), ('E4', 1), ('D4', 1), ('D4', 4)], 9, vel=56)
    s.pads('strings', ['Dm', 'Dm'], 9, 'D4', 4, 44)
    s.bass('basses', ['Dm'], 9, 1, 'whole', 46)
    s.swell('strings', s.bar(9), s.bar(11), 100, 10)
    s.timpani('timpani', ['Dm'], 10, 'downbeat', 44)
    return s


PIECES = {
    'legion': legion, 'iron_advance': iron_advance, 'breakthrough': breakthrough, 'hold_the_line': hold_the_line,
    'quiet_front': quiet_front, 'before_dawn': before_dawn, 'fields_of_ash': fields_of_ash, 'victory': victory,
    'fallen': fallen,
}


# ---------------------------------------------------------------------------
# The hall and the master
# ---------------------------------------------------------------------------
def hall_ir(rt60=2.8, seed=7):
    r = np.random.default_rng(seed)
    n = int(rt60 * 1.4 * SR)
    t = np.arange(n) / SR
    decay = np.exp(-6.91 * t / rt60)
    out = []
    for ch in range(2):
        x = r.standard_normal(n) * decay
        # Air and curtains: the high end dies away faster than the low.
        lo = signal.sosfilt(signal.butter(2, 1200, 'lp', fs=SR, output='sos'), x)
        hi = x - lo
        x = lo + hi * np.exp(-t / (rt60 * 0.18))
        x[:int(0.022 * SR)] = 0  # pre-delay
        for d, g in ((0.031, 0.5), (0.047, 0.4), (0.061, 0.35), (0.083, 0.25)):  # early reflections
            i = int((d + 0.003 * ch) * SR)
            x[i] += g * (1 if r.random() > 0.5 else -1)
        out.append(x / np.sqrt(np.sum(x ** 2)))
    return np.stack(out, 1)


def master(dry, wet=0.32):
    ir = hall_ir()
    rev = np.stack([signal.fftconvolve(dry[:, c], ir[:, c]) for c in range(2)], 1)
    n = len(rev)
    dry = np.concatenate([dry, np.zeros((n - len(dry), 2))])
    x = dry * (1 - wet * 0.5) + rev * wet * 1.6
    # Gentle low shelf for weight, a touch less glare.
    lo = np.stack([signal.sosfilt(signal.butter(2, 120, 'lp', fs=SR, output='sos'), x[:, c]) for c in range(2)], 1)
    x = x + lo * 0.25
    hi = np.stack([signal.sosfilt(signal.butter(2, 6000, 'hp', fs=SR, output='sos'), x[:, c]) for c in range(2)], 1)
    x = x - hi * 0.2
    # Loudness: aim the body of the piece at about -18 dBFS RMS, peaks soft-limited at -1 dBFS.
    rms = np.sqrt(np.mean(x ** 2)) + 1e-9
    x *= 10 ** (-18 / 20) / rms
    ceiling = 10 ** (-1 / 20)
    x = np.where(np.abs(x) > 0.7 * ceiling,
                 np.sign(x) * (0.7 * ceiling + 0.3 * ceiling * np.tanh((np.abs(x) - 0.7 * ceiling) / (0.3 * ceiling))), x)
    # Trim the hall's tail once it has died away, fade the last moment.
    env = np.abs(x).max(1)
    loud = np.nonzero(env > 10 ** (-60 / 20))[0]
    end = min(len(x), loud[-1] + SR // 4) if len(loud) else len(x)
    x = x[:end]
    f = min(len(x), SR)
    x[-f:] *= np.linspace(1, 0, f)[:, None] ** 2
    return x


def render(name, song, out_dir):
    with tempfile.TemporaryDirectory() as tmp:
        mid = os.path.join(tmp, name + '.mid')
        wav = os.path.join(tmp, name + '.wav')
        song.save(mid)
        subprocess.run(['fluidsynth', '-ni', '-q', '-R', '0', '-C', '0', '-g', '0.5', '-r', str(SR), '-F', wav, SF2, mid],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        sr, x = wavfile.read(wav)
        assert sr == SR
        x = x.astype(np.float64) / 32768.0
        x = master(x)
        wavfile.write(wav, SR, (np.clip(x, -1, 1) * 32767).astype(np.int16))
        out = os.path.join(out_dir, name + '.ogg')
        subprocess.run(['oggenc', '-Q', '-q', '4', '-o', out, wav], check=True)
    return len(x) / SR


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'content/audio'
    names = sys.argv[2:] or list(PIECES)
    out_dir = os.path.join(root, 'music')
    os.makedirs(out_dir, exist_ok=True)
    for n in names:
        sec = render(n, PIECES[n](), out_dir)
        print(f'{n}: {sec:.0f} s', flush=True)


if __name__ == '__main__':
    main()
