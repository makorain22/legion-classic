#!/usr/bin/env python3
"""Turns field recordings into the game's sounds (Pioneer part 7).

Input: a folder (or .zip) of recordings with a manifest.json (id, file, user,
title, license, page) - the set gathered from Freesound for Legion Classic.
Output, under content/audio/:
  rec/<name>.ogg        the cut, cleaned and levelled sounds
  RECORDINGS.md         where every recording came from and its licence
and the "sounds", "ambience" and "voices" of audio.json rewritten to use them.

What it does to a recording, by recipe:
  shots   cut a take with several shots or bursts into one sound per shot
          (onsets found from the energy envelope), each with its tail, faded
  one     a single sound: leading silence trimmed, the tail faded
  loop    a steady stretch made into a seamless loop (an equal-power
          crossfade of its end into its start)
  voice   a spoken line: trimmed, levelled, mono
Every sound is resampled to 48 kHz, made mono unless it is an ambience bed,
high-passed below 30 Hz (rumble from the recorder), and levelled to the
loudness of the sound it replaces, so the mix keeps its balance.

Needs numpy, scipy and ffmpeg (with libvorbis). Deterministic.

    python3 tools/audio/recordings.py <recordings dir or zip> [--content content]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

import numpy as np
from scipy import signal

RATE = 48000


def load(path, channels=1):
    raw = subprocess.run(['ffmpeg', '-v', 'quiet', '-i', path, '-f', 'f32le', '-ac', str(channels), '-ar', str(RATE), '-'],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, np.float32).astype(np.float64)
    return x.reshape(-1, channels) if channels > 1 else x


def write_ogg(path, x, quality=5):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    x = np.clip(x, -1, 1).astype(np.float32)
    ch = 1 if x.ndim == 1 else x.shape[1]
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-f', 'f32le', '-ac', str(ch), '-ar', str(RATE), '-i', '-',
                    '-c:a', 'libvorbis', '-q:a', str(quality), path], input=x.tobytes(), check=True)


def mono(x):
    return x if x.ndim == 1 else x.mean(axis=1)


def highpass(x, hz=30):
    sos = signal.butter(2, hz, 'highpass', fs=RATE, output='sos')
    return signal.sosfiltfilt(sos, x, axis=0)


def lowpass(x, hz):
    sos = signal.butter(2, hz, 'lowpass', fs=RATE, output='sos')
    return signal.sosfiltfilt(sos, x, axis=0)


def envelope(x, ms=5):
    n = max(1, int(RATE * ms / 1000))
    e = np.sqrt(np.convolve(mono(x) ** 2, np.ones(n) / n, mode='same'))
    return e


def trim(x, floor_db=-50, pre_ms=4):
    e = envelope(x, 3)
    thr = e.max() * 10 ** (floor_db / 20)
    idx = np.nonzero(e > thr)[0]
    if len(idx) == 0:
        return x
    a = max(0, idx[0] - int(RATE * pre_ms / 1000))
    b = min(len(x), idx[-1] + int(RATE * 0.02))
    return x[a:b]


def fade(x, fade_in_ms=2, fade_out_ms=120):
    x = x.copy()
    n_in = min(len(x) // 4, int(RATE * fade_in_ms / 1000))
    n_out = min(len(x) // 2, int(RATE * fade_out_ms / 1000))
    shape_in = np.linspace(0, 1, n_in) if n_in else None
    shape_out = np.cos(np.linspace(0, np.pi / 2, n_out)) ** 2 if n_out else None
    if x.ndim == 1:
        if n_in: x[:n_in] *= shape_in
        if n_out: x[-n_out:] *= shape_out
    else:
        if n_in: x[:n_in] *= shape_in[:, None]
        if n_out: x[-n_out:] *= shape_out[:, None]
    return x


def onsets(x, min_gap=0.25, rise_db=14):
    """Starts of shots: where the short envelope jumps well above the recent level."""
    e = envelope(x, 2) + 1e-7
    slow = envelope(x, 60) + 1e-7
    jump = 20 * np.log10(e / np.maximum(slow, e.max() * 1e-3))
    loud = e > e.max() * 0.08
    found = []
    i = 0
    gap = int(min_gap * RATE)
    cand = np.nonzero((jump > rise_db * 0.5) & loud)[0]
    for c in cand:
        if found and c - found[-1] < gap:
            continue
        found.append(c)
    # Keep the attack itself: step back to where the envelope starts to rise.
    out = []
    for c in found:
        a = c
        while a > 0 and e[a - 1] < e[a] and c - a < int(0.01 * RATE):
            a -= 1
        out.append(max(0, a - int(0.002 * RATE)))
    return out


def cut_shots(x, max_len, min_len=0.15, keep=8, min_gap=0.25):
    """One sound per shot: from each onset to the next (or max_len), longest-tailed loudest first."""
    starts = onsets(x, min_gap=min_gap)
    pieces = []
    for k, a in enumerate(starts):
        b = starts[k + 1] if k + 1 < len(starts) else len(x)
        b = min(b, a + int(max_len * RATE))
        if b - a < int(min_len * RATE):
            continue
        p = x[a:b]
        pieces.append((float(np.abs(p).max()), p))
    pieces.sort(key=lambda t: -t[0])
    # The loudest few, but spread over the take (not all from one burst).
    return [p for _, p in pieces[:keep]]


def loop(x, seconds, start=0.15, xfade=1.5):
    """A steady stretch, its end crossfaded into its start: plays round without a seam."""
    n = int(seconds * RATE)
    xf = int(xfade * RATE)
    a = int(len(x) * start)
    if a + n + xf > len(x):
        a = max(0, len(x) - n - xf)
    if a + n + xf > len(x):  # too short: as long as it can be
        n = max(int(RATE), len(x) - a - xf)
    body = x[a:a + n].copy()
    tail = x[a + n:a + n + xf]
    t = np.linspace(0, np.pi / 2, len(tail))
    fin, fout = np.sin(t), np.cos(t)
    if body.ndim == 2:
        fin, fout = fin[:, None], fout[:, None]
    body[:len(tail)] = body[:len(tail)] * fin + tail * fout
    return body


def rms(x):
    return float(np.sqrt(np.mean(mono(x) ** 2)) + 1e-9)


def active_rms(x):
    """Loudness of the part that sounds (a shot's body, not its silence)."""
    m = mono(x)
    e = envelope(m, 20)
    sel = e > e.max() * 0.1
    return float(np.sqrt(np.mean(m[sel] ** 2)) + 1e-9) if sel.any() else rms(x)


def level(x, target_active_rms, peak=0.89):
    g = target_active_rms / active_rms(x)
    y = x * g
    p = np.abs(y).max()
    if p > peak:
        y *= peak / p
    return y


def pitch(x, factor):
    """Lower (factor < 1) or raise a sound by resampling: a heavier gun from a lighter one."""
    n = int(round(len(x) / factor))
    return signal.resample(x, n, axis=0)


def mix(*parts):
    n = max(len(p) for p, _ in parts)
    ch = max((1 if p.ndim == 1 else p.shape[1]) for p, _ in parts)
    out = np.zeros((n, ch)) if ch > 1 else np.zeros(n)
    for p, g in parts:
        if ch > 1 and p.ndim == 1:
            p = np.repeat(p[:, None], ch, axis=1)
        out[:len(p)] += p * g
    return out


def stereo(x):
    return x if x.ndim == 2 else np.repeat(x[:, None], 2, axis=1)


def loop_stereo_mix(srcs, seconds):
    """Layers several beds (each made into a loop of the same length) into one stereo loop."""
    beds = [stereo(loop(highpass(load_src(s, 2)), seconds, start=st)) * g for s, g, st in srcs]
    n = min(len(b) for b in beds)
    return sum(b[:n] for b in beds)


# ---------------------------------------------------------------------------
SRC = None  # the recordings folder


def load_src(name, channels=1):
    return load(os.path.join(SRC, name + '.ogg'), channels)


def main():
    global SRC
    ap = argparse.ArgumentParser()
    ap.add_argument('recordings')
    ap.add_argument('--content', default='content')
    args = ap.parse_args()
    tmp = None
    SRC = args.recordings
    if SRC.endswith('.zip'):
        tmp = tempfile.mkdtemp()
        with zipfile.ZipFile(SRC) as z:
            z.extractall(tmp)
        SRC = tmp
    audio = os.path.join(args.content, 'audio')
    manifest = json.load(open(os.path.join(SRC, 'manifest.json')))
    by_name = {m['file'][:-4]: m for m in manifest}
    bank = json.load(open(os.path.join(audio, 'audio.json')))
    old_sounds = bank['sounds']
    used = set()   # recordings that went into the game
    made = {}      # event -> [files]
    far = {}       # event -> [far files]

    def target_of(event):
        """The loudness of the synthesised sound this event had: the mix keeps its balance."""
        files = old_sounds.get(event, {}).get('files', [])
        vals = [active_rms(load(os.path.join(audio, f))) for f in files[:3] if os.path.exists(os.path.join(audio, f))]
        return float(np.median(vals)) if vals else 0.12

    targets = {}

    def tgt(event):
        if event not in targets:
            targets[event] = target_of(event)
        return targets[event]

    def emit(event, name, x, kind='files', target=None):
        rel = f'rec/{name}.ogg'
        y = level(x, target if target is not None else tgt(event))
        write_ogg(os.path.join(audio, rel), y)
        (far if kind == 'far' else made).setdefault(event, []).append(rel)

    def shots(event, src, max_len, keep=3, min_gap=0.25, kind='files', p=1.0, lp=None, tail_ms=150, target=None, prefix=None):
        x = highpass(load_src(src))
        if p != 1.0:
            x = pitch(x, p)
        if lp:
            x = lowpass(x, lp)
        pieces = cut_shots(x, max_len, keep=keep, min_gap=min_gap)
        if not pieces:
            pieces = [trim(x)[:int(max_len * RATE)]]
        for k, piece in enumerate(pieces):
            emit(event, f'{prefix or src}_{k}', fade(piece, 1, tail_ms), kind, target)
        used.add(src)

    def one(event, src, max_len=6.0, kind='files', p=1.0, lp=None, name=None, start=0.0, target=None, tail_ms=250):
        x = highpass(load_src(src))
        if start:
            x = x[int(start * RATE):]
        if p != 1.0:
            x = pitch(x, p)
        if lp:
            x = lowpass(x, lp)
        x = trim(x)[:int(max_len * RATE)]
        emit(event, name or src, fade(x, 1, tail_ms), kind, target)
        used.add(src)

    # --- Small arms --------------------------------------------------------------
    shots('rifle', 'rifle_mauser', 1.6, keep=2)
    shots('rifle', 'rifle_garand', 1.6, keep=1)
    shots('rifle', 'rifle_garand2', 1.6, keep=1)
    shots('rifle', 'rifle_shots_craig', 1.4, keep=3, min_gap=0.4)
    shots('rifle_smle', 'rifle_enfield', 1.6, keep=1, target=tgt('rifle'))
    shots('rifle_smle', 'rifle_enfield2', 1.6, keep=2, target=tgt('rifle'))
    shots('rifle', 'battle_rifles', 1.2, keep=4, min_gap=0.5, kind='far', prefix='rifle_far')
    shots('sniper', 'sniper_field', 2.0, keep=1)
    shots('sniper', 'sniper_shots', 2.0, keep=3, min_gap=0.8)
    shots('mg', 'mg42', 1.5, keep=3, min_gap=0.6)
    shots('mg', 'mg_rapid', 1.4, keep=3, min_gap=0.7)
    shots('mg', 'mg_burst', 1.5, keep=1)
    shots('lmg', 'lmg_long', 1.4, keep=4, min_gap=0.7)
    shots('lmg', 'mg_burst_clean', 1.5, keep=1)
    shots('lmg', 'bren_distant', 1.5, keep=3, min_gap=0.7, kind='far', prefix='lmg_far')
    shots('mg', 'battle_mg', 1.4, keep=3, min_gap=0.7, kind='far', prefix='mg_far')
    shots('hmg', 'hmg_50cal', 1.5, keep=3, min_gap=0.7)
    shots('hmg', 'hmg_50cal_2', 1.5, keep=2, min_gap=0.7)
    shots('hmg', 'hmg_dshk', 1.0, keep=1)
    shots('hmg', 'mg_bursts_craig', 1.5, keep=2, min_gap=0.7)
    # An anti-tank rifle: a big rifle's crack, lower and longer.
    shots('at_rifle', 'rifle_enfield', 2.0, keep=1, p=0.78, prefix='at_rifle_enfield')
    shots('at_rifle', 'sniper_field', 2.2, keep=1, p=0.82, prefix='at_rifle_sniper')
    shots('at_launcher', 'bazooka_shot', 2.5, keep=1)
    shots('at_launcher', 'bazooka_craig', 2.5, keep=3, min_gap=1.2)

    # --- Guns ------------------------------------------------------------------------
    shots('cannon_small', 'atgun_37mm', 2.5, keep=3, min_gap=1.0)
    shots('cannon_small', 'atgun_37mm_squad', 2.5, keep=2, min_gap=1.0)
    shots('cannon_6pdr', 'atgun_6pdr', 3.0, keep=3, min_gap=1.0, target=tgt('cannon_small'))
    shots('cannon_medium', 'tank_shots', 3.0, keep=3, min_gap=1.0)
    shots('cannon_medium', 'tanks_shooting', 3.0, keep=3, min_gap=1.0)
    shots('cannon_large', 'tanks_shooting', 3.5, keep=3, min_gap=1.0, p=0.85, prefix='tanks_shooting_heavy')
    shots('cannon_large', 'tank_shots', 3.5, keep=2, min_gap=1.0, p=0.8, prefix='tank_shots_heavy')
    for ev in ('cannon_small', 'cannon_6pdr', 'cannon_medium', 'cannon_large'):
        shots(ev, 'tank_shots_distant', 3.0, keep=3, min_gap=1.0, kind='far', prefix=f'{ev}_far', target=tgt('cannon_medium') * 0.8)
    shots('howitzer', 'artillery_shots', 4.0, keep=3, min_gap=1.5)
    shots('howitzer', 'artillery_gunfire', 4.0, keep=2, min_gap=1.5)
    shots('howitzer', 'artillery_k55', 4.0, keep=2, min_gap=1.5)
    one('howitzer', 'artillery_69th', 4.0)
    shots('howitzer', 'mortar_shots', 3.0, keep=2, min_gap=1.5, prefix='mortar')
    one('rocket', 'rocket_launch', 3.0, start=0.0)
    shots('rocket', 'rocket_launch', 2.5, keep=2, min_gap=1.0, prefix='rocket_salvo')

    # --- Impacts -----------------------------------------------------------------------
    for k, n in enumerate(('expl_small_1', 'expl_small_2', 'expl_small_3', 'expl_small_4')):
        one('explosion_small', n, 2.5)
    for n in ('expl_1', 'expl_2', 'expl_3'):
        one('explosion_medium', n, 4.0)
    for n in ('expl_big', 'expl_huge'):
        one('explosion_large', n, 6.0)
    one('explosion_large', 'expl_tank', 5.0)
    for ev in ('explosion_medium', 'explosion_large'):
        one(ev, 'expl_distant_1', 4.0, kind='far', name=f'{ev}_far_1', target=tgt(ev) * 0.8)
        one(ev, 'expl_distant_2', 5.0, kind='far', name=f'{ev}_far_2', target=tgt(ev) * 0.8)
        shots(ev, 'expl_distant_many', 4.0, keep=2, min_gap=2.0, kind='far', prefix=f'{ev}_far_many', target=tgt(ev) * 0.8)
    one('explosion_large', 'expl_distant_bomb', 6.0, kind='far', target=tgt('explosion_large') * 0.8)
    for n in ('incoming_1', 'incoming_2', 'incoming_3', 'incoming_4'):
        one('shell_incoming', n, 2.5, target=0.08)
    shots('ricochet', 'ricochets_mg', 1.2, keep=4, min_gap=0.4)
    one('ricochet', 'ricochet_1', 1.5)
    one('ricochet', 'ricochet_2', 1.5)
    one('bullet_flyby', 'flyby', 0.6, target=0.06)
    one('armour_hit', 'metal_hit_1', 1.2)
    shots('armour_hit', 'metal_hits', 1.2, keep=3, min_gap=0.4)
    one('armour_hit', 'metal_hit_resonant', 2.0)
    one('vehicle_destroyed', 'expl_tank', 5.0, name='vehicle_destroyed_tank')
    one('vehicle_destroyed', 'expl_huge', 6.0, name='vehicle_destroyed_huge')
    one('vehicle_destroyed', 'expl_big', 6.0, name='vehicle_destroyed_big')
    for n in ('collapse_wood_1', 'collapse_wood_2', 'collapse_3', 'collapse_4'):
        one('collapse', n, 7.0)

    # --- Men and machines ------------------------------------------------------------------
    for k, st in enumerate((0.1, 0.4, 0.7)):
        one('construction', 'hammer_wood', 3.0, start=st * 25, name=f'hammer_wood_{k}', tail_ms=400)
    one('construction', 'shovel_dig', 3.0, start=2.0, tail_ms=400)
    one('whistle', 'whistle_trench', 2.0)
    one('whistle', 'whistle_police', 2.0)
    one('engine_rev', 'tank_short', 4.0)
    one('engine_rev', 'truck_pullaway', 4.0, start=3.0)
    one('engine_rev', 'tank_driving', 4.0)
    for k, n in enumerate(('radio_static_1', 'radio_static_2', 'radio_static_3')):
        one('radio', n, 0.45, tail_ms=80)

    def bed(event, layers, seconds, name, channels=1):
        """A loop under the battle: an engine, marching feet, a fire."""
        parts = []
        for src, g, st in layers:
            x = highpass(load_src(src, channels))
            parts.append((loop(x, seconds, start=st), g))
            used.add(src)
        n = min(len(p) for p, _ in parts)
        y = sum(p[:n] * g for p, g in parts)
        rel = f'rec/{name}.ogg'
        write_ogg(os.path.join(audio, rel), level(y, tgt(event)))
        made.setdefault(event, []).append(rel)

    bed('tank_engine', [('engine_churchill', 1.0, 0.2), ('tracks', 0.6, 0.1)], 8.0, 'tank_engine')
    bed('truck_engine', [('truck_diesel_large', 1.0, 0.3)], 8.0, 'truck_engine')
    bed('march', [('march_gravel', 1.0, 0.2)], 10.0, 'march')
    bed('fire', [('fire_crackling', 0.8, 0.2), ('fire_roaring', 0.6, 0.3)], 10.0, 'fire')

    # --- The land: ambience beds, stereo, seamless ------------------------------------------
    amb = {}

    def ambience(biome, layers, seconds=48.0):
        parts = []
        for src, g, st in layers:
            x = highpass(load_src(src, 2), 40)
            parts.append((loop(x, seconds, start=st, xfade=3.0), g))
            used.add(src)
        n = min(len(p) for p, _ in parts)
        y = sum(p[:n] * g for p, g in parts)
        old = os.path.join(audio, bank['ambience'][biome])
        y = y * (rms(load(old, 2)) / rms(y))
        p = np.abs(y).max()
        if p > 0.89:
            y *= 0.89 / p
        rel = f'rec/ambience_{biome}.ogg'
        write_ogg(os.path.join(audio, rel), y, quality=4)
        amb[biome] = rel

    ambience('temperate', [('amb_countryside', 1.0, 0.1), ('amb_forest', 0.5, 0.2)])
    ambience('winter', [('amb_winter_forest', 1.0, 0.05), ('amb_winter_wind', 0.6, 0.1)])
    ambience('desert', [('amb_desert', 1.0, 0.1), ('amb_desert_wind', 0.5, 0.1)])
    ambience('tropical', [('amb_jungle', 1.0, 0.1), ('amb_rainforest', 0.5, 0.1)])
    ambience('urban', [('amb_city_dawn', 1.0, 0.0), ('amb_city_park', 0.5, 0.1)], seconds=24.0)
    ambience('battle', [('battle_distant_rumble', 1.0, 0.1), ('battle_bombardment', 0.6, 0.1), ('battle_tanks', 0.5, 0.05)], seconds=40.0)

    # --- Voices --------------------------------------------------------------------------------
    voice_files = {}

    def line(src, name=None, start=0.0, max_len=4.0):
        x = highpass(load_src(src), 90)
        if start:
            x = x[int(start * RATE):]
        x = trim(x, -45)[:int(max_len * RATE)]
        rel = f'rec/voice/{name or src}.ogg'
        write_ogg(os.path.join(audio, rel), level(fade(x, 2, 120), 0.16))
        used.add(src)
        voice_files[name or src] = rel
        return rel

    def L(src, text, meaning='', **kw):
        return {'file': line(src, **kw), 'text': text, **({'meaning': meaning} if meaning else {})}

    voices = {
        'en': {'language': 'en', 'speaker': 'Sergeant', 'fallback': 'battle', 'cues': {
            'move': {'cooldown': 7, 'priority': 1, 'chance': 0.7, 'radio': True,
                     'lines': [L('vo_en_move', 'Move!'), L('vo_en_gogogo', 'Go, go, go!'), L('vo_en_yes', 'Yes!')]},
            'attack': {'cooldown': 7, 'priority': 2, 'chance': 0.8, 'radio': True,
                       'lines': [L('vo_en_covering_fire', 'Covering fire!'), L('vo_en_guns_up', 'Guns up!')]},
            'ready': {'cooldown': 10, 'priority': 1, 'radio': True, 'lines': [L('vo_en_yes', 'Yes!', name='vo_en_yes_ready')]},
            'incoming': {'cooldown': 6, 'priority': 4,
                         'lines': [L('vo_en_mortars_incoming', 'Mortars incoming!'), L('vo_en_take_cover', 'Take cover!'),
                                   L('vo_en_take_cover2', 'Take cover!')]},
            'man_down': {'cooldown': 8, 'priority': 3, 'lines': [L('vo_en_medic', 'Medic!')]},
        }},
        'de': {'language': 'de', 'speaker': 'Unteroffizier', 'fallback': 'battle', 'cues': {
            'move': {'cooldown': 7, 'priority': 1, 'chance': 0.7, 'radio': True,
                     'lines': [L('vo_de_los', 'Los!', 'Go!'), L('vo_de_schnell', 'Schnell, schnell!', 'Quick, quick!')]},
            'attack': {'cooldown': 7, 'priority': 2, 'chance': 0.8,
                       'lines': [L('vo_de_los_los_los', 'Los, los, los!', 'Go, go, go!')]},
        }},
        'ru': {'language': 'ru', 'speaker': 'Serzhant', 'fallback': 'battle', 'cues': {
            'move': {'cooldown': 7, 'priority': 1, 'chance': 0.7, 'radio': True,
                     'lines': [L('vo_ru_davai', 'Davai!', 'Come on!')]},
            'attack': {'cooldown': 8, 'priority': 2, 'chance': 0.8, 'lines': [L('vo_ru_ura', 'Urraa!', 'Hurrah!')]},
            'ready': {'cooldown': 12, 'priority': 1, 'radio': True,
                      'lines': [L('vo_ru_comrade_commander', 'Tovarishch komandir!', 'Comrade commander!')]},
        }},
        # Sounds with no words, for every side: what a man makes when he is hit.
        'battle': {'language': '', 'speaker': '', 'cues': {
            'man_down': {'cooldown': 8, 'priority': 3, 'lines': [L('vo_pain', '(a cry of pain)')]},
        }},
    }

    # --- Write the bank -------------------------------------------------------------------------
    sounds = bank['sounds']
    defaults = {
        'rifle_smle': dict(sounds['rifle']),
        'cannon_6pdr': dict(sounds['cannon_small']),
        'shell_incoming': {'bus': 'sfx', 'volume': 0.5, 'range': 10, 'priority': 3, 'reverb': 0.1, 'pitch': 0.04,
                           'perSecond': 3, 'limit': 2},
        'bullet_flyby': {'bus': 'sfx', 'volume': 0.4, 'range': 4, 'priority': 1, 'reverb': 0.05, 'pitch': 0.08,
                         'perSecond': 6, 'limit': 3},
    }
    for ev, d in defaults.items():
        sounds.setdefault(ev, d)
    for ev, files in made.items():
        sounds.setdefault(ev, {'bus': 'sfx'})
        sounds[ev]['files'] = files
    for ev, files in far.items():
        sounds[ev]['far'] = files
        sounds[ev].setdefault('farBelow', 0.3)
    bank['ambience'] = amb
    bank['voices'] = voices
    bank['factions'] = {'gbr': 'en', 'ger': 'de', 'rus': 'ru', 'default': 'en'}
    with open(os.path.join(audio, 'audio.json'), 'w') as f:
        json.dump(bank, f, indent=1, ensure_ascii=False)
        f.write('\n')

    # --- Where they came from -------------------------------------------------------------------
    rows = []
    for name in sorted(used):
        m = by_name[name]
        lic = 'CC0 1.0' if 'publicdomain/zero' in m['license'] else ('CC BY ' + m['license'].rstrip('/').split('/')[-1])
        rows.append(f"| {name} | {m['title'].replace('|', '/')} | {m['user']} | [{m['id']}]({m['page']}) | {lic} |")
    with open(os.path.join(audio, 'RECORDINGS.md'), 'w') as f:
        f.write('# Recordings\n\n'
                'The sounds in `rec/` are cut, cleaned and levelled from these field recordings by\n'
                '`tools/audio/recordings.py`. All are from [Freesound](https://freesound.org). CC0 recordings are\n'
                'public domain; CC BY ones are used under the Creative Commons Attribution licence named, and\n'
                'are credited here to their authors (the "user" column), with a link to the original. Changes\n'
                'made: cut into single sounds or loops, filtered, resampled, levelled, some lowered in pitch\n'
                'or layered with others.\n\n'
                '| Recording | Title | User | Freesound | Licence |\n| --- | --- | --- | --- | --- |\n')
        f.write('\n'.join(rows) + '\n')
    print(f'{sum(len(v) for v in made.values())} sounds, {sum(len(v) for v in far.values())} far, {len(amb)} beds, '
          f'{len(voice_files)} voice lines from {len(used)} recordings')
    if tmp:
        shutil.rmtree(tmp)


if __name__ == '__main__':
    main()
