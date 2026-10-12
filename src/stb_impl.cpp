// stb implementations live in their own translation unit so editing main.cpp
// doesn't recompile them.

// On Windows, treat filenames passed to stb as UTF-8 (otherwise they go through
// the ANSI code page and non-ASCII paths fail to open).
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
#include <stb_image_resize2.h>
