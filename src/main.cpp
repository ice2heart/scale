#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>
#include <fpng.h>

namespace fs = std::filesystem;

struct ResolutionInfo {
    std::string_view suffix;
    int size;
};

// Ordered largest first: each step is downscaled from the previous one.
constexpr std::array<ResolutionInfo, 4> resolutions{{
    {"8k", 8192},
    {"4k", 4096},
    {"2k", 2048},
    {"1k", 1024},
}};

constexpr int smallest_size = resolutions.back().size;

// UTF-8 path string, for stb (built with *_WINDOWS_UTF8) and for printing.
static std::string to_utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

static fs::path from_utf8(std::string_view s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

static stbir_pixel_layout layout_for(int channels) {
    switch (channels) {
        case 1: return STBIR_1CHANNEL;
        case 2: return STBIR_RA;
        case 3: return STBIR_RGB;
        default: return STBIR_RGBA;
    }
}

// fpng is much faster than stb_image_write but only encodes RGB/RGBA, so
// 1- and 2-channel images go through stb to stay greyscale.
static bool write_png(const fs::path& path, const unsigned char* pixels, int w, int h, int c) {
    if (c != 3 && c != 4)
        return stbi_write_png(to_utf8(path).c_str(), w, h, c, pixels, w * c) != 0;

    std::vector<uint8_t> png;
    if (!fpng::fpng_encode_image_to_memory(pixels, w, h, c, png))
        return false;
    // ofstream takes the path directly, so Unicode filenames work on Windows.
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return f.good();
}

using StbImage = std::unique_ptr<unsigned char, decltype(&stbi_image_free)>;

struct Source {
    fs::path path;
    int width;
    int height;
};

// args are UTF-8 on every platform; args[0] is the program name.
static int run(const std::vector<std::string>& args) {
    bool srgb = false;
    std::vector<std::string_view> positional;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "--srgb")
            srgb = true;
        else
            positional.push_back(arg);
    }
    if (positional.size() != 1) {
        std::print("Usage: {} [--srgb] <directory>\n", args.empty() ? "scale_app" : args[0]);
        std::print("  --srgb  gamma-correct resize for color textures (default: linear, for data maps)\n");
        return 1;
    }

    // Normalize so "textures/", "." etc. still have a usable folder name.
    fs::path dir = from_utf8(positional[0]);
    std::error_code ec;
    dir = fs::absolute(dir, ec).lexically_normal();
    if (!dir.has_filename())
        dir = dir.parent_path();
    if (!fs::is_directory(dir)) {
        std::print("Not a directory: {}\n", to_utf8(dir));
        return 1;
    }

    // create subfolders for each resolution
    std::array<fs::path, resolutions.size()> outdirs;
    for (size_t r = 0; r < resolutions.size(); ++r) {
        fs::path sub = dir.parent_path() /
                       from_utf8(to_utf8(dir.filename()) + "_" + std::string(resolutions[r].suffix));
        fs::create_directory(sub, ec);
        if (ec) {
            std::print("Failed to create {}: {}\n", to_utf8(sub), ec.message());
            return 1;
        }
        outdirs[r] = std::move(sub);
    }

    // Scan sources by header only (no decode). Key is the stem with any
    // resolution suffix stripped, e.g. "image_4k.png" -> "image".
    std::map<std::string, Source> sources;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const std::string path = to_utf8(entry.path());
        int w, h, c;
        if (!stbi_info(path.c_str(), &w, &h, &c))
            continue;
        if (std::max(w, h) < smallest_size) {
            std::print("Skipping {} ({}x{}): smaller than {}\n",
                       to_utf8(entry.path().filename()), w, h, smallest_size);
            continue;
        }

        std::string stem = to_utf8(entry.path().stem());
        for (const auto& res : resolutions) {
            if (stem.size() > res.suffix.size() + 1 && stem.ends_with(res.suffix) &&
                stem[stem.size() - res.suffix.size() - 1] == '_') {
                stem.resize(stem.size() - res.suffix.size() - 1);
                break;
            }
        }

        auto [it, inserted] = sources.try_emplace(stem, Source{entry.path(), w, h});
        if (!inserted) {
            // Same stem from several files: keep the largest source.
            Source& prev = it->second;
            const bool bigger = std::max(w, h) > std::max(prev.width, prev.height);
            std::print("Warning: {} and {} both map to \"{}\", using {}\n",
                       to_utf8(prev.path.filename()), to_utf8(entry.path().filename()), stem,
                       to_utf8((bigger ? entry.path() : prev.path).filename()));
            if (bigger)
                prev = Source{entry.path(), w, h};
        }
    }

    std::print("Found {} images\n", sources.size());

    fpng::fpng_init();

    // One work item per source image: load once, write every resolution, free.
    std::vector<std::pair<const std::string*, const Source*>> tasks;
    tasks.reserve(sources.size());
    for (const auto& [stem, src] : sources)
        tasks.emplace_back(&stem, &src);

    const size_t total = tasks.size();
    std::atomic<size_t> next{0};
    std::atomic<bool> failed{false};

    auto worker = [&](std::stop_token st) {
        while (!st.stop_requested()) {
            const size_t i = next.fetch_add(1);
            if (i >= total)
                break;
            const std::string& stem = *tasks[i].first;
            const Source& src = *tasks[i].second;
            const std::string srcpath = to_utf8(src.path);

            int w, h, c;
            StbImage img{stbi_load(srcpath.c_str(), &w, &h, &c, 0), &stbi_image_free};
            if (!img) {
                std::print("Failed to load {}: {}\n", srcpath, stbi_failure_reason());
                failed = true;
                continue;
            }
            std::print("Processing {} ({}x{}, {} ch) {} of {}\n", stem, w, h, c, i + 1, total);

            // Cascade: each resolution is resized from the previous (larger) result.
            const unsigned char* prev = img.get();
            int pw = w, ph = h;
            std::unique_ptr<unsigned char[]> prevbuf;

            for (size_t r = 0; r < resolutions.size(); ++r) {
                const int target = resolutions[r].size;
                if (target > std::max(w, h))
                    continue;

                // Fit the longest side to target, keep aspect ratio.
                const double scale = static_cast<double>(target) / std::max(w, h);
                const int ow = std::max(1, static_cast<int>(std::lround(w * scale)));
                const int oh = std::max(1, static_cast<int>(std::lround(h * scale)));

                std::unique_ptr<unsigned char[]> outbuf;
                const unsigned char* out = prev;
                if (ow != pw || oh != ph) {
                    outbuf = std::make_unique_for_overwrite<unsigned char[]>(
                        static_cast<size_t>(ow) * oh * c);
                    const auto resize = srgb ? stbir_resize_uint8_srgb : stbir_resize_uint8_linear;
                    if (!resize(prev, pw, ph, 0, outbuf.get(), ow, oh, 0, layout_for(c))) {
                        std::print("Failed to resize {} to {}\n", stem, resolutions[r].suffix);
                        failed = true;
                        break;
                    }
                    out = outbuf.get();
                }

                const fs::path outpath = outdirs[r] /
                    from_utf8(stem + "_" + std::string(resolutions[r].suffix) + ".png");
                const std::string outstr = to_utf8(outpath);
                if (!write_png(outpath, out, ow, oh, c)) {
                    std::print("Failed to write {}\n", outstr);
                    failed = true;
                } else {
                    std::print("Saved {}\n", outstr);
                }

                if (outbuf) {
                    prevbuf = std::move(outbuf);
                    prev = prevbuf.get();
                }
                pw = ow;
                ph = oh;
            }
        }
    };

    const size_t nthreads =
        std::clamp<size_t>(std::thread::hardware_concurrency(), 1, std::max<size_t>(total, 1));
    {
        std::vector<std::jthread> threads;
        threads.reserve(nthreads);
        for (size_t t = 0; t < nthreads; ++t)
            threads.emplace_back(worker);
    } // jthreads join here

    return failed ? 1 : 0;
}

#ifdef _WIN32
// wmain gets the command line as UTF-16, so non-ASCII paths survive
// (plain main would get them in the ANSI code page).
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(to_utf8(fs::path(argv[i])));
    return run(args);
}
#else
int main(int argc, char* argv[]) {
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif
