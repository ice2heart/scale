#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <map>

// stb headers (implementation follows)
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
        std::cerr << "Usage: " << argv[0] << " <directory>" << std::endl;
        return 1;
    }

    std::filesystem::path dir = argv[1];
    if (!std::filesystem::is_directory(dir)) {
        std::cerr << "Not a directory: " << dir << std::endl;
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
            std::cerr << "Failed to create " << sub << ": " << ec.message() << std::endl;
            continue;
        } 
        resolution_paths[r] = sub;
    }

    std::map<std::string, ImageData> images;

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        std::string filename = entry.path().filename().string();
        std::string fullpath = entry.path().string();

        int w, h, c;
        unsigned char* data = stbi_load(fullpath.c_str(), &w, &h, &c, 0);
        if (data) {
            ImageData img;
            img.width = w;
            img.height = h;
            img.channels = c;
            img.pixels.assign(data, data + (w * h * c));
            stbi_image_free(data);
            images[filename] = std::move(img);
            std::cout << "Loaded " << filename << " (" << w << "x" << h << ", " << c << " ch)\n";
        } else {
            continue;
        }
    }

    std::cout << "Total loaded images: " << images.size() << std::endl;

    // calculate total operations (images × resolutions) for progress reporting
    size_t totalOps = images.size() * resolution_paths.size();
    size_t opCount = 0;

    // for each image, generate scaled square copies per resolution
    for (const auto& [fname, img] : images) {
        for (const auto& [res, outdir] : resolution_paths) {
            int target = size_for(res);
            if (target <= 0) continue;
            ++opCount;
            std::cout << "Processing " << fname << " (" << to_string(res) << ") "
                      << opCount << " of " << totalOps << std::endl;

            size_t outSize = static_cast<size_t>(target) * target * img.channels;
            std::vector<unsigned char> outbuf(outSize);
            stbir_resize_uint8_linear(img.pixels.data(), img.width, img.height, 0,
                                       outbuf.data(), target, target, 0,
                                       (stbir_pixel_layout)img.channels);

            std::filesystem::path outpath = outdir / fname;
            outpath.replace_extension(".png");
            if (!stbi_write_png(outpath.string().c_str(), target, target, img.channels,
                                outbuf.data(), target * img.channels)) {
                std::cerr << "Failed to write " << outpath << std::endl;
            } else {
                std::cout << "Saved " << outpath << std::endl;
            }
        }
    }

    return 0;
}
