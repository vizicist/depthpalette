#pragma once

#include <cstring>
#include <cstdlib>
#include <tuple>
#include <vector>
#include <libobsensor/ObSensor.hpp>

// Prefer native RGB on the 305; also accept the 335's MJPEG/YUYV profiles.
inline int colorFormatPreference(OBFormat format) {
    switch (format) {
    case OB_FORMAT_RGB: return 4;
    case OB_FORMAT_BGR: return 3;
    case OB_FORMAT_MJPG: return 2;
    case OB_FORMAT_YUYV: return 1;
    default: return 0;
    }
}

inline std::shared_ptr<const ob::StreamProfile> selectColorProfile(
    const std::shared_ptr<ob::StreamProfileList>& profiles, int fps, int width, int height) {
    std::shared_ptr<const ob::StreamProfile> best;
    std::tuple<bool, bool, int, int, int> bestScore{};
    for (uint32_t i = 0; i < profiles->getCount(); ++i) {
        auto profile = profiles->getProfile(i);
        auto video = profile->as<ob::VideoStreamProfile>();
        int preference = colorFormatPreference(video->getFormat());
        if (!preference) continue;
        auto score = std::make_tuple(static_cast<int>(video->fps()) == fps,
            static_cast<int>(video->getWidth()) == width && static_cast<int>(video->getHeight()) == height,
            preference, -std::abs(static_cast<int>(video->fps()) - fps),
            static_cast<int>(video->getWidth() * video->getHeight()));
        if (!best || score > bestScore) {
            best = profile;
            bestScore = score;
        }
    }
    return best;
}

inline bool convertColorToBgr(const std::shared_ptr<ob::ColorFrame>& frame,
                             ob::FormatConvertFilter& converter, std::vector<uint8_t>& output) {
    std::shared_ptr<ob::Frame> bgr = frame;
    switch (frame->getFormat()) {
    case OB_FORMAT_BGR: break;
    case OB_FORMAT_RGB: converter.setFormatConvertType(FORMAT_RGB_TO_BGR); break;
    case OB_FORMAT_MJPG: converter.setFormatConvertType(FORMAT_MJPG_TO_BGR); break;
    case OB_FORMAT_YUYV: converter.setFormatConvertType(FORMAT_YUYV_TO_BGR); break;
    default: return false;
    }
    if (frame->getFormat() != OB_FORMAT_BGR) bgr = converter.process(frame);
    const size_t size = static_cast<size_t>(frame->getWidth()) * frame->getHeight() * 3;
    if (!bgr || bgr->getDataSize() < size) return false;
    output.resize(size);
    std::memcpy(output.data(), bgr->getData(), size);
    return true;
}
