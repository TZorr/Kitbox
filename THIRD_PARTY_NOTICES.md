# Third-party notices

Kitbox is © 2026 T'Zorr and is distributed under the GNU Affero General
Public License, version 3 (AGPLv3) - see [LICENSE](LICENSE). This follows
from the one third-party component it links against.

## JUCE

- **What:** the JUCE 9 framework (audio plugin client, audio formats, GUI).
  Not included in this repository - `CMakeLists.txt` expects it at `~/JUCE`
  (or a path passed with `-DJUCE_PATH`) and links it as an external framework.
- **Copyright:** © Raw Material Software Limited - <https://juce.com>.
- **License:** JUCE 9 is dual-licensed. This project uses JUCE's free tier,
  the GNU Affero General Public License, version 3 (AGPLv3). Because JUCE is
  AGPLv3 here and Kitbox links it, Kitbox is itself AGPLv3 in turn - the
  AGPL's own requirement for any work that incorporates AGPL-licensed code.
  The full JUCE licence terms are at
  <https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>.
- **What AGPLv3 asks of anyone who distributes this plugin (including a
  built AU/VST3, not only source):** make the complete corresponding source
  available under the same licence, and preserve the copyright and licence
  notices. This repository already is that source.

## Everything else

The sampler engine, the filter, the envelopes, the send effects (shared with
its sibling Rackbox) and the panel are Kitbox's own code. The drive's
antiderivative anti-aliasing and the frequency shifter follow ideas published
in Signalsmith Audio's open-source DSP, but were written independently: no
code or tuned constants were copied; the shifter's Hilbert allpass is
designed by `Scripts/HilbertDesign.cpp`. No other library is linked, and
nothing is downloaded at build or run time.

The demo kit in `Kits/` was exported from Transmute, T'Zorr's drum
resynthesiser: its sixteen drums are Transmute's synthesis, rendered to WAV.
