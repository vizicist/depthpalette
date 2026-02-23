#include "pointcloud.hpp"

#include <algorithm>

CameraIntrinsics getIntrinsics(dai::Device& device, dai::CameraBoardSocket socket,
                                int width, int height) {
    auto calibData = device.readCalibration();
    auto intrinsics = calibData.getCameraIntrinsics(socket, width, height);

    return CameraIntrinsics{
        static_cast<float>(intrinsics[0][0]),  // fx
        static_cast<float>(intrinsics[1][1]),  // fy
        static_cast<float>(intrinsics[0][2]),  // cx
        static_cast<float>(intrinsics[1][2]),  // cy
    };
}

PointCloudData depthToPointCloud(const std::shared_ptr<dai::ImgFrame>& depthFrame,
                                  const std::shared_ptr<dai::ImgFrame>& colorFrame,
                                  const CameraIntrinsics& intr,
                                  int step) {
    int depthW = depthFrame->getWidth();
    int depthH = depthFrame->getHeight();

    int colorW = colorFrame->getWidth();
    int colorH = colorFrame->getHeight();

    const auto& depthData = depthFrame->getData();
    const auto* depthPixels = reinterpret_cast<const uint16_t*>(depthData.data());

    // Color frame is planar RGB: [R plane][G plane][B plane]
    const auto& colorData = colorFrame->getData();
    int colorPlaneSize = colorW * colorH;

    // Scale factors to map depth pixel coords to color pixel coords
    float scaleX = static_cast<float>(colorW) / depthW;
    float scaleY = static_cast<float>(colorH) / depthH;

    if (step < 1) step = 1;
    int outW = (depthW + step - 1) / step;
    int outH = (depthH + step - 1) / step;

    PointCloudData result;
    result.points.reserve(outW * outH);
    result.colors.reserve(outW * outH);

    for (int v = 0; v < depthH; v += step) {
        for (int u = 0; u < depthW; u += step) {
            uint16_t d = depthPixels[v * depthW + u];
            if (d == 0) continue;

            double z = d / 1000.0;  // mm -> meters
            double x = (u - intr.cx) * z / intr.fx;
            double y = (v - intr.cy) * z / intr.fy;
            result.points.emplace_back(x, y, z);

            // Map depth pixel to color pixel
            int cu = std::clamp(static_cast<int>(u * scaleX), 0, colorW - 1);
            int cv = std::clamp(static_cast<int>(v * scaleY), 0, colorH - 1);
            int colorIdx = cv * colorW + cu;

            double r = colorData[colorIdx] / 255.0;
            double g = colorData[colorIdx + colorPlaneSize] / 255.0;
            double b = colorData[colorIdx + 2 * colorPlaneSize] / 255.0;
            result.colors.emplace_back(r, g, b);
        }
    }

    return result;
}

PointCloudData colorImageToPointCloud(const std::shared_ptr<dai::ImgFrame>& colorFrame,
                                       const CameraIntrinsics& intr,
                                       double fixedDepth,
                                       int step) {
    int w = colorFrame->getWidth();
    int h = colorFrame->getHeight();

    const auto& colorData = colorFrame->getData();
    int planeSize = w * h;

    if (step < 1) step = 1;
    int outW = (w + step - 1) / step;
    int outH = (h + step - 1) / step;

    PointCloudData result;
    result.points.reserve(outW * outH);
    result.colors.reserve(outW * outH);

    for (int v = 0; v < h; v += step) {
        for (int u = 0; u < w; u += step) {
            double x = (u - intr.cx) * fixedDepth / intr.fx;
            double y = (v - intr.cy) * fixedDepth / intr.fy;
            result.points.emplace_back(x, y, fixedDepth);

            int idx = v * w + u;
            double r = colorData[idx] / 255.0;
            double g = colorData[idx + planeSize] / 255.0;
            double b = colorData[idx + 2 * planeSize] / 255.0;
            result.colors.emplace_back(r, g, b);
        }
    }

    return result;
}
