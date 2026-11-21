# Sound and music: where it comes from

The interface sounds and the music were made for Legion Classic; the battle's
sounds, the ambience and the voices are field recordings under free licences.

## Battle sounds, ambience and voices (`rec/`)

Cut, cleaned and levelled by `tools/audio/recordings.py` from field
recordings on [Freesound](https://freesound.org), each **CC0 1.0** (public
domain) or **CC BY** (credit to its author). Every recording, its author,
link and licence: **[RECORDINGS.md](RECORDINGS.md)**. That list is the credit
the CC BY licences ask for; keep it with the game.

## Interface sounds (`sfx/ui_*.ogg`)

Synthesised from scratch by `tools/audio/sfx.py` (shaped noise, swept tones,
inharmonic partials). No recordings or samples are used. Released under
**CC0 1.0** (public domain dedication). (Until part 7 of Milestone 9.13 the
battle's sounds were synthesised the same way.)

## Music (`music/`)

Composed for Legion Classic in `tools/audio/music.py` (every melody, harmony,
orchestration and dynamic is written there) and performed by the
**FluidR3 GM** soundfont through FluidSynth, then given a concert-hall reverb
and mastered by the same script.

- The compositions and these recordings: © the Legion Classic authors, all
  rights reserved like the rest of the game for now (see the README).
- FluidR3 GM soundfont: © 2000–2002, 2008 Frank Wen, released under the MIT
  licence ("I hereby release Fluid under the MIT license"). Its samples are
  rendered into the recordings; the soundfont itself is not shipped.

| Track | Mood | Length |
| --- | --- | --- |
| Legion | main menu | 1:49 |
| Quiet Front | calm | 2:21 |
| Before Dawn | calm | 2:36 |
| Fields of Ash | calm | 2:04 |
| Iron Advance | battle | 1:46 |
| Breakthrough | battle | 1:13 |
| Hold the Line | battle | 1:37 |
| Victory | victory | 0:32 |
| Fallen | defeat | 0:58 |

## Adding sounds

New files must be CC0, CC BY or made for the game - free to ship with it. A
recording goes through `tools/audio/recordings.py` (add it to the manifest
and a recipe) so it lands in RECORDINGS.md with its source and licence;
anything else is recorded in this file. Licences that forbid passing the file
on by itself (Pixabay's, for one) don't qualify.
