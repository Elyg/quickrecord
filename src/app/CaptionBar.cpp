#include "app/CaptionBar.h"

#include "app/Settings.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QToolButton>

namespace qr
{

CaptionBar::CaptionBar(const Settings& settings, const QString& sizeTip, QWidget* parent)
    : QWidget(parent)
{
    m_text = new QLineEdit(this);
    m_text->setPlaceholderText("Text to burn into the export (optional)");
    m_text->setClearButtonEnabled(true);
    m_position = new QComboBox(this);
    for(const auto& [label, pos] : std::initializer_list<std::pair<const char*, TextPosition>>{
            {"Bottom right", TextPosition::BottomRight},
            {"Bottom center", TextPosition::BottomCenter},
            {"Bottom left", TextPosition::BottomLeft},
            {"Top right", TextPosition::TopRight},
            {"Top center", TextPosition::TopCenter},
            {"Top left", TextPosition::TopLeft}})
        m_position->addItem(label, toString(pos));
    m_position->setCurrentIndex(std::max(0, m_position->findData(settings.textPosition)));
    m_size = new QSpinBox(this);
    m_size->setRange(6, 400);
    m_size->setSuffix(" px");
    m_size->setValue(settings.textSize);
    m_size->setToolTip(sizeTip);
    m_colorButton = new QToolButton(this);
    m_colorButton->setToolTip("Text colour");
    m_background = new QCheckBox("Background", this);
    m_background->setToolTip("Semi-transparent box behind the text (otherwise a dark outline)");
    m_background->setChecked(settings.textBackground);
    setColor(QColor(settings.textColor));

    auto* label = new QLabel("Text", this);
    label->setStyleSheet("color: palette(placeholder-text);");

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(label);
    row->addWidget(m_text, 1);
    row->addWidget(m_position);
    row->addWidget(m_size);
    row->addWidget(m_colorButton);
    row->addWidget(m_background);

    connect(m_text, &QLineEdit::textChanged, this, &CaptionBar::changed);
    connect(m_position, &QComboBox::currentIndexChanged, this, &CaptionBar::changed);
    connect(m_size, &QSpinBox::valueChanged, this, &CaptionBar::changed);
    connect(m_background, &QCheckBox::toggled, this, &CaptionBar::changed);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, "Text colour", QColorDialog::ShowAlphaChannel);
        if(c.isValid())
            setColor(c);
    });
}

TextOverlay CaptionBar::overlay() const
{
    TextOverlay o;
    o.text = m_text->text();
    o.position = textPositionFromString(m_position->currentData().toString());
    o.fontSize = m_size->value();
    o.color = m_color;
    o.background = m_background->isChecked();
    return o;
}

void CaptionBar::setOverlay(const TextOverlay& overlay)
{
    m_text->setText(overlay.text);
    m_position->setCurrentIndex(std::max(0, m_position->findData(toString(overlay.position))));
    m_size->setValue(overlay.fontSize);
    m_background->setChecked(overlay.background);
    setColor(overlay.color);
}

void CaptionBar::storeStyle(Settings& settings) const
{
    const TextOverlay o = overlay();
    settings.textPosition = toString(o.position);
    settings.textSize = o.fontSize;
    settings.textColor = o.color.name(QColor::HexArgb);
    settings.textBackground = o.background;
}

void CaptionBar::setColor(const QColor& color)
{
    m_color = color.isValid() ? color : QColor(Qt::white);
    QPixmap swatch(18, 18);
    swatch.fill(m_color);
    m_colorButton->setIcon(QIcon(swatch));
    emit changed();
}

} // namespace qr
