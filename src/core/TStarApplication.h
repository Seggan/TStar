#ifndef TSTARAPPLICATION_H
#define TSTARAPPLICATION_H

// ============================================================================
// TStarApplication.h
// Custom QApplication subclass with global exception safety in the event loop.
// ============================================================================

#include <QApplication>

/**
 * @brief QApplication subclass that catches exceptions from the Qt event loop.
 *
 * Overrides notify() to wrap all event delivery in a try/catch block,
 * forwarding unhandled exceptions to GlobalExceptionHandler for logging
 * and user notification rather than allowing silent crashes.
 */
class TStarApplication : public QApplication
{
public:
    TStarApplication(int& argc, char** argv);
    virtual ~TStarApplication();

    /** Exception-safe event dispatch. */
    bool notify(QObject* receiver, QEvent* event) override;
};

#endif // TSTARAPPLICATION_H