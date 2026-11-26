# Intergint Systems

VCV Rack 2 modules.

| Module | Description |
| --- | --- |
| Attenuverter | Polyphonic attenuverter with DC offset (3HP) |
| Piano Roll | Clickable 2-octave piano roll sequencer with a configurable loop (bars × beats per bar). Each clock pulse advances one 16th note. Up to 4 voices on polyphonic V/Oct and gate outputs (30HP) |

### Piano Roll

- **Voices:** right-click to set **Voices** (1–4). Each voice is its own one-note-at-a-time line with its own color: orange, cyan, pink and green. V/OCT and GATE become polyphonic, with one channel per voice.
- **VOICE buttons:** press one to choose which voice you're drawing. If it's above the current voice count, the count rises to match: pressing 3 with 1 voice gives you 3 voices. The fully lit button is the voice you're editing, half-lit buttons are the other active voices, and faint buttons are unused. Clicks only edit that voice. The other voices are drawn faded underneath. Lowering the voice count hides the extra voices but keeps their notes.
- **Loop length:** right-click the module to set **Bars** (1–16) and **Beats per bar** (1–8). The default is 4 bars of 4 beats. Each beat is four 16th-note steps. If you shorten the loop, notes past the new end are kept, and they come back when you lengthen it again.
- **Scale:** right-click to choose **Scale** and **Root**. The scales are Chromatic (the default), Major, Natural minor, Harmonic minor, Melodic minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major and Minor pentatonic, Blues, and Whole tone. Rows outside the scale are shaded and won't take new notes. Root rows are marked in orange on the keyboard strip. Notes that already sit outside a newly chosen scale stay in the pattern: they're drawn dimmed, and they play snapped to the nearest scale note (the lower one on a tie). Switch back and they play as written.
- **Clock in:** each pulse advances one 16th-note step. At the end of the loop it wraps back to the start.
- **Overview strip:** the thin strip along the top of the display shows the whole loop, with the visible section outlined. The grid shows as many whole bars as fit in 64 columns. For longer loops, click or drag in the strip to scroll.
- **Reset in:** the next clock pulse plays step 1.
- **Editing:** click an empty cell to add a note to the selected voice, and drag right to make it longer. Click one of that voice's notes to delete it. Each voice holds one note per step.
- **OCT trimpots:** one per voice, sitting under that voice's button. Each shifts its voice from −3 to +3 octaves, so one voice can play bass while another plays up high from the same 2-octave grid. The grid's C4 row puts out 0 V when the trimpot is centered.
- **Gate out:** 10 V while a note plays. Between two notes that touch, the gate drops for 1 ms so the next note retriggers.
- **Clear:** right-click the module and choose **Clear voice N** or **Clear all voices**. **Initialize** resets everything. All of this is saved with your patch.

## Install a release build

1. Download the `.vcvplugin` file for your platform (`mac-arm64`, `mac-x64`, `lin-x64` or `win-x64`).
2. Copy it into your Rack 2 plugins folder. If you don't know where that is, open Rack and choose **Help → Open user folder**; the plugins folder is inside it:
   - macOS (Apple Silicon): `~/Library/Application Support/Rack2/plugins-mac-arm64/`
   - macOS (Intel): `~/Library/Application Support/Rack2/plugins-mac-x64/`
   - Linux: `~/.local/share/Rack2/plugins-lin-x64/`
   - Windows: `%LOCALAPPDATA%\Rack2\plugins-win-x64\`
3. Restart Rack. Rack unpacks the file on startup. The modules show up in the browser under **Intergint Systems**.

## Build from source

### Prerequisites

- **macOS:** Xcode command line tools (`xcode-select --install`), plus `brew install jq zstd`.
- **Linux:** `build-essential git curl unzip jq zstd` (or your distro's equivalents).
- **Windows:** [MSYS2](https://www.msys2.org/). In the MINGW64 shell, run
  `pacman -S git wget make tar unzip zip mingw-w64-x86_64-gcc mingw-w64-x86_64-gdb mingw-w64-x86_64-cmake autoconf automake libtool mingw-w64-x86_64-jq python zstd mingw-w64-x86_64-pkgconf`.

You also need VCV Rack 2 installed if you want to run the modules.

### Build and install

```sh
./scripts/fetch-sdk.sh   # downloads the Rack SDK into dep/Rack-SDK (one time)
make -j                  # builds plugin.dylib / plugin.so / plugin.dll
make install             # packages it and copies the .vcvplugin into your Rack plugins folder
```

Restart Rack after `make install` to load the new build.

To use an SDK version other than the default, pass it to the script: `./scripts/fetch-sdk.sh 2.6.6`. To use an SDK you already have somewhere else, set `RACK_DIR`: `make RACK_DIR=~/Rack-SDK`.

### Other make targets

| Target | What it does |
| --- | --- |
| `make` | Compile the plugin library |
| `make dist` | Build `dist/IntergintSystems-<version>-<platform>.vcvplugin` |
| `make install` | `make dist`, then copy the package into the Rack user plugins folder |
| `make panels` | Regenerate `res/*.svg` from `panels/*.svg`, turning text into paths. Needs Python 3 and sets up `dep/.venv` on first run |
| `make clean` | Delete build output |

## Adding a module

1. Draw the panel source as an SVG in `panels/`, using millimetres as the viewBox units. Panels are 128.5 mm tall, and the width is a multiple of 5.08 mm (1 HP). Write labels as plain `<text>` elements. They support `x`, `y` (the baseline), `font-size`, `text-anchor`, `fill` and `letter-spacing`.
2. Run `make panels`. Rack can't render SVG `<text>`, so this turns each label into glyph outlines (Nunito Bold, from `fonts/`) and writes the result to `res/`. The generated `res/*.svg` files are committed, so regular builds don't need Python. Always edit `panels/`, never `res/`.
3. Optional: generate the code from the panel. Add a layer named `components` with colored placeholder shapes (red circles = params, green = inputs, blue = outputs, magenta = lights), then run
   `python3 dep/Rack-SDK/helper.py createmodule <Slug> res/<Slug>.svg src/<Slug>.cpp`.
   The script also prints the `plugin.json` entry for you.
4. If you write the module by hand instead, copy `src/Attenuverter.cpp` as a starting point.
5. Register the module in three places:
   - `plugin.json` → add an entry to `modules`
   - `src/plugin.hpp` → `extern Model* model<Slug>;`
   - `src/plugin.cpp` → `p->addModel(model<Slug>);`
6. `make install` and restart Rack.

Never change a module's `slug` after release. Patches refer to modules by slug.

## Releasing

Bump `version` in `plugin.json`. The major version must stay `2` to match Rack 2. Push a tag like `v2.0.1`. The GitHub Actions workflow builds all four platforms and attaches the `.vcvplugin` files to a GitHub release.

## License

Code: GPL-3.0-or-later. See `LICENSE`.

The Nunito font in `fonts/` is under the SIL Open Font License. See `fonts/Nunito-LICENSE.txt`.
