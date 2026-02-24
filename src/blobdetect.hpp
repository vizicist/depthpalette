#pragma once

#include <cstdint>
#include <vector>

// Simple union-find (disjoint set) for connected component labeling.
struct UnionFind {
    std::vector<int> parent;
    std::vector<int> rank;

    // Initialize with n elements (0..n-1). Element 0 is a dummy for "no label".
    void init(int n) {
        parent.resize(n);
        rank.assign(n, 0);
        for (int i = 0; i < n; i++) parent[i] = i;
    }

    // Ensure the structure can hold element x.
    void grow(int x) {
        while (static_cast<int>(parent.size()) <= x) {
            int i = static_cast<int>(parent.size());
            parent.push_back(i);
            rank.push_back(0);
        }
    }

    int find(int x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];  // path compression
            x = parent[x];
        }
        return x;
    }

    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return;
        if (rank[a] < rank[b]) { int t = a; a = b; b = t; }
        parent[b] = a;
        if (rank[a] == rank[b]) rank[a]++;
    }
};

struct BlobInfo {
    int minX, minY, maxX, maxY;
    int pixelCount;
    uint64_t depthSum;      // accumulator for average calculation
    float avgDepthMm;       // average depth across blob pixels
    uint16_t maxDepthMm;    // maximum depth in this blob
    int maxDepthX, maxDepthY; // pixel position of the maximum depth
};

// Detect connected components of black pixels (val == 0) in a packed BGR image,
// then draw green rectangles around blobs whose pixel count <= maxBlobPixels.
// Operates in-place on the BGR buffer.
// If depthMm is provided, computes per-blob depth statistics.
// Returns the filtered blobs (those that were drawn).
inline std::vector<BlobInfo> detectAndDrawBlobs(uint8_t* bgr, int width, int height,
                                                 int maxBlobPixels,
                                                 const uint16_t* depthMm = nullptr,
                                                 int minBlobPixels = 20) {
    int totalPixels = width * height;

    // Label buffer — 0 means unlabeled / background (white pixel)
    std::vector<int> labels(totalPixels, 0);

    UnionFind uf;
    // Index 0 = dummy (reserved for "no label"). Labels start at 1.
    uf.grow(0);

    int nextLabel = 1;

    // ---- Pass 1: assign provisional labels ----
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            // Check if this pixel is black (foreground)
            int bgrIdx = idx * 3;
            if (bgr[bgrIdx] != 0 || bgr[bgrIdx + 1] != 0 || bgr[bgrIdx + 2] != 0)
                continue;  // white/non-black -> background

            int labelUp   = (y > 0) ? labels[(y - 1) * width + x] : 0;
            int labelLeft  = (x > 0) ? labels[y * width + (x - 1)] : 0;

            if (labelUp == 0 && labelLeft == 0) {
                // New component
                uf.grow(nextLabel);
                labels[idx] = nextLabel;
                nextLabel++;
            } else if (labelUp != 0 && labelLeft == 0) {
                labels[idx] = labelUp;
            } else if (labelUp == 0 && labelLeft != 0) {
                labels[idx] = labelLeft;
            } else {
                // Both neighbors labeled — pick one, merge
                labels[idx] = labelUp;
                uf.unite(labelUp, labelLeft);
            }
        }
    }

    if (nextLabel <= 1) return {};  // no foreground pixels at all

    // ---- Pass 2: resolve labels, compute bounding boxes and depth stats ----
    std::vector<int> rootToBlob(nextLabel, -1);
    std::vector<BlobInfo> blobs;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int lbl = labels[idx];
            if (lbl == 0) continue;

            int root = uf.find(lbl);
            labels[idx] = root;

            int blobIdx = rootToBlob[root];
            if (blobIdx < 0) {
                blobIdx = static_cast<int>(blobs.size());
                rootToBlob[root] = blobIdx;
                blobs.push_back({x, y, x, y, 0, 0, 0.0f, 0, 0, 0});
            }

            BlobInfo& b = blobs[blobIdx];
            if (x < b.minX) b.minX = x;
            if (x > b.maxX) b.maxX = x;
            if (y < b.minY) b.minY = y;
            if (y > b.maxY) b.maxY = y;
            b.pixelCount++;

            if (depthMm) {
                uint16_t d = depthMm[idx];
                b.depthSum += d;
                if (d > b.maxDepthMm) {
                    b.maxDepthMm = d;
                    b.maxDepthX = x;
                    b.maxDepthY = y;
                }
            }
        }
    }

    // ---- Filter, compute averages, draw rectangles ----
    std::vector<BlobInfo> result;
    // minBlobPixels is now a function parameter (default 20)

    auto drawHLine = [&](int x0, int x1, int y) {
        if (y < 0 || y >= height) return;
        if (x0 < 0) x0 = 0;
        if (x1 >= width) x1 = width - 1;
        for (int x = x0; x <= x1; x++) {
            int i = (y * width + x) * 3;
            bgr[i + 0] = 0;    // B
            bgr[i + 1] = 255;  // G
            bgr[i + 2] = 0;    // R  -> green in BGR
        }
    };

    auto drawVLine = [&](int x, int y0, int y1) {
        if (x < 0 || x >= width) return;
        if (y0 < 0) y0 = 0;
        if (y1 >= height) y1 = height - 1;
        for (int y = y0; y <= y1; y++) {
            int i = (y * width + x) * 3;
            bgr[i + 0] = 0;
            bgr[i + 1] = 255;
            bgr[i + 2] = 0;
        }
    };

    for (auto& b : blobs) {
        if (b.pixelCount < minBlobPixels) continue;   // skip noise
        if (b.pixelCount > maxBlobPixels) continue;    // skip large blobs

        // Compute average depth
        if (depthMm && b.pixelCount > 0) {
            b.avgDepthMm = static_cast<float>(b.depthSum) / b.pixelCount;
        }

        // Draw 2px thick rectangle with 2px margin for visibility
        int rx0 = b.minX - 2;
        int ry0 = b.minY - 2;
        int rx1 = b.maxX + 2;
        int ry1 = b.maxY + 2;
        drawHLine(rx0, rx1, ry0);
        drawHLine(rx0, rx1, ry1);
        drawVLine(rx0, ry0, ry1);
        drawVLine(rx1, ry0, ry1);
        drawHLine(rx0 + 1, rx1 - 1, ry0 + 1);
        drawHLine(rx0 + 1, rx1 - 1, ry1 - 1);
        drawVLine(rx0 + 1, ry0 + 1, ry1 - 1);
        drawVLine(rx1 - 1, ry0 + 1, ry1 - 1);

        result.push_back(b);
    }

    return result;
}
