#include "app/ScreenshotEditor.h"

#include "app/CaptionBar.h"
#include "app/Clipboard.h"
#include "app/FlowLayout.h"
#include "app/Icons.h"
#include "app/Settings.h"
#include "app/VideoView.h"

#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace qr
{

namespace
{
struct ToolInfo
{
    std::optional<AnnotationTool> tool; // nullopt = Move
    const char* glyph;                  // the button shows only this; the name is in the tooltip
    const char* name;
    const char* key;
    const char* tip;
};
const ToolInfo kTools[] = {
    {std::nullopt, "✥", "Move", "V", "Select a drawing, then drag it, nudge it with the arrow keys or remove it with "
                                     "Delete; colour and width changes apply to it"},
    {AnnotationTool::Pen, "✎", "Pen", "P", "Freehand"},
    {AnnotationTool::Line, "╱", "Line", "L", "Straight line"},
    {AnnotationTool::Arrow, "➜", "Arrow", "A", "Arrow pointing where you release"},
    {AnnotationTool::Rect, "▢", "Box", "B", "Rectangle outline"},
    {AnnotationTool::Ellipse, "◯", "Ellipse", "E", "Ellipse outline"},
    {AnnotationTool::Marker, "▍", "Marker", "M", "See-through highlighter"},
    {AnnotationTool::Pixelate, "▦", "Pixelate", "X", "Hide an area behind big pixels"},
    {AnnotationTool::Text, "T", "Text", "T", "Click where the text should start, or pick a corner for it beside the "
                                             "width"},
};
} // namespace

ScreenshotEditor::ScreenshotEditor(const HistoryEntry& entry, Settings& settings)
    : m_entry(entry)
    , m_settings(settings)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("QuickRecord — %1").arg(entry.id));
    setWindowIcon(icons::app());

    QImageReader reader(entry.master);
    m_image = reader.read();

    m_view = new VideoView(this);
    m_view->setMinimumSize(320, 180);
    m_view->setCropAlignment(1);
    m_view->setToolTip("Wheel: zoom · drag: pan · double-click / 0 / 1: fit ↔ 100%");

    m_cropEdit = new QToolButton(this);
    m_cropEdit->setText("Crop");
    m_cropEdit->setCheckable(true);
    m_cropEdit->setToolTip("Drag the handles on the preview; drag outside the box to draw a new one");
    m_cropReset = new QToolButton(this);
    m_cropReset->setText("↺");
    m_cropReset->setToolTip("Remove the crop (use the whole screenshot)");
    m_cropLabel = new QLabel(this);
    m_caption = new CaptionBar(settings, "Font size in pixels of the screenshot", this);
    m_caption->hide(); // restoreEdits() shows it for older screenshots with a caption

    // --- drawing tools ---
    for(const ToolInfo& t : kTools)
    {
        auto* b = new QToolButton(this);
        b->setText(t.glyph);
        b->setCheckable(true);
        b->setMinimumWidth(34);
        b->setToolTip(QStringLiteral("<b>%1</b> (%2)<br>%3 — click again to go back to pan/zoom").arg(t.name, t.key, t.tip));
        m_toolButtons << b;
    }
    m_drawColor = QColor(settings.drawColor);
    if(!m_drawColor.isValid())
        m_drawColor = QColor("#ff3b3f");
    m_drawColorButton = new QToolButton(this);
    m_drawColorButton->setToolTip("Drawing colour");
    m_drawColorButton->setIcon(icons::swatch(m_drawColor));
    m_drawWidth = new QSpinBox(this);
    m_drawWidth->setRange(1, 40);
    m_drawWidth->setSuffix(" px");
    m_drawWidth->setValue(std::clamp(settings.drawWidth, 1, 40));
    m_drawWidth->setToolTip("Line width in screenshot pixels; text, arrow heads and pixel blocks scale with it");
    m_textPlace = new QComboBox(this);
    m_textPlace->addItem("At click", QString());
    for(const auto& [label, pos] : std::initializer_list<std::pair<const char*, TextPosition>>{
            {"Bottom right", TextPosition::BottomRight},
            {"Bottom center", TextPosition::BottomCenter},
            {"Bottom left", TextPosition::BottomLeft},
            {"Top right", TextPosition::TopRight},
            {"Top center", TextPosition::TopCenter},
            {"Top left", TextPosition::TopLeft}})
        m_textPlace->addItem(label, toString(pos));
    m_textPlace->setToolTip("Where text goes: where you click, or a corner of the screenshot (it stays there when "
                            "the crop changes, until you move it)");
    m_textPlace->hide();
    m_undo = new QToolButton(this);
    m_undo->setText("↶");
    m_undo->setToolTip("Undo the last drawing change (Ctrl+Z)");
    m_redo = new QToolButton(this);
    m_redo->setText("↷");
    m_redo->setToolTip("Redo (Ctrl+Shift+Z)");
    m_clear = new QToolButton(this);
    m_clear->setText("Clear");
    m_clear->setToolTip("Remove all drawings (undoable)");

    m_status = new QLabel(this);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->setWordWrap(true);

    m_close = new QPushButton("Close", this);
    m_close->setToolTip("Close (Esc); the drawings, crop and text stay for next time");
    m_save = new QPushButton("Save && Close", this);
    m_save->setToolTip("Write the edited PNG next to the original and close (Ctrl+S); without edits it just closes");
    m_copy = new QPushButton("Copy", this);
    m_copy->setToolTip("Save the edited PNG, copy it to the clipboard and close (Ctrl+C); without edits it copies "
                       "the original");
    m_copy->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(m_view, 1);

    // One toolbar (wraps when the window is narrow): tools, crop, style, history.
    auto* toolbar = new FlowLayout(nullptr, 14, 6);
    auto* tools = new QHBoxLayout;
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(2);
    for(QToolButton* b : m_toolButtons)
        tools->addWidget(b);
    toolbar->addLayout(tools);
    toolbar->addLayout(group({m_cropEdit, m_cropLabel, m_cropReset}));
    toolbar->addLayout(group({m_drawColorButton, m_drawWidth, m_textPlace}));
    toolbar->addLayout(group({m_undo, m_redo, m_clear}, 2));
    root->addLayout(toolbar);
    // The Text tool covers captions; this row only shows for screenshots that have one.
    root->addWidget(m_caption);
    root->addWidget(m_status);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_close);
    buttons->addSpacing(12);
    buttons->addWidget(m_save);
    buttons->addWidget(m_copy);
    root->addLayout(buttons);

    // --- wiring ---
    m_editsTimer.setSingleShot(true);
    m_editsTimer.setInterval(400);
    connect(&m_editsTimer, &QTimer::timeout, this, [this] { emit editsChanged(m_entry.id, currentEdits()); });

    connect(m_cropEdit, &QToolButton::toggled, this, [this](bool on) {
        if(on)
            setTool(-1); // cropping and drawing share the mouse
        m_view->setCropEditing(on);
        if(on)
            m_view->setFocus();
    });

    for(int i = 0; i < int(m_toolButtons.size()); ++i)
    {
        connect(m_toolButtons[i], &QToolButton::clicked, this, [this, i](bool checked) { setTool(checked ? i : -1); });
        connect(new QShortcut(QKeySequence(kTools[i].key), this), &QShortcut::activated, this,
                [this, i] { setTool(m_activeTool == i ? -1 : i); });
    }
    m_view->installEventFilter(this);
    connect(m_drawColorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_drawColor, this, "Drawing colour", QColorDialog::ShowAlphaChannel);
        if(!c.isValid())
            return;
        m_drawColor = c;
        m_drawColorButton->setIcon(icons::swatch(c));
        m_settings.drawColor = c.name(QColor::HexArgb);
        m_settings.save();
        editSelected([c](Annotation& a) { a.color = c; });
    });
    connect(m_drawWidth, &QSpinBox::valueChanged, this, [this](int w) {
        m_settings.drawWidth = w;
        m_settings.save();
        editSelected([w](Annotation& a) { a.width = w; });
    });
    connect(m_textPlace, &QComboBox::activated, this, [this] {
        const QString place = m_textPlace->currentData().toString();
        if(selectedText())
        {
            // "At click" keeps it where it is, free to move.
            editSelected([&place](Annotation& a) {
                a.anchor = place.isEmpty() ? std::nullopt : std::optional(textPositionFromString(place));
            });
            return;
        }
        m_settings.textPlace = place;
        m_settings.save();
        if(!place.isEmpty() && drawingTool() == AnnotationTool::Text)
            addText({}); // a corner needs no click
    });
    connect(m_undo, &QToolButton::clicked, this, &ScreenshotEditor::undo);
    connect(m_redo, &QToolButton::clicked, this, &ScreenshotEditor::redo);
    connect(m_clear, &QToolButton::clicked, this, [this] {
        if(!m_annotations.isEmpty())
            changeAnnotations({});
    });
    connect(m_view, &VideoView::drawPressed, this, &ScreenshotEditor::onDrawPressed);
    connect(m_view, &VideoView::drawMoved, this, &ScreenshotEditor::onDrawMoved);
    connect(m_view, &VideoView::drawReleased, this, &ScreenshotEditor::onDrawReleased);
    connect(new QShortcut(QKeySequence::Undo, this), &QShortcut::activated, this, &ScreenshotEditor::undo);
    connect(new QShortcut(QKeySequence("Ctrl+Shift+Z"), this), &QShortcut::activated, this, &ScreenshotEditor::redo);
    connect(new QShortcut(QKeySequence("Ctrl+Y"), this), &QShortcut::activated, this, &ScreenshotEditor::redo);
    connect(m_cropReset, &QToolButton::clicked, this, [this] {
        setCrop({});
        m_view->setCrop({});
    });
    connect(m_view, &VideoView::cropChanged, this, &ScreenshotEditor::setCrop);
    connect(m_caption, &CaptionBar::changed, this, &ScreenshotEditor::updateOverlay);

    connect(m_close, &QPushButton::clicked, this, &QWidget::close);
    connect(m_save, &QPushButton::clicked, this, [this] { finish(Action::Save); });
    connect(m_copy, &QPushButton::clicked, this, [this] { finish(Action::Copy); });
    connect(new QShortcut(QKeySequence("Ctrl+S"), this), &QShortcut::activated, this, [this] { finish(Action::Save); });
    connect(new QShortcut(QKeySequence("Ctrl+C"), this), &QShortcut::activated, this, [this] { finish(Action::Copy); });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] {
        if(m_selected >= 0)
            select(-1);
        else if(m_activeTool >= 0)
            setTool(-1); // first Esc puts the pen down
        else
            close();
    });

    // --- initial state ---
    m_base = m_image;
    m_view->setImage(m_image);
    setCrop({});
    if(entry.edits)
        restoreEdits(*entry.edits);

    // Size the window around the screenshot's aspect ratio.
    const QSize src = entry.size.isValid() ? entry.size : QSize(16, 9);
    const QSize view = src.scaled(QSize(1100, 660), Qt::KeepAspectRatio).expandedTo(QSize(480, 200));
    resize(std::max(view.width(), 980), view.height() + 170);
    updateDrawButtons();

    if(m_image.isNull())
    {
        m_status->setText(QStringLiteral("Cannot read the screenshot %1: %2").arg(entry.master, reader.errorString()));
        for(QWidget* w : std::initializer_list<QWidget*>{m_copy, m_save, m_cropEdit, m_caption})
            w->setEnabled(false);
        for(QToolButton* b : m_toolButtons)
            b->setEnabled(false);
    }
}

QSize ScreenshotEditor::outputSize() const
{
    return m_crop.isEmpty() ? m_image.size() : m_crop.size();
}

QRect ScreenshotEditor::frame() const
{
    return m_crop.isEmpty() ? m_image.rect() : m_crop;
}

QImage ScreenshotEditor::composed() const
{
    QImage out = (m_crop.isEmpty() ? m_base : m_base.copy(m_crop)).convertToFormat(QImage::Format_RGB32);
    const TextOverlay overlay = m_caption->overlay();
    if(!overlay.isEmpty())
    {
        QPainter p(&out);
        p.drawImage(0, 0, renderTextOverlay(overlay, out.size()));
    }
    return out;
}

bool ScreenshotEditor::hasEdits() const
{
    return !currentEdits().isDefault();
}

void ScreenshotEditor::finish(Action action)
{
    if(m_image.isNull())
        return;
    QString output = m_entry.master;
    if(hasEdits())
    {
        // Replaced whole: a half-written PNG never shows up in the folder.
        output = editedPath(m_entry, "png");
        QSaveFile f(output);
        if(!f.open(QIODevice::WriteOnly) || !composed().save(&f, "PNG") || !f.commit())
        {
            m_status->setText("Could not write " + output);
            emit notify("Save failed", output, true);
            return;
        }
        emit exported(m_entry.id, output);
    }
    if(action == Action::Copy)
    {
        copyFileToClipboard(output);
        emit notify("Copied to clipboard",
                    QStringLiteral("%1 (%2)").arg(QFileInfo(output).fileName(),
                                                  QLocale().formattedDataSize(QFileInfo(output).size(), 1)),
                    false);
    }
    close();
}

void ScreenshotEditor::updateOverlay()
{
    const TextOverlay o = m_caption->overlay();
    m_view->setOverlay(o.isEmpty() ? QImage() : renderTextOverlay(o, outputSize()));
    m_caption->storeStyle(m_settings);
    m_settings.save();
    scheduleEditsSave();
}

EditState ScreenshotEditor::currentEdits() const
{
    EditState s;
    s.crop = m_crop;
    s.text = m_caption->overlay();
    s.annotations = m_annotations;
    return s;
}

void ScreenshotEditor::restoreEdits(const EditState& edits)
{
    m_restoring = true;
    m_caption->setOverlay(edits.text);
    m_caption->setVisible(!edits.text.isEmpty());
    m_annotations = edits.annotations;
    rebuildBase();
    setCrop(edits.crop);
    m_view->setCrop(m_crop);
    m_restoring = false;
}

void ScreenshotEditor::scheduleEditsSave()
{
    if(!m_restoring)
        m_editsTimer.start();
}

void ScreenshotEditor::setCrop(const QRect& crop)
{
    // The whole image counts as "no crop".
    m_crop = (crop == QRect(QPoint(0, 0), m_image.size())) ? QRect() : crop;
    m_cropReset->setVisible(!m_crop.isEmpty());
    const QSize size = outputSize();
    m_cropLabel->setText(m_crop.isEmpty() ? QStringLiteral("%1×%2").arg(size.width()).arg(size.height())
                                          : QStringLiteral("%1×%2 at %3,%4")
                                                .arg(size.width())
                                                .arg(size.height())
                                                .arg(m_crop.x())
                                                .arg(m_crop.y()));
    updateOverlay(); // captions are laid out on the cropped image
    // So is text in a corner.
    const QList<Annotation> placed = placeAnchored(m_annotations, frame());
    if(placed != m_annotations)
        rebuildBase();
}

void ScreenshotEditor::setTool(int index)
{
    m_activeTool = index;
    for(int i = 0; i < int(m_toolButtons.size()); ++i)
        m_toolButtons[i]->setChecked(i == index);
    const bool active = index >= 0;
    if(active && m_cropEdit->isChecked())
        m_cropEdit->setChecked(false);
    m_view->setDrawing(active, moveToolActive() ? Qt::SizeAllCursor : Qt::CrossCursor);
    if(active)
        m_view->setFocus();
    if(!moveToolActive())
        select(-1);
    updateTextPlace();
    if(m_current || m_dragging)
    {
        m_current.reset();
        m_dragging = false;
        m_dragBase = {};
        refreshView();
    }
}

std::optional<AnnotationTool> ScreenshotEditor::drawingTool() const
{
    return m_activeTool >= 0 ? kTools[m_activeTool].tool : std::nullopt;
}

void ScreenshotEditor::select(int index)
{
    m_selected = index;
    m_view->setHighlight(index >= 0 ? annotationBounds(m_annotations[index]) : QRectF());
    updateTextPlace();
}

const Annotation* ScreenshotEditor::selectedText() const
{
    if(!moveToolActive() || m_selected < 0 || m_annotations[m_selected].tool != AnnotationTool::Text)
        return nullptr;
    return &m_annotations[m_selected];
}

void ScreenshotEditor::updateTextPlace()
{
    const Annotation* text = selectedText();
    m_textPlace->setVisible(text || drawingTool() == AnnotationTool::Text);
    // Selected text shows its own placement; otherwise the one new text gets.
    const QString place = text ? (text->anchor ? toString(*text->anchor) : QString()) : m_settings.textPlace;
    m_textPlace->setCurrentIndex(std::max(0, m_textPlace->findData(place)));
}

void ScreenshotEditor::addText(QPointF pos)
{
    Annotation a;
    a.tool = AnnotationTool::Text;
    a.color = m_drawColor;
    a.width = m_drawWidth->value();
    bool ok = false;
    a.text = QInputDialog::getText(this, "Add text", "Text:", QLineEdit::Normal, {}, &ok).trimmed();
    if(!ok || a.text.isEmpty())
        return;
    // Click = middle of the first line's left edge, which is where people aim.
    a.points = {pos - QPointF(0, annotationFontPixels(a.width) * 0.6)};
    if(!m_settings.textPlace.isEmpty())
        a.anchor = textPositionFromString(m_settings.textPlace); // rebuildBase() puts it there
    changeAnnotations(m_annotations + QList<Annotation>{a});
}

void ScreenshotEditor::editSelected(const std::function<void(Annotation&)>& change)
{
    if(!moveToolActive() || m_selected < 0)
        return;
    QList<Annotation> list = m_annotations;
    change(list[m_selected]);
    if(list != m_annotations)
        changeAnnotations(list);
}

void ScreenshotEditor::deleteSelected()
{
    if(m_selected < 0)
        return;
    QList<Annotation> list = m_annotations;
    list.removeAt(m_selected);
    select(-1);
    changeAnnotations(list);
}

bool ScreenshotEditor::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == m_view && event->type() == QEvent::KeyPress && moveToolActive() && m_selected >= 0 && !m_dragging)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        const qreal step = (key->modifiers() & Qt::ShiftModifier) ? 10 : 1;
        QPointF d;
        switch(key->key())
        {
            case Qt::Key_Delete:
            case Qt::Key_Backspace: deleteSelected(); return true;
            case Qt::Key_Left: d = {-step, 0}; break;
            case Qt::Key_Right: d = {step, 0}; break;
            case Qt::Key_Up: d = {0, -step}; break;
            case Qt::Key_Down: d = {0, step}; break;
            default: break;
        }
        if(!d.isNull())
        {
            editSelected([d](Annotation& a) { a = translated(a, d); });
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ScreenshotEditor::onDrawPressed(QPointF pos)
{
    if(moveToolActive())
    {
        // Grab within a few screen pixels, whatever the zoom.
        const int hit = annotationAt(m_annotations, pos, 6 * m_view->imagePixelsPerWidgetPixel());
        select(hit);
        if(hit < 0)
            return;
        m_dragging = true;
        m_dragStart = pos;
        m_dragOriginal = m_annotations[hit];
        QList<Annotation> others = m_annotations;
        others.removeAt(hit);
        m_dragBase = applyAnnotations(m_image, others);
        return;
    }
    const std::optional<AnnotationTool> tool = drawingTool();
    if(!tool)
        return;
    if(*tool == AnnotationTool::Text)
    {
        addText(pos);
        return;
    }
    Annotation a;
    a.tool = *tool;
    a.color = m_drawColor;
    a.width = m_drawWidth->value();
    const bool freehand = a.tool == AnnotationTool::Pen || a.tool == AnnotationTool::Marker;
    a.points = freehand ? QList<QPointF>{pos} : QList<QPointF>{pos, pos};
    m_current = a;
    refreshView();
}

void ScreenshotEditor::onDrawMoved(QPointF pos)
{
    if(m_dragging)
    {
        // Shown on top while dragging; it drops back into its layer on release.
        const Annotation moved = translated(m_dragOriginal, pos - m_dragStart);
        m_view->setImage(applyAnnotations(m_dragBase, {moved}));
        m_view->setHighlight(annotationBounds(moved));
        return;
    }
    if(!m_current)
        return;
    QList<QPointF>& pts = m_current->points;
    if(m_current->tool == AnnotationTool::Pen || m_current->tool == AnnotationTool::Marker)
    {
        if(QLineF(pts.last(), pos).length() < 1.0)
            return; // sub-pixel jitter just bloats the stroke
        pts << pos;
    }
    else
        pts[1] = pos;
    refreshView();
}

void ScreenshotEditor::onDrawReleased(QPointF pos)
{
    if(m_dragging)
    {
        m_dragging = false;
        m_dragBase = {};
        const QPointF delta = pos - m_dragStart;
        if(QLineF(QPointF(), delta).length() < 0.5)
            return; // just selected
        QList<Annotation> list = m_annotations;
        list[m_selected] = translated(m_dragOriginal, delta);
        changeAnnotations(list);
        return;
    }
    if(!m_current)
        return;
    onDrawMoved(pos);
    Annotation a = *m_current;
    m_current.reset();
    const bool freehand = a.tool == AnnotationTool::Pen || a.tool == AnnotationTool::Marker;
    if(!freehand && QLineF(a.points[0], a.points[1]).length() < 3)
    {
        refreshView(); // a stray click, not a shape
        return;
    }
    changeAnnotations(m_annotations + QList<Annotation>{a});
}

void ScreenshotEditor::changeAnnotations(const QList<Annotation>& annotations)
{
    m_undoStack << m_annotations;
    m_redoStack.clear();
    m_annotations = annotations;
    rebuildBase();
    scheduleEditsSave();
}

void ScreenshotEditor::undo()
{
    if(m_undoStack.isEmpty())
        return;
    m_redoStack << m_annotations;
    m_annotations = m_undoStack.takeLast();
    rebuildBase();
    scheduleEditsSave();
}

void ScreenshotEditor::redo()
{
    if(m_redoStack.isEmpty())
        return;
    m_undoStack << m_annotations;
    m_annotations = m_redoStack.takeLast();
    rebuildBase();
    scheduleEditsSave();
}

void ScreenshotEditor::rebuildBase()
{
    m_annotations = placeAnchored(m_annotations, frame()); // after a crop, width or text change
    m_base = applyAnnotations(m_image, m_annotations);
    refreshView();
    updateDrawButtons();
    select(m_selected < m_annotations.size() ? m_selected : -1); // undo may remove it
}

void ScreenshotEditor::refreshView()
{
    // The shape being drawn goes on a copy of the finished ones, so a drag stays cheap.
    m_view->setImage(m_current ? applyAnnotations(m_base, {*m_current}) : m_base);
}

void ScreenshotEditor::updateDrawButtons()
{
    m_undo->setEnabled(!m_undoStack.isEmpty());
    m_redo->setEnabled(!m_redoStack.isEmpty());
    m_clear->setEnabled(!m_annotations.isEmpty());
}

void ScreenshotEditor::closeEvent(QCloseEvent* e)
{
    if(m_editsTimer.isActive())
    {
        m_editsTimer.stop();
        emit editsChanged(m_entry.id, currentEdits());
    }
    e->accept();
}

} // namespace qr
