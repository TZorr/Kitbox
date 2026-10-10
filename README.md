# Kitbox

A 16-pad drum sampler for macOS - AU (aumu/Ktbx/Tzor), VST3 and a standalone app, built with JUCE 9.

Its sibling [Transmute](https://github.com/TZorr/Transmute) rebuilds recorded
drum hits as clean synth voices and hands a whole kit over in one step:
**Export Kitbox Kit…** writes every drum onto its own Kitbox pad, with pan and
notes. Since 0.7.0 its engine is in Kitbox too: each pad plays its sample or
Transmute's synth of it.

<img src="screenshot.png" width="700" alt="Kitbox in Logic Pro, with SMP / SYN on every pad">

## Install

Download `Kitbox.0.7.1.pkg` from
[Releases](https://github.com/TZorr/Kitbox/releases) and run it; choose AU,
VST3 or both - they go into `/Library/Audio/Plug-Ins`. Apple Silicon, macOS
26.5 or later. The installer is unsigned, so Gatekeeper refuses it at first:
right-click it and choose Open, or allow it under System Settings › Privacy &
Security.

**Demo kit:** [`Kits/Transmute Kit.aupreset`](Kits/Transmute%20Kit.aupreset)
(also attached to the release) - sixteen DMX drum samples, turned into
Transmute synths right in Kitbox with **SYN**; each pad's **SMP / SYN** plays
the original or the synth. Open it with **Load Kit**,
or put it in `~/Music/Audio Music Apps/Plug-In Settings/Kitbox/` and Logic's
settings menu lists it.

## Signal path

    pad sample (summed to mono on load)
      -> pitch (Tune, Fine; resampled on the fly, 4-point Hermite)
      -> filter: TPT state-variable, LP 12/24, BP, HP 12/24, with Drive and
         a saturating resonance; filter envelope (Attack, Decay, Amount)
      -> amp envelope (Attack, Hold, Decay; Decay "Full" = play the sample out)
      -> Level / Pan (equal power)  -> stereo mix
      -> Reverb / Delay / Mod sends (mono, post-level, pre-pan)

    Three send slots with Rackbox's effects (copied into Source/Engine/Fx),
    each knob-selectable by Type; a mono send enters as a centred stereo
    signal and returns in stereo:

    Reverb   Plate / Room / Hall                     Size, Damp
    Delay    Digital / Tape / Ping-Pong, tempo-synced Time (note value), Feedback
    Mod      Chorus / Phaser / Flanger / Tremolo     Rate, Depth
             Shifter (frequency shifter, 0.3.0)      Shift, Feedback

    Switching Type crossfades over 30 ms. Kits saved before 0.2.0 are
    translated on load (Source/Engine/FxMigration.h).

64 voices, 48 sounding; a stolen voice fades over 2 ms. Hits are one-shots and
sample-accurate.

## Routing (per pad)

- **Note** - any MIDI note; default 36-51 (C1-D#2). Several pads on one note
  play together (layering). **Learn Note**: select a pad, press the button,
  play a key.
- **Choke** - groups 1-8. A hit fades every voice in its group over 5 ms, the
  pad's own included (an open hat chokes itself). Pads layered on one note in
  the same group do not choke each other.
- **Output** - Main or Out 1-16. The plugin has sixteen stereo aux outputs,
  off by default; use Logic's *Multi Output* variant (or enable outputs in
  your DAW). A pad routed to an output that is not enabled plays on Main.
  Effect returns always go to Main; Master scales every output.

Dragging one pad onto another swaps sample, knobs, choke and output - the
note stays with the pad's place.

**Humanize** (0-100 %, 50 % at start) varies every hit by up to pitch +-6 ct,
filter +-10 %, level +-2 dB, start +-1 ms and pan +-4 % at 100 %; 50 % is what
100 % was before 0.6.0 (+-3 ct, 5 %, 1 dB, 0.5 ms, 2 %), and kits and sessions
saved before then load with their Humanize halved, so they sound as they did.

## Pads

- Click to play (higher on the pad = louder), right-click to load, rename or
  clear, or to choose Sample, Auto or a Transmute model (see below).
- **Rename...** renames the pad's sample: the name is saved with the kit and
  the session, moves with the pad when it is swapped, and is the file name a
  drag out of the pad hands over (the extension stays).
- Drop audio files on a pad; several files fill the following pads in name order.
- Drag a pad onto another to swap them (sample and knobs).
- **Export Samples...** (right-click, any pad) writes the whole kit's files
  into a new folder: `Original/` with every pad's sample as it came, `Synth/`
  with every synth and its `.drumparams`, named `01 Kick 01.wav` and so on,
  as inside the kit container.
- Drag a pad out of the window to hand its file to the Finder or a DAW - the
  sample, or the synth when the pad plays one.

## Transmute

Kitbox carries [Transmute](https://github.com/TZorr/Transmute)'s engine: the
analysis that measures a recorded hit, the fit that rebuilds it from a small
synth voice, and the synth itself - ported to C++ and held against the Swift
original, which it matches bit for bit on Transmute's 24 test samples
(analysis, fit and render; see `Verification/TransmuteCheck.cpp`). No knobs
for it: Transmute's sliders stay in Transmute.

- **Right-click a pad** and choose what it plays: **Sample**, **Auto** (the
  model the hit suggests, named once it has been analysed), or one of the five
  models - **Kick / Tom**, **Snare**, **Hi-Hat**, **Modal**, **Clap**.
- **SMP / SYN**, top right on each pad beside the note, switches it between
  its sample and its synth with one click (back to the model chosen last, Auto
  otherwise). It is filled once the synth plays, an outline while it fits.
- **SYN**, top right in the display, sets every pad that holds a sample to
  Auto, once. While it works it counts the pads done (`SYN 3/10`).
- A pad plays its sample until its fit is in (one to four seconds, several
  pads at once), then the synth; the display says `fitting... 40 %`, with a
  bar crossing the waveform as far as the fit has got, then `synth Snare`. Back to Sample and again to the same model switch at once.
- The synth is rendered at the sample's own rate, as Transmute exports, and
  every Kitbox knob acts on it as on any sample. A new sample dropped on a pad
  set to Auto or a model is fitted in turn.
- Up to 10 seconds, Transmute's limit for a single hit; longer samples stay
  samples.

## Kits

**Save Kit / Load Kit** write and read Audio Unit presets (`.aupreset`) - the
same kind of file Logic saves for an AU setting. The dialogs open in
`/Library/Audio/Presets/Kitbox/`. That folder is root's, so it is created once by
hand:

    sudo mkdir -p /Library/Audio/Presets/Kitbox
    sudo chown "$USER" /Library/Audio/Presets/Kitbox

Until it exists, they open in Logic's settings folder for Kitbox,
`~/Music/Audio Music Apps/Plug-In Settings/Kitbox/`. The preset is named after the file, without extension, and that
name is what Logic shows. Inside, under `jucePluginState`, is the Kitbox
container: every pad's original sample file plus all settings, gzip-compressed.
The DAW session stores the same container, so a project never depends on where
the samples were.

Given the extension `.kitbox` instead, Save Kit writes the container itself.
Since 0.7.0 that is a plain ZIP: rename it to `.zip` (or `unzip` it) to get at
the samples.

    kit.xml                        every knob, each pad's Sample/Auto/model choice
    Original/01 Kick 01.wav        each pad's sample, the file as it came
    Synth/01 Kick 01.wav           its Transmute synth (24-bit, or 32-bit float above 0 dBFS)
    Synth/01 Kick 01.drumparams    the synth's parameters - opens in Transmute

The two digits are the pad. At most 16 originals and 16 synths. Kits and
sessions from before 0.7.0, and the kits Transmute's *Export Kitbox Kit*
writes, still load.

## Building

    Scripts/build.sh     # Release build + KitboxCheck + EditorShot (panel PNG in build/shots)
    Scripts/install.sh   # installs to ~/Library/Audio/Plug-Ins, runs auval
    Scripts/package.sh   # build + checks, then the AU/VST3 installer in build/

JUCE is expected at `~/JUCE`. No AUv3 on purpose - see the note in `CMakeLists.txt`.

## Licence

Kitbox is © 2026 T'Zorr and is distributed under AGPLv3 - see
[LICENSE](LICENSE). This follows from linking JUCE's free tier, which is
AGPLv3 itself; details in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contact

T'Zorr - <TZorr@gmx.de>
