#ifndef ABERRATIONINSPECTORDIALOG_H
#define ABERRATIONINSPECTORDIALOG_H

#include <array>

#include "DialogBase.h"
#include "../ImageBuffer.h"

class QLabel;

/**
 * @brief Dialog that displays a 3x3 grid of cropped image regions for
 *        evaluating optical aberrations across the field of view.
 *
 * Each panel shows a fixed-size crop centred on one of nine equally-spaced
 * positions (corners, edges, and centre). All crops are auto-stretched for
 * visibility and scaled to a fixed display size.
 */
class AberrationInspectorDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit AberrationInspectorDialog(const ImageBuffer& img,
                                       QWidget* parent = nullptr);

    void setSource(const ImageBuffer& img);

private:
    void   setupUi();
    void   updatePanels();

    /**
     * @brief Extracts a square crop of @p size pixels centred on (@p cx, @p cy),
     *        clamped so the crop stays within the image boundaries.
     * @return Auto-stretched QImage of the cropped region.
     */
    QImage cropToQImage(int cx, int cy, int size);

    ImageBuffer              m_source;

    // Nine panel labels arranged as: TL TC TR / ML MC MR / BL BC BR
    std::array<QLabel*, 9>   m_panels;
};

#endif // ABERRATIONINSPECTORDIALOG_H