#ifndef MASKMANAGER_H
#define MASKMANAGER_H

// ============================================================================
// MaskManager.h
// Singleton registry for named mask layers used across the application.
// ============================================================================

#include <QObject>
#include <QMap>
#include <QString>
#include "MaskLayer.h"

/**
 * @brief Centralized registry for mask layers.
 *
 * Provides thread-safe storage and retrieval of named MaskLayer instances.
 * Emits masksChanged() whenever the collection is modified.
 */
class MaskManager : public QObject {
    Q_OBJECT

public:
    static MaskManager& instance()
    {
        static MaskManager inst;
        return inst;
    }

    /** Add or replace a mask layer with the given name. */
    void addMask(const QString& name, const MaskLayer& mask)
    {
        MaskLayer m = mask;
        m.name = name;
        m_masks[name] = m;
        emit masksChanged();
    }

    /** Remove a mask layer by name. No-op if not found. */
    void removeMask(const QString& name)
    {
        if (m_masks.remove(name)) {
            emit masksChanged();
        }
    }

    /** Retrieve a mask layer by name. Returns default-constructed if not found. */
    MaskLayer getMask(const QString& name) const
    {
        return m_masks.value(name);
    }

    /** Get the list of all registered mask names. */
    QStringList getMaskNames() const
    {
        return m_masks.keys();
    }

    /** Get a copy of the entire mask collection. */
    QMap<QString, MaskLayer> getAllMasks() const
    {
        return m_masks;
    }

signals:
    /** Emitted whenever a mask is added or removed. */
    void masksChanged();

private:
    MaskManager()  = default;
    ~MaskManager() = default;
    MaskManager(const MaskManager&)            = delete;
    MaskManager& operator=(const MaskManager&) = delete;

    QMap<QString, MaskLayer> m_masks;
};

#endif // MASKMANAGER_H