# Intergint Systems

VCV Rack 2 modules.

| Module | Description |
| --- | --- |
| Attenuverter | Polyphonic attenuverter with DC offset (3HP) |

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
| `make clean` | Delete build output |

## Adding a module

1. Draw the panel as an SVG in `res/`. Panels are 128.5 mm tall, and the width is a multiple of 5.08 mm (1 HP). Rack can't render SVG `<text>`, so convert text to paths first.
2. Optional: generate the code from the panel. Add a layer named `components` with colored placeholder shapes (red circles = params, green = inputs, blue = outputs, magenta = lights), then run
   `python3 dep/Rack-SDK/helper.py createmodule <Slug> res/<Slug>.svg src/<Slug>.cpp`.
   The script also prints the `plugin.json` entry for you.
3. If you write the module by hand instead, copy `src/Attenuverter.cpp` as a starting point.
4. Register the module in three places:
   - `plugin.json` → add an entry to `modules`
   - `src/plugin.hpp` → `extern Model* model<Slug>;`
   - `src/plugin.cpp` → `p->addModel(model<Slug>);`
5. `make install` and restart Rack.

Never change a module's `slug` after release. Patches refer to modules by slug.

## Releasing

Bump `version` in `plugin.json`. The major version must stay `2` to match Rack 2. Push a tag like `v2.0.1`. The GitHub Actions workflow builds all four platforms and attaches the `.vcvplugin` files to a GitHub release.

## License

GPL-3.0-or-later. See `LICENSE`.
