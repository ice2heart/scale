# scale

Batch image resizer that downscales a folder of images to 1K, 2K, 4K, and 8K resolutions in parallel.

## How it works

Given a directory of images, `scale` creates four sibling output folders (e.g. `textures_1k`, `textures_2k`, `textures_4k`, `textures_8k`) and resizes every image into each resolution using 8 threads. Images smaller than a target resolution are skipped for that target — no upscaling.

Existing resolution suffixes in filenames (`_1k`, `_2k`, `_4k`, `_8k`) are stripped before processing so re-running on already-scaled images works correctly.

Output is always written as PNG regardless of the source format.

## Build

Requires CMake 3.22+ and a C++23 compiler. Dependencies are fetched automatically via CMake FetchContent ([stb](https://github.com/nothings/stb)).

**Linux / macOS**
```sh
cmake -B build
cmake --build build
```

**Windows**
```sh
cmake -B build
cmake --build . --config Release
```

## Usage

```sh
./build/scale_app <directory>
```

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
