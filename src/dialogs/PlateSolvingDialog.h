#ifndef PLATESOLVINGDIALOG_H
#define PLATESOLVINGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QCloseEvent>
#include "astrometry/SimbadSearcher.h"
#include "astrometry/NativePlateSolver.h"
#include "astrometry/AstapSolver.h"

// For passing the image buffer
#include "../ImageBuffer.h"
#include <QPointer>
#include "../ImageViewer.h"

#include "DialogBase.h"

class PlateSolvingDialog : public DialogBase {
    Q_OBJECT
public:
    explicit PlateSolvingDialog(QWidget* parent = nullptr);
    
    // Instead of image path, we pass the buffer for Native Solving
    void setImageBuffer(const ImageBuffer& img);
    void setViewer(class ImageViewer* v); // Added
    
    bool isSolved() const { return m_solved; }
    NativeSolveResult result() const { return m_result; }

private slots:
    void onSearchSimbad();
    void onSolve();
    void onCancel();
    void onSolverFinished(const NativeSolveResult& res);
    void onSolverLog(const QString& text);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QLineEdit* m_objectName;
    QLineEdit* m_raHint;
    QLineEdit* m_decHint;
    QLineEdit* m_fov;
    QLineEdit* m_focalLength;  // Focal length in mm
    QLineEdit* m_pixelSizeUm;  // Pixel size in micrometers
    QLineEdit* m_pixelScale;   // Calculated pixel scale arcsec/px
    
    void updateScaleFromMetadata(); // Auto-populate from metadata
    void calculatePixelScale();     // Calculate from focal length and pixel size
    QTextEdit* m_log;
    class QComboBox* m_engineCombo;
    QPushButton* m_solveBtn;
    QPushButton* m_cancelBtn;
    
    ImageBuffer m_image;
    QPointer<class ImageViewer> m_viewer; 
    QPointer<class ImageViewer> m_jobTarget; // Track viewer for async jobs
    
    SimbadSearcher* m_simbad;
    NativePlateSolver* m_solver;
    AstapSolver* m_astapSolver;
    
    
    bool m_solved = false;
    NativeSolveResult m_result;

    // Fallback logic
    bool m_isFallbackLoop = false;
    double m_lastRA = 0, m_lastDec = 0, m_lastRadius = 0, m_lastScale = 0;
};

#endif // PLATESOLVINGDIALOG_H
