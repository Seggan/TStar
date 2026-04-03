#ifndef MASKLAYER_H
#define MASKLAYER_H

// ============================================================================
// MaskLayer.h
// Data structure representing a single mask layer for selective processing.
// Stores normalized float data [0.0, 1.0] with blending and visibility state.
// ============================================================================

#include <vector>
#include <QString>

/**
 * @brief A single mask layer with pixel data and compositing properties.
 *
 * Masks are used to selectively apply processing operations to regions
 * of an image. Multiple layers can be composed via the MaskManager.
 */
struct MaskLayer {
    std::vector<float> data;        ///< Normalized mask values [0.0, 1.0]
    int     width    = 0;
    int     height   = 0;
    QString id;                     ///< Unique identifier
    QString name;                   ///< Human-readable display name
    QString mode     = "replace";   ///< Compositing mode ("replace", "multiply", etc.)
    bool    inverted = false;       ///< Whether the mask is logically inverted
    bool    visible  = true;        ///< Whether the mask is active in composition
    float   opacity  = 1.0f;        ///< Opacity factor [0.0, 1.0]

    /** Check whether the mask contains valid, properly-sized data. */
    bool isValid() const
    {
        return !data.empty()
            && width > 0
            && height > 0
            && data.size() == static_cast<size_t>(width * height);
    }

    /** Safely access a pixel value with bounds checking. */
    float pixel(int x, int y) const
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return 0.0f;
        return data[y * width + x];
    }
};

#endif // MASKLAYER_H