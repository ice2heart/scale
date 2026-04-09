#include <filesystem>
#include <string>
#include <vector>
#include <map>
#include <array>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <print>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>


struct ImageData {
    int width;
    int height;
    int channels;
    std::vector<unsigned char> pixels;
};

enum class Resolution {
    R8K,
    R4K,
    R2K,
    R1K,
};

static std::string to_string(Resolution r) {
    switch (r) {
        case Resolution::R8K: return "8k";
        case Resolution::R4K: return "4k";
        case Resolution::R2K: return "2k";
        case Resolution::R1K: return "1k";
    }
    return "";
}

int size_for(Resolution r) {
    switch (r) {
        case Resolution::R8K: return 8192;
        case Resolution::R4K: return 4096;
        case Resolution::R2K: return 2048;
        case Resolution::R1K: return 1024;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::print("Usage: {} <directory>\n", argv[0]);
        return 1;
    }

    std::filesystem::path dir = argv[1];
    if (!std::filesystem::is_directory(dir)) {
        std::print("Not a directory: {}\n", dir.generic_string());
        return 1;
    }

    // create subfolders for each resolution and remember their paths
    std::map<Resolution, std::filesystem::path> resolution_paths;
    for (Resolution r : {Resolution::R8K, Resolution::R4K, Resolution::R2K, Resolution::R1K}) {
        std::string fname = dir.filename().generic_string() + "_"+ to_string(r);
        std::filesystem::path sub = dir / fname;
        std::error_code ec;
        std::filesystem::create_directory(sub, ec);
        if (ec) {
            std::print("Failed to create {}: {}\n", sub.string(), ec.message());
            continue;
        } 
        resolution_paths[r] = sub;
    }

    std::map<std::string, ImageData> images;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const std::string filename = entry.path().filename().string();
        std::string stem = entry.path().stem().string();
        // Strip any existing resolution suffix from the stem (e.g. "image_4k.png")
        constexpr std::array<const char*, 4> suffixes = {"_8k", "_4k", "_2k", "_1k"};
        for (auto s : suffixes) {
            if (stem.size() > std::strlen(s) && stem.ends_with(s)) {
                stem.resize(stem.size() - std::strlen(s));
                break;
            }
        }
        const std::string fullpath = entry.path().string();

        int w, h, c;
        unsigned char* data = stbi_load(fullpath.c_str(), &w, &h, &c, 0);
        if (data) {
            ImageData img;
            img.width = w;
            img.height = h;
            img.channels = c;
            img.pixels.assign(data, data + (w * h * c));
            stbi_image_free(data);
            images[stem] = std::move(img);
            std::print("Loaded {} ({}x{}, {} ch)\n", filename, w, h, c);
        } else {
            continue;
        }
    }

    std::print("Total loaded images: {}\n", images.size());

    // build a list of work items (image × resolution) so threads can consume
    struct WorkItem {
        const std::string* fname;
        const ImageData* img;
        const Resolution res;
        const std::filesystem::path* outdir;
    };

    std::vector<WorkItem> tasks;
    tasks.reserve(images.size() * resolution_paths.size());
    for (const auto& [fname, img] : images) {
        for (const auto& [res, outdir] : resolution_paths) {
            if (size_for(res) <= 0) continue;
            tasks.push_back(WorkItem{&fname, &img, res, &outdir});
        }
    }

    const size_t totalOps = tasks.size();
    std::atomic<size_t> opCount{0};
    std::mutex ioMutex; // protect print

    auto worker = [&](std::stop_token st) {
        while (!st.stop_requested()) {
            size_t i = opCount.fetch_add(1);
            if (i >= totalOps)
                break;
            const WorkItem &item = tasks[i];
            const std::string fname{*item.fname + "_" + to_string(item.res)};
            const int target = size_for(item.res);
            {
                std::lock_guard<std::mutex> lock(ioMutex);
                std::print("Processing {} ({}) {} of {}\n",
                           *item.fname, to_string(item.res), (i+1), totalOps);
            }
            const size_t outSize = static_cast<size_t>(target) * target * item.img->channels;
            std::vector<unsigned char> outbuf(outSize);
            stbir_resize_uint8_linear(item.img->pixels.data(), item.img->width, item.img->height, 0,
                                       outbuf.data(), target, target, 0,
                                       (stbir_pixel_layout)item.img->channels);

            std::filesystem::path outpath = *item.outdir / fname;
            outpath.replace_extension(".png");
            if (!stbi_write_png(outpath.string().c_str(), target, target, item.img->channels,
                                outbuf.data(), target * item.img->channels)) {
                std::lock_guard<std::mutex> lock(ioMutex);
                std::print("Failed to write {}\n", outpath.string());
            } else {
                std::lock_guard<std::mutex> lock(ioMutex);
                std::print("Saved {}\n", outpath.string());
            }
        }
    };

    // launch 8 threadst cou
    std::vector<std::jthread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back(worker);

    // threads join automatically on destruction of jthread

    return 0;
}
