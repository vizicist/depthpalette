#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <Eigen/Core>
#include <depthai/depthai.hpp>

struct CameraIntrinsics {
    float fx;  // focal length x (pixels)
    float fy;  // focal length y (pixels)
    float cx;  // principal point x
    float cy;  // principal point y
};

// Query camera intrinsics from the device calibration for a given socket and resolution.
CameraIntrinsics getIntrinsics(dai::Device& device, dai::CameraBoardSocket socket,
                                int width, int height);

// Result of depth-to-point-cloud conversion.
struct PointCloudData {
    std::vector<Eigen::Vector3d> points;
    std::vector<Eigen::Vector3d> colors;  // RGB in [0, 1]
};

// Convert a depth frame + color frame to a colored 3D point cloud.
// Depth is RAW16 in millimeters. Color is RGB planar (non-interleaved).
// step controls subsampling: step=1 uses every pixel, step=2 every other (4x fewer), etc.
PointCloudData depthToPointCloud(const std::shared_ptr<dai::ImgFrame>& depthFrame,
                                  const std::shared_ptr<dai::ImgFrame>& colorFrame,
                                  const CameraIntrinsics& intrinsics,
                                  int step = 1);

// Project the entire color image onto a flat plane at a fixed depth.
// Every nth pixel (controlled by step) becomes a colored 3D point.
// step=1 uses every pixel, step=2 uses every other pixel (4x fewer points), etc.
PointCloudData colorImageToPointCloud(const std::shared_ptr<dai::ImgFrame>& colorFrame,
                                       const CameraIntrinsics& intrinsics,
                                       double fixedDepth = 1.0,
                                       int step = 1);
