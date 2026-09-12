#include "colorstream.hpp"
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() try {
    ob::FormatConvertFilter converter;
    std::vector<uint8_t> output;
    auto convert = [&](OBFormat format, const std::vector<uint8_t>& data) {
        auto frame = ob::FrameFactory::createVideoFrame(OB_FRAME_COLOR, format, 2, 2,
            format == OB_FORMAT_MJPG ? static_cast<uint32_t>(data.size()) : 0)->as<ob::ColorFrame>();
        frame->updateData(data.data(), static_cast<uint32_t>(data.size()));
        require(convertColorToBgr(frame, converter, output), "Conversion failed");
        require(output.size() == 12, "Incorrect output size");
    };
    const std::vector<uint8_t> rgb{255,0,0, 0,255,0, 0,0,255, 255,255,255};
    const std::vector<uint8_t> bgr{0,0,255, 0,255,0, 255,0,0, 255,255,255};
    convert(OB_FORMAT_RGB, rgb);
    require(output == bgr, "RGB channel order incorrect");
    convert(OB_FORMAT_BGR, bgr);
    require(output == bgr, "BGR passthrough incorrect");
    convert(OB_FORMAT_YUYV, {16,128,235,128, 16,128,235,128});
    require(output[0] < 5 && output[3] > 250, "YUYV black/white conversion incorrect");

    std::vector<uint8_t> jpeg;
    const uint8_t redPixels[]{255,0,0, 255,0,0, 255,0,0, 255,0,0};
    stbi_write_jpg_to_func([](void* context, void* data, int size) {
        auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
        auto begin = static_cast<uint8_t*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    }, &jpeg, 2, 2, 3, redPixels, 100);
    convert(OB_FORMAT_MJPG, jpeg);
    require(output[2] > 240 && output[0] < 15, "MJPEG channel order incorrect");
    auto unsupported = ob::FrameFactory::createVideoFrame(OB_FRAME_COLOR, OB_FORMAT_Y8, 2, 2)->as<ob::ColorFrame>();
    require(!convertColorToBgr(unsupported, converter, output), "Unsupported format accepted");
    std::cout << "RGB, BGR, YUYV, MJPEG and unsupported-format checks passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
