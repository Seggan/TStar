#include "XTransDemosaic.h"
#include <cmath>
#include <algorithm>

namespace Preprocessing {

int XTransDemosaic::getPixelType(int x, int y, const int pattern[6][6]) {
    return pattern[y % 6][x % 6];
}

bool XTransDemosaic::demosaic(const ImageBuffer& input, ImageBuffer& output, Algorithm algo) {
    if (input.channels() != 1) return false;

    // Parse X-Trans pattern from metadata (36 characters)
    int pattern[6][6];
    QString bayerStr = input.getHeaderValue("BAYERPAT");
    
    if (bayerStr.length() >= 36) {
        for (int i = 0; i < 36; ++i) {
            char c = bayerStr[i].toLatin1();
            int color = 0; // G
            if (c == 'R') color = 1;
            else if (c == 'B') color = 2;
            pattern[i / 6][i % 6] = color;
        }
    } else {
        // Default standard X-Trans pattern (same as before)
        static const int stdPattern[6][6] = {
            {0, 1, 0, 0, 2, 0},
            {1, 0, 1, 2, 0, 2},
            {0, 1, 0, 0, 2, 0},
            {0, 2, 0, 0, 1, 0},
            {2, 0, 2, 1, 0, 1},
            {0, 2, 0, 0, 1, 0}
        };
        memcpy(pattern, stdPattern, sizeof(pattern));
    }
    
    if (algo == Algorithm::Markesteijn) {
        interpolateMarkesteijn(input, output, pattern);
    } else {
        interpolateVNG(input, output, pattern);
    }
    return true;
}

void XTransDemosaic::interpolateMarkesteijn(const ImageBuffer& input, ImageBuffer& output, const int pattern[6][6]) {
    int w = input.width();
    int h = input.height();
    output.resize(w, h, 3);
    
    const float* in = input.data().data();
    float* out = output.data().data();

    // Step 1: Preliminary Green Interpolation with HV edge detection
    #pragma omp parallel for
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {
            int idx = y * w + x;
            int type = getPixelType(x, y, pattern);
            if (type == 0) {
                out[idx * 3 + 1] = in[idx];
            } else {
                // 3x3 homogeneity detector
                float hGrad = std::abs(in[idx-1] - in[idx+1]);
                float vGrad = std::abs(in[idx-w] - in[idx+w]);
                if (hGrad < vGrad) {
                    out[idx * 3 + 1] = (in[idx-1] + in[idx+1]) * 0.5f;
                } else {
                    out[idx * 3 + 1] = (in[idx-w] + in[idx+w]) * 0.5f;
                }
            }
        }
    }

    // Step 2: Ratio-guided R and B interpolation (Pass 1)
    #pragma omp parallel for collapse(2)
    for (int y = 3; y < h - 3; ++y) {
        for (int x = 3; x < w - 3; ++x) {
            int idx = y * w + x;
            int type = getPixelType(x, y, pattern);
            float g = out[idx * 3 + 1];
            
            if (type == 1) {
                out[idx * 3 + 0] = in[idx];
            } else {
                float rS = 0, rW = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (getPixelType(x + dx, y + dy, pattern) == 1) {
                            float gn = out[((y + dy) * w + (x + dx)) * 3 + 1];
                            float weight = 1.0f / (dx * dx + dy * dy + 0.1f);
                            rS += (in[(y + dy) * w + (x + dx)] / (gn + 1e-6f)) * weight;
                            rW += weight;
                        }
                    }
                }
                out[idx * 3 + 0] = g * (rW > 0 ? rS / rW : 1.0f);
            }

            if (type == 2) {
                out[idx * 3 + 2] = in[idx];
            } else {
                float bS = 0, bW = 0;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        if (getPixelType(x + dx, y + dy, pattern) == 2) {
                            float gn = out[((y + dy) * w + (x + dx)) * 3 + 1];
                            float weight = 1.0f / (dx * dx + dy * dy + 0.1f);
                            bS += (in[(y + dy) * w + (x + dx)] / (gn + 1e-6f)) * weight;
                            bW += weight;
                        }
                    }
                }
                out[idx * 3 + 2] = g * (bW > 0 ? bS / bW : 1.0f);
            }
        }
    }

    // Step 3: Color-Difference Refinement (Median filtering of R/G and B/G)
    // We use a small window (3x3) to smooth chrominance artifacts
    #pragma omp parallel for collapse(2)
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {
            int idx = y * w + x;
            float g = out[idx * 3 + 1];
            float rDiffs[9], bDiffs[9];
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nid = (y + dy) * w + (x + dx);
                    float gn = out[nid * 3 + 1];
                    rDiffs[n] = out[nid * 3 + 0] - gn;
                    bDiffs[n] = out[nid * 3 + 2] - gn;
                    n++;
                }
            }
            std::sort(rDiffs, rDiffs + 9);
            std::sort(bDiffs, bDiffs + 9);
            out[idx * 3 + 0] = g + rDiffs[4];
            out[idx * 3 + 2] = g + bDiffs[4];
        }
    }
}

void XTransDemosaic::interpolateVNG(const ImageBuffer& input, ImageBuffer& output, const int pattern[6][6]) {
    int w = input.width();
    int h = input.height();
    output.resize(w, h, 3);
    
    const float* in = input.data().data();
    float* out = output.data().data();
    
    // VNG (Variable Number of Gradients) algorithm adapted for X-Trans
    #pragma omp parallel for
    for (int y = 2; y < h - 2; ++y) {
        for (int x = 2; x < w - 2; ++x) {
            int currentType = getPixelType(x, y, pattern); // 0=G, 1=R, 2=B
            
            // Output pointers
            float* px = &out[(y*w + x) * 3];
            
            // Set the known channel
            if (currentType == 0) px[1] = in[y*w + x];      // G known
            else if (currentType == 1) px[0] = in[y*w + x]; // R known
            else px[2] = in[y*w + x];                       // B known

            // Calculate gradients in 8 directions (N, S, W, E, NW, NE, SW, SE)
            float gradients[8] = {0};
            auto val = [&](int dx, int dy) { return in[(y+dy)*w + (x+dx)]; };
            auto type = [&](int dx, int dy) { return getPixelType(x+dx, y+dy, pattern); };

            const int dirs[8][2] = {{0,-1}, {0,1}, {-1,0}, {1,0}, {-1,-1}, {1,-1}, {-1,1}, {1,1}};
            for (int d = 0; d < 8; ++d) {
                int dx = dirs[d][0];
                int dy = dirs[d][1];
                float g = 0;
                // Simple directional gradient for X-Trans: compare central pixel with 2-pixel-distance neighbors
                if (x+2*dx >= 0 && x+2*dx < w && y+2*dy >= 0 && y+2*dy < h) {
                     g += std::abs(val(0,0) - val(2*dx, 2*dy));
                }
                gradients[d] = g;
            }
            
            // Find direction with min gradient
            int bestDir = 0;
            float minG = gradients[0];
            for(int d=1; d<8; ++d) {
                if(gradients[d] < minG) {
                    minG = gradients[d];
                    bestDir = d;
                }
            }
            
            float rSum = 0, gSum = 0, bSum = 0;
            float rW = 0, gW = 0, bW = 0;
            
            // Collect neighbors in 5x5 area with directional weighting
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (dx==0 && dy==0) continue;
                    
                    int t = type(dx, dy);
                    float v = val(dx, dy);
                    float dist = std::sqrt((float)(dx*dx + dy*dy));
                    float w_dist = 1.0f / (dist + 0.1f);
                    
                    // Boost weight if neighbor aligns with the smooth (min gradient) direction
                    int bdx = dirs[bestDir][0];
                    int bdy = dirs[bestDir][1];
                    float dot = (dx*bdx + dy*bdy) / (dist * std::sqrt((float)(bdx*bdx + bdy*bdy)) + 0.01f);
                    
                    if (std::abs(dot) > 0.8f) w_dist *= 2.0f; // Strongly along/opposite min-gradient axis
                    else if (std::abs(dot) < 0.2f) w_dist *= 0.2f; // Strongly orthogonal to min-gradient
                    
                    if (t == 0) { gSum += v*w_dist; gW += w_dist; }
                    else if (t == 1) { rSum += v*w_dist; rW += w_dist; }
                    else { bSum += v*w_dist; bW += w_dist; }
                }
            }
            
            // Fill missing channels using weighted averages
            if (currentType != 0) px[1] = (gW > 0) ? gSum / gW : px[1];
            if (currentType != 1) px[0] = (rW > 0) ? rSum / rW : 0;
            if (currentType != 2) px[2] = (bW > 0) ? bSum / bW : 0;
        }
    }
}

} // namespace Preprocessing
