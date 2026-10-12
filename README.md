# scale

Batch image resizer that downscales a folder of images to 1K, 2K, 4K, and 8K resolutions in parallel.

## How it works

Given a directory of images, `scale` creates four sibling output folders (e.g. `textures_1k`, `textures_2k`, `textures_4k`, `textures_8k`) and resizes every image into each resolution, one image per thread (as many threads as CPU cores). The longest side is fit to the target size and the aspect ratio is kept. Images smaller than a target resolution are skipped for that target — no upscaling.

Each image is decoded once, and each smaller resolution is downscaled from the previous (larger) result, so only a few images are held in memory at a time.

Existing resolution suffixes in filenames (`_1k`, `_2k`, `_4k`, `_8k`) are stripped before processing so re-running on already-scaled images works correctly. If several files map to the same name (e.g. `rock.png` and `rock_4k.png`), the largest one is used and a warning is printed.

Output is always written as PNG regardless of the source format.

## Download

Prebuilt Windows x64 binaries (statically linked, no VC++ redistributable needed) are attached to each [GitHub release](../../releases). They are built by the `Windows release` workflow whenever a `v*` tag is pushed.

## Build

Requires CMake 3.22+ and a C++23 compiler. Dependencies are fetched automatically via CMake FetchContent: [stb](https://github.com/nothings/stb) (decode, resize) and [fpng](https://github.com/richgel999/fpng) (fast PNG encoding). Both are plain source files with no further dependencies, so the build works the same on Windows, Linux and macOS.

**Linux / macOS** (defaults to a Release build)
```sh
cmake -B build
cmake --build build
```

**Windows**
```sh
cmake -B build
cmake --build build --config Release
```

## Usage

```sh
./build/scale_app [--srgb] <directory>
```

- `--srgb` — gamma-correct resizing, for color textures (albedo/diffuse). Without it the data is resized linearly, which is correct for normal, roughness and other data maps.

The exit code is non-zero if any image failed to load, resize or write.

**Example:**

```
textures/
  rock.png        (4096x4096)
  wood.jpg        (2048x2048)
```

After running `./scale_app textures`:

```
textures/
textures_1k/
  rock_1k.png
  wood_1k.png
textures_2k/
  rock_2k.png
  wood_2k.png
textures_4k/
  rock_4k.png
  wood_4k.png    <- skipped (source is already 2K, below 4K target)
textures_8k/
  ...            <- all skipped (both sources below 8K)
```
