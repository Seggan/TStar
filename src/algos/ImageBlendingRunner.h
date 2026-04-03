#ifndef IMAGEBLENDINGRUNNER_H
#define IMAGEBLENDINGRUNNER_H

#include <QObject>
#include "../ImageBuffer.h"

// =============================================================================
// ImageBlendingParams -- configuration for a layer-blend operation
// =============================================================================
struct ImageBlendingParams {

    /// Supported compositing blend modes.
    enum BlendMode {
        Normal,
        Multiply,
        Screen,
        Overlay,
        Add,
        Subtract,
        Difference,
        SoftLight,
        HardLight
    };

    BlendMode mode    = Normal;
    float     opacity = 1.0f;       ///< Global opacity of the top layer [0..1]

    // -- Range masking --------------------------------------------------------
    float lowRange  = 0.0f;         ///< Low-end intensity threshold
    float highRange = 1.0f;         ///< High-end intensity threshold
    float feather   = 0.0f;         ///< Soft-edge width around the range bounds

    // -- Channel targeting (color + mono blending) ----------------------------
    /// 0 = Red, 1 = Green, 2 = Blue, 3 = All channels.
    int targetChannel = 3;
};

// =============================================================================
// ImageBlendingRunner -- composites two ImageBuffers using the specified mode
// =============================================================================
class ImageBlendingRunner : public QObject {
    Q_OBJECT

public:
    explicit ImageBlendingRunner(QObject* parent = nullptr);

    /**
     * Blend the top image onto the base image.
     *
     * @param base      Bottom (background) image.
     * @param top       Top (foreground) image -- must match base dimensions.
     * @param result    Receives the composited output.
     * @param params    Blend mode, opacity, masking, and channel settings.
     * @param errorMsg  If non-null, receives a description on failure.
     * @return          true on success, false on dimensional mismatch.
     */
    bool run(const ImageBuffer&         base,
             const ImageBuffer&         top,
             ImageBuffer&               result,
             const ImageBlendingParams& params,
             QString*                   errorMsg = nullptr);

private:
    /// Compute a single blend-mode operation for one pixel-channel pair.
    float blendPixel(float b, float t,
                     ImageBlendingParams::BlendMode mode);
};

#endif // IMAGEBLENDINGRUNNER_H