#include "RawEditorProcessor.h"
#include "../ImageBuffer.h"
#include <cmath>
#include <algorithm>
#include <numeric>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace RawEditor {

// ─── Luma coefficients (Rec.709) ─────────────────────────────────────────────
static constexpr float LUMA_R = 0.2126f;
static constexpr float LUMA_G = 0.7152f;
static constexpr float LUMA_B = 0.0722f;

// ─── Math Utilities ──────────────────────────────────────────────────────────

float Processor::getLuma(float r, float g, float b) {
    return r * LUMA_R + g * LUMA_G + b * LUMA_B;
}

float Processor::smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void Processor::rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    float cMax = std::max({r, g, b});
    float cMin = std::min({r, g, b});
    float delta = cMax - cMin;
    h = 0.0f;
    if (delta > 0.0f) {
        if (cMax == r)      h = 60.0f * std::fmod((g - b) / delta, 6.0f);
        else if (cMax == g) h = 60.0f * (((b - r) / delta) + 2.0f);
        else                h = 60.0f * (((r - g) / delta) + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
    s = (cMax > 0.0f) ? (delta / cMax) : 0.0f;
    v = cMax;
}

void Processor::hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float C = v * s;
    float X = C * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - C;
    float rp, gp, bp;
    if (h < 60.0f)       { rp = C; gp = X; bp = 0; }
    else if (h < 120.0f) { rp = X; gp = C; bp = 0; }
    else if (h < 180.0f) { rp = 0; gp = C; bp = X; }
    else if (h < 240.0f) { rp = 0; gp = X; bp = C; }
    else if (h < 300.0f) { rp = X; gp = 0; bp = C; }
    else                 { rp = C; gp = 0; bp = X; }
    r = rp + m;
    g = gp + m;
    b = bp + m;
}

float Processor::hashNoise(float x, float y) {
    float p3x = std::fmod(std::fabs(x * 0.1031f), 1.0f);
    float p3y = std::fmod(std::fabs(y * 0.1031f), 1.0f);
    float p3z = std::fmod(std::fabs(x * 0.1031f), 1.0f);  // .xyx pattern
    float dot = p3x * (p3y + 33.33f) + p3y * (p3z + 33.33f) + p3z * (p3x + 33.33f);
    p3x = std::fmod(std::fabs(p3x + dot), 1.0f);
    p3y = std::fmod(std::fabs(p3y + dot), 1.0f);
    p3z = std::fmod(std::fabs(p3z + dot), 1.0f);
    return std::fmod(std::fabs((p3x + p3y) * p3z), 1.0f);
}

float Processor::gradientNoise(float px, float py) {
    float ix = std::floor(px);
    float iy = std::floor(py);
    float fx = px - ix;
    float fy = py - iy;
    // Quintic smooth
    float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

    auto h = [](float ax, float ay) -> float {
        return std::fmod(std::fabs(std::sin(ax * 12.9898f + ay * 78.233f) * 43758.5453f), 1.0f);
    };

    float n00 = h(ix, iy);
    float n10 = h(ix + 1, iy);
    float n01 = h(ix, iy + 1);
    float n11 = h(ix + 1, iy + 1);

    float nx0 = n00 + ux * (n10 - n00);
    float nx1 = n01 + ux * (n11 - n01);
    return (nx0 + uy * (nx1 - nx0)) * 2.0f - 1.0f;
}

// ─── Params::isIdentity ──────────────────────────────────────────────────────

bool Params::isIdentity() const {
    if (exposure != 0.0f || brightness != 0.0f || contrast != 0.0f) return false;
    if (highlights != 0.0f || shadows != 0.0f || whites != 0.0f || blacks != 0.0f) return false;
    if (temperature != 0.0f || tint != 0.0f || saturation != 0.0f || vibrance != 0.0f) return false;
    if (sharpness != 0.0f || clarity != 0.0f || structure != 0.0f || dehaze != 0.0f) return false;
    if (caRedCyan != 0.0f || caBlueYellow != 0.0f) return false;
    if (vignetteAmount != 0.0f || grainAmount != 0.0f) return false;
    for (auto& h : hsl) {
        if (h.hue != 0.0f || h.saturation != 0.0f || h.luminance != 0.0f) return false;
    }
    if (colorGradingShadows.saturation != 0.0f || colorGradingShadows.luminance != 0.0f) return false;
    if (colorGradingMidtones.saturation != 0.0f || colorGradingMidtones.luminance != 0.0f) return false;
    if (colorGradingHighlights.saturation != 0.0f || colorGradingHighlights.luminance != 0.0f) return false;
    if (!Processor::isDefaultCurve(lumaCurve)) return false;
    if (!Processor::isDefaultCurve(redCurve)) return false;
    if (!Processor::isDefaultCurve(greenCurve)) return false;
    if (!Processor::isDefaultCurve(blueCurve)) return false;
    return true;
}

bool Processor::isDefaultCurve(const std::vector<CurvePoint>& points) {
    if (points.size() != 2) return false;
    return (std::fabs(points[0].x) < 0.1f && std::fabs(points[0].y) < 0.1f &&
            std::fabs(points[1].x - 255.0f) < 0.1f && std::fabs(points[1].y - 255.0f) < 0.1f);
}

// ─── Blur Helpers ────────────────────────────────────────────────────────────

std::vector<float> Processor::computeBlurredLuma(const std::vector<float>& data,
                                                  int w, int h, int channels,
                                                  int radius) {
    // Extract luminance
    std::vector<float> luma(static_cast<size_t>(w) * h);
    #pragma omp parallel for schedule(static)
    for (int i = 0; i < w * h; ++i) {
        size_t idx = static_cast<size_t>(i) * channels;
        if (channels >= 3) {
            luma[i] = getLuma(data[idx], data[idx + 1], data[idx + 2]);
        } else {
            luma[i] = data[idx];
        }
    }

    // Two-pass separable box blur (fast approximation of Gaussian)
    // 3 iterations of box blur ≈ Gaussian
    std::vector<float> temp(luma.size());
    auto boxBlurH = [&](const std::vector<float>& src, std::vector<float>& dst) {
        #pragma omp parallel for schedule(static)
        for (int y = 0; y < h; ++y) {
            float sum = 0.0f;
            int count = 0;
            // Init window
            for (int x = 0; x <= radius && x < w; ++x) {
                sum += src[y * w + x];
                count++;
            }
            for (int x = 0; x < w; ++x) {
                dst[y * w + x] = sum / count;
                // Expand right
                int right = x + radius + 1;
                if (right < w) { sum += src[y * w + right]; count++; }
                // Shrink left
                int left = x - radius;
                if (left >= 0) { sum -= src[y * w + left]; count--; }
            }
        }
    };
    auto boxBlurV = [&](const std::vector<float>& src, std::vector<float>& dst) {
        #pragma omp parallel for schedule(static)
        for (int x = 0; x < w; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int y = 0; y <= radius && y < h; ++y) {
                sum += src[y * w + x];
                count++;
            }
            for (int y = 0; y < h; ++y) {
                dst[y * w + x] = sum / count;
                int bottom = y + radius + 1;
                if (bottom < h) { sum += src[bottom * w + x]; count++; }
                int top = y - radius;
                if (top >= 0) { sum -= src[top * w + x]; count--; }
            }
        }
    };

    for (int pass = 0; pass < 3; ++pass) {
        boxBlurH(luma, temp);
        boxBlurV(temp, luma);
    }
    return luma;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Individual Adjustment Functions (per-pixel, ported from shader.wgsl)
// ═══════════════════════════════════════════════════════════════════════════════

// ─── Exposure ────────────────────────────────────────────────────────────────
void Processor::applyLinearExposure(float& r, float& g, float& b, float exposure) {
    if (exposure == 0.0f) return;
    float mult = std::pow(2.0f, exposure);
    r *= mult; g *= mult; b *= mult;
}

// ─── Brightness (Filmic) ─────────────────────────────────────────────────────
void Processor::applyFilmicExposure(float& r, float& g, float& b, float brightness) {
    if (brightness == 0.0f) return;
    constexpr float RATIONAL_CURVE_MIX = 0.95f;
    constexpr float MIDTONE_STRENGTH = 1.2f;

    float origLuma = getLuma(r, g, b);
    if (std::fabs(origLuma) < 0.00001f) return;

    float directAdj = brightness * (1.0f - RATIONAL_CURVE_MIX);
    float rationalAdj = brightness * RATIONAL_CURVE_MIX;
    float scale = std::pow(2.0f, directAdj);
    float k = std::pow(2.0f, -rationalAdj * MIDTONE_STRENGTH);

    float lumaAbs = std::fabs(origLuma);
    float lumaFloor = std::floor(lumaAbs);
    float lumaFract = lumaAbs - lumaFloor;
    float shapedFract = lumaFract / (lumaFract + (1.0f - lumaFract) * k);
    float shapedLumaAbs = lumaFloor + shapedFract;
    float sign = (origLuma >= 0.0f) ? 1.0f : -1.0f;
    float newLuma = sign * shapedLumaAbs * scale;

    float chromaR = r - origLuma;
    float chromaG = g - origLuma;
    float chromaB = b - origLuma;
    float totalLumaScale = newLuma / origLuma;
    float chromaScale = std::pow(std::fabs(totalLumaScale), 0.8f);
    if (totalLumaScale < 0.0f) chromaScale = -chromaScale;

    r = newLuma + chromaR * chromaScale;
    g = newLuma + chromaG * chromaScale;
    b = newLuma + chromaB * chromaScale;
}

// ─── Shadow multiplier helper ────────────────────────────────────────────────
static float getShadowMult(float luma, float sh, float bl) {
    float mult = 1.0f;
    float safeLuma = std::max(luma, 0.0001f);

    if (bl != 0.0f) {
        // Broader black-region support than strict RAW-domain thresholds,
        // so the control remains effective on normalized images too.
        constexpr float limit = 0.22f;
        if (safeLuma < limit) {
            float x = safeLuma / limit;
            float mask = std::pow(1.0f - x, 1.6f);
            float factor = std::clamp(std::pow(2.0f, bl * 0.95f), 0.2f, 4.5f);
            mult *= (1.0f - mask) + mask * factor; // mix(1, factor, mask)
        }
    }
    if (sh != 0.0f) {
        constexpr float limit = 0.38f;
        if (safeLuma < limit) {
            float x = safeLuma / limit;
            float mask = std::pow(1.0f - x, 1.35f);
            float factor = std::clamp(std::pow(2.0f, sh * 1.25f), 0.2f, 4.5f);
            mult *= (1.0f - mask) + mask * factor;
        }
    }
    return mult;
}

// ─── Tonal Adjustments ───────────────────────────────────────────────────────
void Processor::applyTonalAdjustments(float& r, float& g, float& b,
                                      float blurredLuma,
                                      float contrast, float shadows,
                                      float whites, float blacks) {
    // Whites
    if (whites != 0.0f) {
        float whiteLevel = 1.0f - whites * 0.25f;
        float wMult = 1.0f / std::max(whiteLevel, 0.01f);
        r *= wMult; g *= wMult; b *= wMult;
        blurredLuma *= wMult;
    }

    float pixelLuma = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));
    float safePixelLuma = std::max(pixelLuma, 0.0001f);
    float safeBlurredLuma = std::max(blurredLuma, 0.0001f);

    // Halo protection
    float percPixel = std::sqrt(safePixelLuma);
    float percBlurred = std::sqrt(safeBlurredLuma);
    float edgeDiff = std::fabs(percPixel - percBlurred);
    float haloProt = smoothstep(0.05f, 0.25f, edgeDiff);

    // Shadows + Blacks
    if (shadows != 0.0f || blacks != 0.0f) {
        float spatialMult = getShadowMult(safeBlurredLuma, shadows, blacks);
        float pixelMult   = getShadowMult(safePixelLuma, shadows, blacks);
        float finalMult = spatialMult + haloProt * (pixelMult - spatialMult);
        r *= finalMult; g *= finalMult; b *= finalMult;
    }

    // Contrast
    if (contrast != 0.0f) {
        float safeR = std::max(r, 0.0f), safeG = std::max(g, 0.0f), safeB = std::max(b, 0.0f);
        constexpr float gamma = 2.2f;
        float pR = std::clamp(std::pow(safeR, 1.0f / gamma), 0.0f, 1.0f);
        float pG = std::clamp(std::pow(safeG, 1.0f / gamma), 0.0f, 1.0f);
        float pB = std::clamp(std::pow(safeB, 1.0f / gamma), 0.0f, 1.0f);

        float strength = std::pow(2.0f, contrast * 1.25f);
        auto curveChannel = [strength](float p) -> float {
            if (p < 0.5f) return 0.5f * std::pow(2.0f * p, strength);
            else          return 1.0f - 0.5f * std::pow(2.0f * (1.0f - p), strength);
        };
        float cR = std::pow(curveChannel(pR), gamma);
        float cG = std::pow(curveChannel(pG), gamma);
        float cB = std::pow(curveChannel(pB), gamma);

        // Protect super-bright
        float mR = smoothstep(1.0f, 1.01f, safeR);
        float mG = smoothstep(1.0f, 1.01f, safeG);
        float mB = smoothstep(1.0f, 1.01f, safeB);
        r = cR + mR * (r - cR);
        g = cG + mG * (g - cG);
        b = cB + mB * (b - cB);
    }
}

// ─── Highlights ──────────────────────────────────────────────────────────────
void Processor::applyHighlightsAdjustment(float& r, float& g, float& b,
                                          float blurredLuma, float highlights) {
    (void)blurredLuma;
    if (highlights == 0.0f) return;

    float pixelLuma = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));
    float safePixelLuma = std::max(pixelLuma, 0.0001f);

    float maskInput = std::tanh(safePixelLuma * 1.5f);
    float hlMask = smoothstep(0.3f, 0.95f, maskInput);
    if (hlMask < 0.001f) return;

    float luma = pixelLuma;
    float adjR, adjG, adjB;

    if (highlights < 0.0f) {
        float newLuma;
        if (luma <= 1.0f) {
            float gamma = 1.0f - highlights * 1.75f;
            newLuma = std::pow(luma, gamma);
        } else {
            float excess = luma - 1.0f;
            float compStr = -highlights * 6.0f;
            float compExcess = excess / (1.0f + excess * compStr);
            newLuma = 1.0f + compExcess;
        }
        float ratio = newLuma / std::max(luma, 0.0001f);
        adjR = r * ratio; adjG = g * ratio; adjB = b * ratio;
        float desatAmt = smoothstep(1.0f, 10.0f, luma);
        adjR = adjR + desatAmt * (newLuma - adjR);
        adjG = adjG + desatAmt * (newLuma - adjG);
        adjB = adjB + desatAmt * (newLuma - adjB);
    } else {
        float factor = std::pow(2.0f, highlights * 1.75f);
        adjR = r * factor; adjG = g * factor; adjB = b * factor;
    }

    r = r + hlMask * (adjR - r);
    g = g + hlMask * (adjG - g);
    b = b + hlMask * (adjB - b);
}

// ─── White Balance ───────────────────────────────────────────────────────────
void Processor::applyWhiteBalance(float& r, float& g, float& b, float temp, float tnt) {
    if (temp == 0.0f && tnt == 0.0f) return;
    float tR = 1.0f + temp * 0.2f;
    float tG = 1.0f + temp * 0.05f;
    float tB = 1.0f - temp * 0.2f;
    float tnR = 1.0f + tnt * 0.25f;
    float tnG = 1.0f - tnt * 0.25f;
    float tnB = 1.0f + tnt * 0.25f;
    r *= tR * tnR;
    g *= tG * tnG;
    b *= tB * tnB;
}

// ─── Creative Color (Saturation + Vibrance) ──────────────────────────────────
void Processor::applyCreativeColor(float& r, float& g, float& b, float sat, float vib) {
    float luma = getLuma(r, g, b);

    if (sat != 0.0f) {
        r = luma + (r - luma) * (1.0f + sat);
        g = luma + (g - luma) * (1.0f + sat);
        b = luma + (b - luma) * (1.0f + sat);
    }

    if (vib == 0.0f) return;

    float cMax = std::max({r, g, b});
    float cMin = std::min({r, g, b});
    float delta = cMax - cMin;
    if (delta < 0.02f) return;

    float currentSat = delta / std::max(cMax, 0.001f);
    luma = getLuma(r, g, b);

    if (vib > 0.0f) {
        float satMask = 1.0f - smoothstep(0.4f, 0.9f, currentSat);
        float h2, s2, v2;
        rgbToHsv(r, g, b, h2, s2, v2);
        float skinCenter = 25.0f;
        float hueDist = std::min(std::fabs(h2 - skinCenter), 360.0f - std::fabs(h2 - skinCenter));
        float isSkin = smoothstep(35.0f, 10.0f, hueDist);
        float skinDampener = 1.0f - isSkin * 0.4f; // mix(1.0, 0.6, isSkin)
        float amount = vib * satMask * skinDampener * 3.0f;
        r = luma + (r - luma) * (1.0f + amount);
        g = luma + (g - luma) * (1.0f + amount);
        b = luma + (b - luma) * (1.0f + amount);
    } else {
        float desatMask = 1.0f - smoothstep(0.2f, 0.8f, currentSat);
        float amount = vib * desatMask;
        r = luma + (r - luma) * (1.0f + amount);
        g = luma + (g - luma) * (1.0f + amount);
        b = luma + (b - luma) * (1.0f + amount);
    }
}

// ─── HSL Panel ───────────────────────────────────────────────────────────────
static float getRawHslInfluence(float hue, float center, float width) {
    float dist = std::min(std::fabs(hue - center), 360.0f - std::fabs(hue - center));
    constexpr float sharpness = 1.5f;
    float falloff = dist / (width * 0.5f);
    return std::exp(-sharpness * falloff * falloff);
}

void Processor::applyHSLPanel(float& r, float& g, float& b,
                              const std::array<HslAdjustment, 8>& hsl) {
    // Skip nearly achromatic
    if (std::fabs(r - g) < 0.001f && std::fabs(g - b) < 0.001f) return;

    float h, s, v;
    rgbToHsv(r, g, b, h, s, v);
    float origLuma = getLuma(r, g, b);

    float satMask = smoothstep(0.05f, 0.20f, s);
    float lumWeight = smoothstep(0.0f, 1.0f, s);
    if (satMask < 0.001f && lumWeight < 0.001f) return;

    // Compute influences
    float rawInfluences[8];
    float total = 0.0f;
    for (int i = 0; i < 8; ++i) {
        rawInfluences[i] = getRawHslInfluence(h, HSL_RANGES[i].center, HSL_RANGES[i].width);
        total += rawInfluences[i];
    }
    if (total < 0.0001f) return;

    float totalHueShift = 0.0f;
    float totalSatMult = 0.0f;
    float totalLumAdj = 0.0f;

    for (int i = 0; i < 8; ++i) {
        float norm = rawInfluences[i] / total;
        float hueSatInfl = norm * satMask;
        float lumaInfl = norm * lumWeight;
        totalHueShift += hsl[i].hue * 2.0f * hueSatInfl;
        totalSatMult += hsl[i].saturation * hueSatInfl;
        totalLumAdj += hsl[i].luminance * lumaInfl;
    }

    if (s * (1.0f + totalSatMult) < 0.0001f) {
        float finalLuma = origLuma * (1.0f + totalLumAdj);
        r = g = b = finalLuma;
        return;
    }

    float newH = std::fmod(h + totalHueShift + 360.0f, 360.0f);
    float newS = std::clamp(s * (1.0f + totalSatMult), 0.0f, 1.0f);

    hsvToRgb(newH, newS, v, r, g, b);
    float newLuma = getLuma(r, g, b);
    float targetLuma = origLuma * (1.0f + totalLumAdj);
    if (newLuma > 0.0001f) {
        float ratio = targetLuma / newLuma;
        r *= ratio; g *= ratio; b *= ratio;
    } else {
        r = g = b = std::max(0.0f, targetLuma);
    }
}

// ─── Color Grading ───────────────────────────────────────────────────────────
void Processor::applyColorGrading(float& r, float& g, float& b,
                                  const ColorGradeSettings& shadows,
                                  const ColorGradeSettings& midtones,
                                  const ColorGradeSettings& highlights,
                                  float blending, float balance) {
    float luma = getLuma(std::max(r, 0.0f), std::max(g, 0.0f), std::max(b, 0.0f));

    float baseShadowCrossover = 0.1f;
    float baseHighCrossover = 0.5f;
    float balanceRange = 0.5f;
    float shadowCrossover = baseShadowCrossover + std::max(0.0f, -balance) * balanceRange;
    float highCrossover = baseHighCrossover - std::max(0.0f, balance) * balanceRange;
    float feather = 0.2f * blending;
    float finalShadowCrossover = std::min(shadowCrossover, highCrossover - 0.01f);

    float shadowMask = 1.0f - smoothstep(finalShadowCrossover - feather, finalShadowCrossover + feather, luma);
    float highMask = smoothstep(highCrossover - feather, highCrossover + feather, luma);
    float midMask = std::max(0.0f, 1.0f - shadowMask - highMask);

    auto applyTint = [&](const ColorGradeSettings& grade, float mask, float satStr, float lumStr) {
        if (grade.saturation > 0.001f) {
            float tR, tG, tB;
            hsvToRgb(grade.hue, 1.0f, 1.0f, tR, tG, tB);
            r += (tR - 0.5f) * grade.saturation * mask * satStr;
            g += (tG - 0.5f) * grade.saturation * mask * satStr;
            b += (tB - 0.5f) * grade.saturation * mask * satStr;
        }
        r += grade.luminance * mask * lumStr;
        g += grade.luminance * mask * lumStr;
        b += grade.luminance * mask * lumStr;
    };

    applyTint(shadows, shadowMask, 0.3f, 0.5f);
    applyTint(midtones, midMask, 0.6f, 0.8f);
    applyTint(highlights, highMask, 0.8f, 1.0f);
}

// ─── Dehaze ──────────────────────────────────────────────────────────────────
void Processor::applyDehaze(float& r, float& g, float& b, float amount) {
    if (amount == 0.0f) return;
    constexpr float aR = 0.95f, aG = 0.97f, aB = 1.0f;

    if (amount > 0.0f) {
        float dark = std::min({r, g, b});
        float transEst = 1.0f - dark;
        float t = 1.0f - amount * transEst;
        t = std::max(t, 0.1f);
        float recR = (r - aR) / t + aR;
        float recG = (g - aG) / t + aG;
        float recB = (b - aB) / t + aB;
        r = r + amount * (recR - r);
        g = g + amount * (recG - g);
        b = b + amount * (recB - b);
        // Slight contrast + saturation boost
        r = 0.5f + (r - 0.5f) * (1.0f + amount * 0.15f);
        g = 0.5f + (g - 0.5f) * (1.0f + amount * 0.15f);
        b = 0.5f + (b - 0.5f) * (1.0f + amount * 0.15f);
        float luma = getLuma(r, g, b);
        r = luma + (r - luma) * (1.0f + amount * 0.1f);
        g = luma + (g - luma) * (1.0f + amount * 0.1f);
        b = luma + (b - luma) * (1.0f + amount * 0.1f);
    } else {
        float aa = std::fabs(amount) * 0.7f;
        r = r + aa * (aR - r);
        g = g + aa * (aG - g);
        b = b + aa * (aB - b);
    }
}

// ─── Local Contrast ──────────────────────────────────────────────────────────
void Processor::applyLocalContrast(float& r, float& g, float& b,
                                   float blurredR, float blurredG, float blurredB,
                                   float amount, int mode) {
    if (amount == 0.0f) return;

    float centerLuma = getLuma(r, g, b);
    float shadowThreshold = 0.03f;
    float shadowProt = smoothstep(0.0f, shadowThreshold, centerLuma);
    float hlProt = 1.0f - smoothstep(0.9f, 1.0f, centerLuma);
    float midMask = shadowProt * hlProt;
    if (midMask < 0.001f) return;

    float blurredLuma = getLuma(blurredR, blurredG, blurredB);
    float safeCenterLuma = std::max(centerLuma, 0.0001f);
    float safeBlurredLuma = std::max(blurredLuma, 0.0001f);

    float finalR, finalG, finalB;

    if (amount < 0.0f) {
        // Soften
        float proj = safeBlurredLuma / safeCenterLuma;
        float bR = r * proj, bG = g * proj, bB = b * proj;
        float ba = -amount;
        if (mode == 0) ba *= 0.5f;
        finalR = r + ba * (bR - r);
        finalG = g + ba * (bG - g);
        finalB = b + ba * (bB - b);
    } else {
        float logRatio = std::log2(safeCenterLuma / safeBlurredLuma);
        float effAmount = amount;
        if (mode == 0) {
            float edgeMag = std::fabs(logRatio);
            float normEdge = std::clamp(edgeMag / 3.0f, 0.0f, 1.0f);
            float edgeDamp = 1.0f - std::sqrt(normEdge);
            effAmount = amount * edgeDamp * 0.8f;
        }
        float contrastFactor = std::pow(2.0f, logRatio * effAmount);
        finalR = r * contrastFactor;
        finalG = g * contrastFactor;
        finalB = b * contrastFactor;
    }

    r = r + midMask * (finalR - r);
    g = g + midMask * (finalG - g);
    b = b + midMask * (finalB - b);
}

// ─── Curves ──────────────────────────────────────────────────────────────────
static float interpolateCubicHermite(float x, const CurvePoint& p1, const CurvePoint& p2,
                                     float m1, float m2) {
    float dx = p2.x - p1.x;
    if (dx <= 0.0f) return p1.y;
    float t = (x - p1.x) / dx;
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 = t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 = t3 - t2;
    return h00 * p1.y + h10 * m1 * dx + h01 * p2.y + h11 * m2 * dx;
}

float Processor::applyCurve(float val, const std::vector<CurvePoint>& points) {
    int count = static_cast<int>(points.size());
    if (count < 2) return val;

    float x = val * 255.0f;
    if (x <= points[0].x) return points[0].y / 255.0f;
    if (x >= points[count - 1].x) return points[count - 1].y / 255.0f;

    for (int i = 0; i < count - 1; ++i) {
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        if (x <= p2.x) {
            int i0 = std::max(0, i - 1);
            int i3 = std::min(count - 1, i + 2);
            const auto& p0 = points[i0];
            const auto& p3 = points[i3];

            float deltaBefore  = (p1.y - p0.y) / std::max(0.001f, p1.x - p0.x);
            float deltaCurrent = (p2.y - p1.y) / std::max(0.001f, p2.x - p1.x);
            float deltaAfter   = (p3.y - p2.y) / std::max(0.001f, p3.x - p2.x);

            float tangentP1, tangentP2;
            if (i == 0) tangentP1 = deltaCurrent;
            else {
                tangentP1 = (deltaBefore * deltaCurrent <= 0.0f) ? 0.0f : (deltaBefore + deltaCurrent) / 2.0f;
            }
            if (i + 1 == count - 1) tangentP2 = deltaCurrent;
            else {
                tangentP2 = (deltaCurrent * deltaAfter <= 0.0f) ? 0.0f : (deltaCurrent + deltaAfter) / 2.0f;
            }

            // Monotonicity constraint
            if (deltaCurrent != 0.0f) {
                float alpha = tangentP1 / deltaCurrent;
                float beta  = tangentP2 / deltaCurrent;
                if (alpha * alpha + beta * beta > 9.0f) {
                    float tau = 3.0f / std::sqrt(alpha * alpha + beta * beta);
                    tangentP1 *= tau;
                    tangentP2 *= tau;
                }
            }

            float resultY = interpolateCubicHermite(x, p1, p2, tangentP1, tangentP2);
            return std::clamp(resultY / 255.0f, 0.0f, 1.0f);
        }
    }
    return points[count - 1].y / 255.0f;
}

void Processor::applyAllCurves(float& r, float& g, float& b,
                               const std::vector<CurvePoint>& luma,
                               const std::vector<CurvePoint>& red,
                               const std::vector<CurvePoint>& green,
                               const std::vector<CurvePoint>& blue) {
    bool redDef   = isDefaultCurve(red);
    bool greenDef = isDefaultCurve(green);
    bool blueDef  = isDefaultCurve(blue);
    bool rgbActive = !redDef || !greenDef || !blueDef;

    if (rgbActive) {
        float gR = applyCurve(r, red);
        float gG = applyCurve(g, green);
        float gB = applyCurve(b, blue);
        float lumaInitial = getLuma(r, g, b);
        float lumaTarget = applyCurve(lumaInitial, luma);
        float lumaGraded = getLuma(gR, gG, gB);
        if (lumaGraded > 0.001f) {
            float ratio = lumaTarget / lumaGraded;
            r = gR * ratio; g = gG * ratio; b = gB * ratio;
        } else {
            r = g = b = lumaTarget;
        }
        float maxComp = std::max({r, g, b});
        if (maxComp > 1.0f) { r /= maxComp; g /= maxComp; b /= maxComp; }
    } else {
        // Apply luma curve to all channels equally
        r = applyCurve(r, luma);
        g = applyCurve(g, luma);
        b = applyCurve(b, luma);
    }
}

// ─── Vignette ────────────────────────────────────────────────────────────────
void Processor::applyVignette(float& r, float& g, float& b,
                              int x, int y, int w, int h,
                              float amount, float midpoint,
                              float roundness, float feather) {
    if (amount == 0.0f) return;
    float aspect = static_cast<float>(h) / w;
    float uvX = (static_cast<float>(x) / w - 0.5f) * 2.0f;
    float uvY = (static_cast<float>(y) / h - 0.5f) * 2.0f;
    float vRound = 1.0f - roundness;
    float uvRx = (uvX >= 0 ? 1.0f : -1.0f) * std::pow(std::fabs(uvX), vRound);
    float uvRy = (uvY >= 0 ? 1.0f : -1.0f) * std::pow(std::fabs(uvY), vRound);
    float d = std::sqrt(uvRx * uvRx + uvRy * uvRy * aspect * aspect) * 0.5f;
    float vFeather = feather * 0.5f;
    float mask = smoothstep(midpoint - vFeather, midpoint + vFeather, d);
    if (amount < 0.0f) {
        float mult = 1.0f + amount * mask;
        r *= mult; g *= mult; b *= mult;
    } else {
        r = r + amount * mask * (1.0f - r);
        g = g + amount * mask * (1.0f - g);
        b = b + amount * mask * (1.0f - b);
    }
}

// ─── Grain ───────────────────────────────────────────────────────────────────
void Processor::applyGrain(float& r, float& g, float& b,
                           int x, int y,
                           float amount, float size, float roughness,
                           float scale) {
    if (amount <= 0.0f) return;
    float luma = std::max(0.0f, getLuma(r, g, b));
    float lumaMask = smoothstep(0.0f, 0.15f, luma) * (1.0f - smoothstep(0.6f, 1.0f, luma));
    float freq = (1.0f / std::max(size, 0.1f)) / scale;
    float baseCoordX = x * freq;
    float baseCoordY = y * freq;
    float roughCoordX = x * freq * 0.6f + 5.2f;
    float roughCoordY = y * freq * 0.6f + 1.3f;
    float noiseBase = gradientNoise(baseCoordX, baseCoordY);
    float noiseRough = gradientNoise(roughCoordX, roughCoordY);
    float noise = noiseBase + roughness * (noiseRough - noiseBase);
    float grainVal = noise * amount * 0.5f * lumaMask;
    r += grainVal; g += grainVal; b += grainVal;
}

// ─── Chromatic Aberration (whole-image) ──────────────────────────────────────
void Processor::applyChromaticAberration(std::vector<float>& data,
                                          int w, int h, int channels,
                                          float caRC, float caBY) {
    if (channels < 3) return;
    if (std::fabs(caRC) < 0.000001f && std::fabs(caBY) < 0.000001f) return;

    // Make a copy of the original for sampling
    std::vector<float> orig = data;
    float cx = w / 2.0f;
    float cy = h / 2.0f;

    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 0.001f) continue;
            float dirX = dx / dist;
            float dirY = dy / dist;

            // Red channel shift
            float redShiftX = dirX * dist * caRC;
            float redShiftY = dirY * dist * caRC;
            int rxc = std::clamp(static_cast<int>(std::round(x - redShiftX)), 0, w - 1);
            int ryc = std::clamp(static_cast<int>(std::round(y - redShiftY)), 0, h - 1);

            // Blue channel shift
            float blueShiftX = dirX * dist * caBY;
            float blueShiftY = dirY * dist * caBY;
            int bxc = std::clamp(static_cast<int>(std::round(x - blueShiftX)), 0, w - 1);
            int byc = std::clamp(static_cast<int>(std::round(y - blueShiftY)), 0, h - 1);

            size_t idx = (static_cast<size_t>(y) * w + x) * channels;
            data[idx + 0] = orig[(static_cast<size_t>(ryc) * w + rxc) * channels + 0]; // R
            // G stays from original position (already in data)
            data[idx + 1] = orig[(static_cast<size_t>(y) * w + x) * channels + 1];     // G
            data[idx + 2] = orig[(static_cast<size_t>(byc) * w + bxc) * channels + 2]; // B
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Main Processing Pipeline
// ═══════════════════════════════════════════════════════════════════════════════

void Processor::apply(ImageBuffer& buffer, const Params& params) {
    if (params.isIdentity()) return;

    ImageBuffer::WriteLock lock(&buffer); // Thread-safe against SwapManager
    int w = buffer.width();
    int h = buffer.height();
    int ch = buffer.channels();
    auto& data = buffer.data();

    // Pre-compute blurred luminance for tonal/local contrast ops
    bool needsBlur = (params.contrast != 0.0f || params.shadows != 0.0f ||
                      params.blacks != 0.0f || params.highlights != 0.0f ||
                      params.sharpness != 0.0f || params.clarity != 0.0f ||
                      params.structure != 0.0f);

    // Different blur radii for different operations
    constexpr float REF_DIM = 1080.0f;
    float scale = std::max(0.1f, std::min(static_cast<float>(w), static_cast<float>(h)) / REF_DIM);

    std::vector<float> tonalBlurred;
    std::vector<float> sharpnessBlurred;
    std::vector<float> clarityBlurred;
    std::vector<float> structureBlurred;

    if (needsBlur) {
        int tonalRadius = std::max(2, static_cast<int>(20 * scale));
        tonalBlurred = computeBlurredLuma(data, w, h, ch, tonalRadius);

        if (params.sharpness != 0.0f) {
            int sharpRadius = std::max(1, static_cast<int>(3 * scale));
            sharpnessBlurred = computeBlurredLuma(data, w, h, ch, sharpRadius);
        }
        if (params.clarity != 0.0f) {
            int clarRadius = std::max(2, static_cast<int>(30 * scale));
            clarityBlurred = computeBlurredLuma(data, w, h, ch, clarRadius);
        }
        if (params.structure != 0.0f) {
            int structRadius = std::max(1, static_cast<int>(5 * scale));
            structureBlurred = computeBlurredLuma(data, w, h, ch, structRadius);
        }
    }

    // 1. Chromatic Aberration (whole-image, before per-pixel)
    applyChromaticAberration(data, w, h, ch, params.caRedCyan, params.caBlueYellow);

    // Check which operations are needed for HSL / Color Grading
    bool needsHSL = false;
    for (auto& h2 : params.hsl) {
        if (h2.hue != 0.0f || h2.saturation != 0.0f || h2.luminance != 0.0f) { needsHSL = true; break; }
    }
    bool needsCG = (params.colorGradingShadows.saturation != 0.0f || params.colorGradingShadows.luminance != 0.0f ||
                    params.colorGradingMidtones.saturation != 0.0f || params.colorGradingMidtones.luminance != 0.0f ||
                    params.colorGradingHighlights.saturation != 0.0f || params.colorGradingHighlights.luminance != 0.0f);
    bool needsCurves = !isDefaultCurve(params.lumaCurve) || !isDefaultCurve(params.redCurve) ||
                       !isDefaultCurve(params.greenCurve) || !isDefaultCurve(params.blueCurve);

    // 2. Per-pixel processing
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = (static_cast<size_t>(y) * w + x) * ch;
            float r, g, b;
            if (ch >= 3) {
                r = data[idx]; g = data[idx + 1]; b = data[idx + 2];
            } else {
                r = g = b = data[idx];
            }

            // Local contrast (sharpness)
            if (params.sharpness != 0.0f && !sharpnessBlurred.empty()) {
                float bl = sharpnessBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl, params.sharpness, 0);
            }
            // Local contrast (clarity)
            if (params.clarity != 0.0f && !clarityBlurred.empty()) {
                float bl = clarityBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl, params.clarity, 1);
            }
            // Local contrast (structure)
            if (params.structure != 0.0f && !structureBlurred.empty()) {
                float bl = structureBlurred[y * w + x];
                applyLocalContrast(r, g, b, bl, bl, bl, params.structure, 1);
            }

            // Exposure
            applyLinearExposure(r, g, b, params.exposure);

            // Dehaze
            applyDehaze(r, g, b, params.dehaze);

            // White Balance
            applyWhiteBalance(r, g, b, params.temperature, params.tint);

            // Brightness
            applyFilmicExposure(r, g, b, params.brightness);

            // Tonal adjustments (contrast, shadows, whites, blacks)
            if (params.contrast != 0.0f || params.shadows != 0.0f ||
                params.whites != 0.0f || params.blacks != 0.0f) {
                float blurred = tonalBlurred.empty() ? getLuma(r, g, b) : tonalBlurred[y * w + x];
                applyTonalAdjustments(r, g, b, blurred, params.contrast,
                                      params.shadows, params.whites, params.blacks);
            }

            // Highlights
            if (params.highlights != 0.0f) {
                float blurred = tonalBlurred.empty() ? getLuma(r, g, b) : tonalBlurred[y * w + x];
                applyHighlightsAdjustment(r, g, b, blurred, params.highlights);
            }

            // HSL Panel
            if (needsHSL) {
                applyHSLPanel(r, g, b, params.hsl);
            }

            // Color Grading
            if (needsCG) {
                applyColorGrading(r, g, b,
                                  params.colorGradingShadows,
                                  params.colorGradingMidtones,
                                  params.colorGradingHighlights,
                                  params.colorGradingBlending,
                                  params.colorGradingBalance);
            }

            // Saturation + Vibrance
            applyCreativeColor(r, g, b, params.saturation, params.vibrance);

            // Convert to sRGB for curves/vignette/grain
            auto linearToSrgb = [](float c) -> float {
                c = std::clamp(c, 0.0f, 1.0f);
                return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            };
            r = linearToSrgb(r);
            g = linearToSrgb(g);
            b = linearToSrgb(b);

            // Curves
            if (needsCurves) {
                applyAllCurves(r, g, b, params.lumaCurve, params.redCurve,
                               params.greenCurve, params.blueCurve);
            }

            // Vignette
            applyVignette(r, g, b, x, y, w, h,
                          params.vignetteAmount, params.vignetteMidpoint,
                          params.vignetteRoundness, params.vignetteFeather);

            // Grain
            applyGrain(r, g, b, x, y,
                       params.grainAmount, params.grainSize,
                       params.grainRoughness, scale);

            // Convert back to linear
            auto srgbToLinear = [](float c) -> float {
                c = std::clamp(c, 0.0f, 1.0f);
                return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            };
            r = srgbToLinear(r);
            g = srgbToLinear(g);
            b = srgbToLinear(b);

            // Clamp output
            r = std::clamp(r, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b, 0.0f, 1.0f);

            // Write back
            if (ch >= 3) {
                data[idx] = r; data[idx + 1] = g; data[idx + 2] = b;
            } else {
                data[idx] = getLuma(r, g, b);
            }
        }
    }
}

// ─── Preview (reduced resolution) ────────────────────────────────────────────
std::vector<float> Processor::applyPreview(const ImageBuffer& buffer,
                                           const Params& params,
                                           int maxDim) {
    int w = buffer.width();
    int h = buffer.height();
    int ch = buffer.channels();

    // Compute scale factor
    float scaleFactor = 1.0f;
    if (w > maxDim || h > maxDim) {
        scaleFactor = static_cast<float>(maxDim) / std::max(w, h);
    }
    int pw = std::max(1, static_cast<int>(w * scaleFactor));
    int ph = std::max(1, static_cast<int>(h * scaleFactor));

    // Downsample to preview buffer
    const auto& srcData = buffer.data();
    std::vector<float> preview(static_cast<size_t>(pw) * ph * 3);

    #pragma omp parallel for schedule(static)
    for (int py = 0; py < ph; ++py) {
        for (int px = 0; px < pw; ++px) {
            int sx = static_cast<int>(px / scaleFactor);
            int sy = static_cast<int>(py / scaleFactor);
            sx = std::clamp(sx, 0, w - 1);
            sy = std::clamp(sy, 0, h - 1);
            size_t srcIdx = (static_cast<size_t>(sy) * w + sx) * ch;
            size_t dstIdx = (static_cast<size_t>(py) * pw + px) * 3;
            if (ch >= 3) {
                preview[dstIdx]     = srcData[srcIdx];
                preview[dstIdx + 1] = srcData[srcIdx + 1];
                preview[dstIdx + 2] = srcData[srcIdx + 2];
            } else {
                preview[dstIdx] = preview[dstIdx + 1] = preview[dstIdx + 2] = srcData[srcIdx];
            }
        }
    }

    // Create temp ImageBuffer for preview processing
    ImageBuffer previewBuf(pw, ph, 3);
    previewBuf.data() = std::move(preview);
    apply(previewBuf, params);

    return std::move(previewBuf.data());
}

} // namespace RawEditor
