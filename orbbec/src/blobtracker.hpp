#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "blobdetect.hpp"

struct TrackedBlob {
    int serial;
    int cx, cy;          // centroid position
    int pixelCount;
    float avgDepthMm;
    uint16_t maxDepthMm;
};

class BlobTracker {
public:
    // Update tracking with the latest detected blobs.
    // Prints Cursor start/moved/end messages to stdout.
    void update(const std::vector<BlobInfo>& blobs, int frameCount, long long ms) {
        // Compute centroids for incoming blobs
        struct Incoming {
            int cx, cy;
            int idx;  // index into blobs vector
        };
        std::vector<Incoming> incoming;
        incoming.reserve(blobs.size());
        for (size_t i = 0; i < blobs.size(); i++) {
            int cx = (blobs[i].minX + blobs[i].maxX) / 2;
            int cy = (blobs[i].minY + blobs[i].maxY) / 2;
            incoming.push_back({cx, cy, static_cast<int>(i)});
        }

        // Build candidate match pairs (active × incoming) within kMatchRadius
        struct MatchPair {
            int activeIdx;
            int incomingIdx;
            int distSq;
        };
        std::vector<MatchPair> candidates;
        for (size_t a = 0; a < active_.size(); a++) {
            for (size_t n = 0; n < incoming.size(); n++) {
                int dx = active_[a].cx - incoming[n].cx;
                int dy = active_[a].cy - incoming[n].cy;
                int dsq = dx * dx + dy * dy;
                if (dsq <= kMatchRadiusSq) {
                    candidates.push_back({static_cast<int>(a), static_cast<int>(n), dsq});
                }
            }
        }

        // Greedy match: sort by distance, assign closest first
        std::sort(candidates.begin(), candidates.end(),
                  [](const MatchPair& a, const MatchPair& b) { return a.distSq < b.distSq; });

        std::vector<bool> activeMatched(active_.size(), false);
        std::vector<bool> incomingMatched(incoming.size(), false);

        for (const auto& m : candidates) {
            if (activeMatched[m.activeIdx] || incomingMatched[m.incomingIdx]) continue;
            activeMatched[m.activeIdx] = true;
            incomingMatched[m.incomingIdx] = true;

            // Update tracked blob position
            const auto& inc = incoming[m.incomingIdx];
            const auto& b = blobs[inc.idx];
            auto& t = active_[m.activeIdx];
            t.cx = inc.cx;
            t.cy = inc.cy;
            t.pixelCount = b.pixelCount;
            t.avgDepthMm = b.avgDepthMm;
            t.maxDepthMm = b.maxDepthMm;

            std::printf("[%6d %7lldms] Cursor moved #%d to (%d, %d) %dmm\n",
                        frameCount, ms, t.serial, t.cx, t.cy,
                        static_cast<int>(t.avgDepthMm + 0.5f));
        }

        // Unmatched active blobs -> ended
        // Remove in reverse order to keep indices valid
        for (int a = static_cast<int>(active_.size()) - 1; a >= 0; a--) {
            if (!activeMatched[a]) {
                std::printf("[%6d %7lldms] Cursor end #%d\n",
                            frameCount, ms, active_[a].serial);
                active_.erase(active_.begin() + a);
            }
        }

        // Unmatched incoming blobs -> new
        for (size_t n = 0; n < incoming.size(); n++) {
            if (!incomingMatched[n]) {
                const auto& inc = incoming[n];
                const auto& b = blobs[inc.idx];
                TrackedBlob t;
                t.serial = nextSerial_++;
                t.cx = inc.cx;
                t.cy = inc.cy;
                t.pixelCount = b.pixelCount;
                t.avgDepthMm = b.avgDepthMm;
                t.maxDepthMm = b.maxDepthMm;
                active_.push_back(t);

                std::printf("[%6d %7lldms] Cursor start #%d at (%d, %d) %dmm\n",
                            frameCount, ms, t.serial, t.cx, t.cy,
                            static_cast<int>(t.avgDepthMm + 0.5f));
            }
        }
    }

    const std::vector<TrackedBlob>& activeBlobs() const { return active_; }

private:
    static constexpr int kMatchRadius = 80;
    static constexpr int kMatchRadiusSq = kMatchRadius * kMatchRadius;

    std::vector<TrackedBlob> active_;
    int nextSerial_ = 1;
};
