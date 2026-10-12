#include "app/Theme.h"

#include "app/Settings.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace qr::theme
{

namespace
{
struct Tokens
{
    const char* window;   // window background
    const char* input;    // lists, text fields
    const char* raised;   // buttons, menus, popups
    const char* hover;
    const char* pressed;
    const char* border;
    const char* borderStrong;
    const char* text;
    const char* muted;
    const char* disabled;
    const char* accentSoft; // selection / checked background
    const char* scroll;
    const char* scrollHover;
    const char* button; // push and tool buttons: clearly raised from the window
    const char* buttonHover;
    const char* buttonBorder;
};

constexpr const char* kAccent = "#e5484d";
constexpr const char* kAccentHover = "#ec5d61";

constexpr Tokens kDark{
    "#24272e", "#1c1f25", "#2f333c", "#383d47", "#424855", "#363a44", "#4c5260",
    "#e8eaee", "#9399a4", "#636975", "rgba(229, 72, 77, 0.26)", "#474d59", "#5a616f",
    "#3a3f4a", "#454b57", "#525967",
};

constexpr Tokens kLight{
    "#f4f5f7", "#ffffff", "#ffffff", "#eef0f3", "#e3e6ea", "#d9dce2", "#b9bec7",
    "#1d2025", "#6b7280", "#a3a8b1", "rgba(229, 72, 77, 0.14)", "#c7ccd4", "#a9afb9",
    "#ffffff", "#eef0f3", "#c2c7cf",
};

bool systemPrefersLight()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
}

QPalette makePalette(const Tokens& t)
{
    QPalette p;
    const QColor window(t.window), input(t.input), raised(t.raised), text(t.text), muted(t.muted);
    const QColor disabled(t.disabled), border(t.border), strong(t.borderStrong);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, input);
    p.setColor(QPalette::AlternateBase, raised);
    p.setColor(QPalette::Button, raised);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, raised);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Highlight, QColor(kAccent));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(kAccent));
    p.setColor(QPalette::Light, strong.lighter(120));
    p.setColor(QPalette::Midlight, strong);
    p.setColor(QPalette::Mid, border);
    p.setColor(QPalette::Dark, input.darker(130));
    p.setColor(QPalette::Shadow, QColor(0, 0, 0, 120));
    for(auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, border);
    return p;
}

enum class Glyph
{
    Up,
    Down,
    Check,
};

// Chevrons and the check mark. Stylesheets can only load images from files, so each is
// drawn once per colour (40×40, scaled down by the stylesheet) into the cache directory.
QString glyphImage(Glyph glyph, const QColor& color)
{
    const char* name = glyph == Glyph::Up ? "up" : (glyph == Glyph::Down ? "down" : "check");
    const QString dir = Settings::cacheDir() + "/theme";
    const QString path = QStringLiteral("%1/%2-%3.png").arg(dir, name, color.name().mid(1));
    if(QFileInfo::exists(path))
        return path;
    QDir().mkpath(dir);
    QImage img(40, 40, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath stroke;
    if(glyph == Glyph::Check)
    {
        stroke.moveTo(9, 21);
        stroke.lineTo(17, 29);
        stroke.lineTo(31, 12);
    }
    else
    {
        const qreal y0 = glyph == Glyph::Up ? 26 : 14, y1 = glyph == Glyph::Up ? 14 : 26;
        stroke.moveTo(8, y0);
        stroke.lineTo(20, y1);
        stroke.lineTo(32, y0);
    }
    p.drawPath(stroke);
    p.end();
    img.save(path);
    return path;
}

QString styleSheet(const Tokens& t)
{
    QString qss = QStringLiteral(R"(
QToolTip { color: @text; background: @raised; border: 1px solid @border; border-radius: 6px; padding: 4px 8px; }

QPushButton {
    background: @button; color: @text; border: 1px solid @buttonBorder; border-radius: 7px;
    padding: 6px 16px; min-height: 18px;
}
QPushButton:hover { background: @buttonHover; border-color: @borderStrong; }
QPushButton:pressed { background: @pressed; }
QPushButton:disabled { color: @disabled; background: @window; border-color: @border; }
QPushButton:default { background: @accent; color: white; border-color: @accent; font-weight: 600; }
QPushButton:default:hover { background: @accentHover; border-color: @accentHover; }
QPushButton:default:disabled { background: @accentSoft; color: @disabled; border-color: transparent; }

QToolButton { color: @text; background: @button; border: 1px solid @buttonBorder; border-radius: 6px; padding: 4px 9px; }
QToolButton:hover { background: @buttonHover; border-color: @borderStrong; }
QToolButton:pressed { background: @pressed; }
QToolButton:checked { background: @accentSoft; border-color: @accent; }
QToolButton:disabled { color: @disabled; background: @window; border-color: @border; }
/* Clear buttons inside text fields are tool buttons too: keep them bare. */
QLineEdit QToolButton, QLineEdit QToolButton:hover { background: transparent; border: none; padding: 0; }

QLineEdit, QComboBox, QSpinBox {
    background: @input; color: @text; border: 1px solid @border; border-radius: 7px;
    padding: 5px 8px; min-height: 18px;
    selection-background-color: @accent; selection-color: white;
}
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border-color: @borderStrong; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: @accent; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: @disabled; background: @window; border-color: @border; }

QComboBox { padding-right: 26px; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 24px; border: none; }
QComboBox::down-arrow { image: url(@arrowDown); width: 10px; height: 10px; }
QComboBox QAbstractItemView {
    background: @raised; color: @text; border: 1px solid @border; outline: 0; padding: 4px;
    selection-background-color: @accentSoft; selection-color: @text;
}

QSpinBox { padding-right: 24px; }
QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 22px; border: none; background: transparent; }
QSpinBox::up-button { subcontrol-position: top right; border-top-right-radius: 7px; }
QSpinBox::down-button { subcontrol-position: bottom right; border-bottom-right-radius: 7px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }
QSpinBox::up-arrow { image: url(@arrowUp); width: 9px; height: 9px; }
QSpinBox::down-arrow { image: url(@arrowDown); width: 9px; height: 9px; }

QTabWidget::pane { border: none; border-top: 1px solid @border; }
QTabBar::tab {
    background: transparent; color: @muted; border: none; border-bottom: 2px solid transparent;
    padding: 7px 16px; margin-right: 2px; font-weight: 600;
}
QTabBar::tab:hover { color: @text; }
QTabBar::tab:selected { color: @text; border-bottom-color: @accent; }

QListView { background: @input; border: 1px solid @border; border-radius: 10px; padding: 6px; outline: 0; }
QTabWidget QListView { border: none; border-radius: 0; }
QListView::item { color: @text; border-radius: 8px; padding: 4px; }
QListView::item:hover { background: @hover; }
QListView::item:selected { background: @accentSoft; color: @text; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: @scroll; border-radius: 3px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background: @scrollHover; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QMenu { background: @raised; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 14px; border-radius: 5px; }
QMenu::item:selected { background: @accentSoft; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QProgressBar { background: @raised; border: none; border-radius: 4px; }
QProgressBar::chunk { background: @accent; border-radius: 4px; }

QSlider::groove:horizontal { height: 4px; background: @pressed; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal {
    background: white; border: 2px solid @accent; width: 12px; height: 12px; margin: -6px 0; border-radius: 8px;
}
QSlider::handle:horizontal:disabled { border-color: @disabled; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @borderStrong; border-radius: 5px; background: @input; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@check); }
QCheckBox::indicator:checked:hover { background: @accentHover; border-color: @accentHover; }
QCheckBox::indicator:disabled { border-color: @border; background: @window; }
QCheckBox:disabled { color: @disabled; }

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
)");
    // Longest names first so "@borderStrong" isn't eaten by "@border".
    const std::initializer_list<std::pair<const char*, QString>> tokens = {
        {"@buttonBorder", t.buttonBorder}, {"@buttonHover", t.buttonHover}, {"@button", t.button},
        {"@borderStrong", t.borderStrong}, {"@accentHover", kAccentHover}, {"@accentSoft", t.accentSoft},
        {"@scrollHover", t.scrollHover},   {"@arrowDown", glyphImage(Glyph::Down, QColor(t.muted))},
        {"@arrowUp", glyphImage(Glyph::Up, QColor(t.muted))},
        {"@check", glyphImage(Glyph::Check, Qt::white)},
        {"@disabled", t.disabled},         {"@pressed", t.pressed},
        {"@raised", t.raised},             {"@window", t.window},
        {"@border", t.border},             {"@scroll", t.scroll},
        {"@accent", kAccent},              {"@hover", t.hover},
        {"@input", t.input},               {"@muted", t.muted},
        {"@text", t.text},
    };
    for(const auto& [name, value] : tokens)
        qss.replace(QLatin1String(name), value);
    return qss;
}

QString g_mode;
bool g_watching = false;
} // namespace

void apply(const QString& mode)
{
    g_mode = mode;
    if(!g_watching)
    {
        g_watching = true;
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] {
            if(g_mode == "system")
                apply(g_mode);
        });
    }

    const bool light = mode == "light" || (mode == "system" && systemPrefersLight());
    const Tokens& t = light ? kLight : kDark;
    if(QApplication::style()->name().compare("fusion", Qt::CaseInsensitive) != 0)
        QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(makePalette(t));
    qApp->setStyleSheet(styleSheet(t));
}

} // namespace qr::theme
