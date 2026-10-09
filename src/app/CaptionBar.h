#pragma once

#include "core/TextOverlay.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QToolButton;

namespace qr
{

struct Settings;

// "Text [caption…] [position] [size] [colour] [Background]" — the caption controls shared
// by the recording and screenshot editors. Starts with the style last used (Settings).
class CaptionBar : public QWidget
{
    Q_OBJECT

public:
    CaptionBar(const Settings& settings, const QString& sizeTip, QWidget* parent = nullptr);

    TextOverlay overlay() const;
    void setOverlay(const TextOverlay& overlay);
    // Remembers the current style (not the text) as the default for the next caption.
    void storeStyle(Settings& settings) const;

signals:
    void changed();

private:
    void setColor(const QColor& color);

    QLineEdit* m_text;
    QComboBox* m_position;
    QSpinBox* m_size;
    QToolButton* m_colorButton;
    QCheckBox* m_background;
    QColor m_color;
};

} // namespace qr
