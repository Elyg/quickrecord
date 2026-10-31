#include "app/SheetEditor.h"

#include "app/Thumbnailer.h"
#include "app/Clipboard.h"
#include "app/FlowLayout.h"
#include "app/Icons.h"
#include "app/PreviewPane.h"
#include "app/Settings.h"
#include "app/VideoView.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <utility>

namespace qr
{

namespace
{
constexpr int kPreviewCellWidth = 1200; // preview tiles are at most this wide (pixels)
constexpr int kPlayRenderDelayMs = 350; // after the last change, before rendering it to play

QLabel* heading(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

QLabel* hint(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet("color: palette(placeholder-text);");
    l->setWordWrap(true);
    return l;
}
} // namespace

SheetEditor::SheetEditor(const HistoryStore& store, const QStringList& members, Settings& settings)
    : m_store(store)
    , m_settings(settings)
{
    const HistoryEntry* first = members.isEmpty() ? nullptr : m_store.find(members.first());
    m_video = first && first->kind == EntryKind::Recording;
    m_created = QDateTime::currentDateTime();
    const QString stamp = m_created.toString("yyyyMMdd-HHmmss");
    m_id = "qsheet-" + stamp;
    for(int n = 2; m_store.find(m_id) || QFileInfo::exists(filePath(m_id)); ++n)
        m_id = QStringLiteral("qsheet-%1-%2").arg(stamp).arg(n);
    m_state.members = members;
    loadImages();
    int cellHeight = 0;
    for(const QImage& img : std::as_const(m_full))
        cellHeight = std::max(cellHeight, img.height());
    m_state = newSheet(members, cellHeight);
    build();
}

SheetEditor::SheetEditor(const HistoryStore& store, const HistoryEntry& entry, Settings& settings)
    : m_store(store)
    , m_settings(settings)
    , m_id(entry.id)
    , m_created(entry.created)
    , m_master(entry.master)
    , m_isNew(false)
    , m_video(entry.kind == EntryKind::Recording)
    , m_state(entry.sheet.value_or(SheetState{}))
{
    loadImages();
    build();
}

void SheetEditor::loadImages()
{
    for(const QString& id : std::as_const(m_state.members))
    {
        const HistoryEntry* e = m_store.find(id);
        if(!e)
            continue;
        QImage img = QImageReader(sheetImagePath(*e)).read();
        if(!img.isNull())
            m_full.insert(id, img.convertToFormat(QImage::Format_RGB32));
    }
    int cellWidth = 0;
    for(const QImage& img : std::as_const(m_full))
        cellWidth = std::max(cellWidth, img.width());
    m_previewScale = cellWidth > kPreviewCellWidth ? double(kPreviewCellWidth) / cellWidth : 1.0;
    for(auto it = m_full.cbegin(); it != m_full.cend(); ++it)
        m_preview.insert(it.key(), m_previewScale < 1.0 ? it->scaled(it->size() * m_previewScale, Qt::KeepAspectRatio,
                                                                     Qt::SmoothTransformation)
                                                       : *it);
}

void SheetEditor::build()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("QuickRecord — Contact sheet %1").arg(m_id));
    setWindowIcon(icons::app());

    m_view = new VideoView(this);
    m_view->setMinimumSize(420, 260);
    m_view->setDrawing(true, Qt::PointingHandCursor);
    m_view->setToolTip("Click a tile to select it, drag it onto another to move it there.\n"
                       "Wheel: zoom · middle-drag: pan · double-click / 0 / 1: fit ↔ 100%");

    // --- layout ---
    m_columns = new QSpinBox(this);
    m_columns->setRange(0, std::max(1, int(m_state.members.size())));
    m_columns->setSpecialValueText("Auto");
    m_columns->setToolTip("Tiles per row; Auto picks the count whose sheet is closest to 16:10");
    m_columns->setValue(m_state.columns);
    m_gap = new QSpinBox(this);
    m_gap->setRange(0, 400);
    m_gap->setSuffix(" px");
    m_gap->setToolTip("Space between the tiles and around them, in sheet pixels");
    m_gap->setValue(m_state.gap);
    m_backgroundButton = new QToolButton(this);
    m_backgroundButton->setToolTip("Background colour");

    // --- text ---
    auto* scopes = new QButtonGroup(this);
    auto* scopeRow = new QHBoxLayout;
    scopeRow->setSpacing(0);
    const std::array<std::pair<const char*, const char*>, 3> scopeInfo = {{
        {"Sheet", "Header and footer bands of the sheet, never over the images"},
        {"Every tile", "On each tile"},
        {"This tile", "Added to the selected tile only"},
    }};
    for(int i = 0; i < 3; ++i)
    {
        auto* b = new QToolButton(this);
        b->setText(scopeInfo[size_t(i)].first);
        b->setToolTip(scopeInfo[size_t(i)].second);
        b->setCheckable(true);
        b->setChecked(Scope(i) == m_scope);
        scopes->addButton(b, i);
        scopeRow->addWidget(b);
        m_scopeButtons[size_t(i)] = b;
    }
    scopeRow->addStretch(1);
    auto* grid = new QGridLayout;
    grid->setSpacing(4);
    const char* names[] = {"Top left", "Top centre", "Top right", "Bottom left", "Bottom centre", "Bottom right"};
    for(int i = 0; i < 6; ++i)
    {
        auto* f = new QLineEdit(this);
        f->setPlaceholderText(names[i]);
        f->setClearButtonEnabled(true);
        f->setMinimumWidth(90);
        grid->addWidget(f, i / 3, i % 3);
        m_fields[size_t(i)] = f;
    }
    m_scopeHint = hint({}, this);
    auto* placeholders = hint("{n} number · {name} · {date} · {time} · {size}; on the sheet {n} is the number of "
                              "tiles. Anything else is kept as typed.",
                              this);
    placeholders->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_textSize = new QSpinBox(this);
    m_textSize->setRange(6, 400);
    m_textSize->setSuffix(" px");
    m_textSize->setToolTip("Font size in sheet pixels");
    m_textSize->setValue(m_state.style.fontSize);
    m_textColorButton = new QToolButton(this);
    m_textColorButton->setToolTip("Text colour");
    m_textBox = new QCheckBox("Background", this);
    m_textBox->setToolTip("Semi-transparent box behind the text (otherwise a dark outline)");
    m_textBox->setChecked(m_state.style.background);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_close = new QPushButton("Close", this);
    m_close->setToolTip(m_isNew ? "Close without making the sheet (Esc)" : "Close (Esc); the changes stay for next time");
    m_save = new QPushButton("Save && Close", this);
    m_save->setToolTip(m_video ? "Render the sheet to a video in the recordings folder and close (Ctrl+S)"
                               : "Write the sheet PNG to the screenshot folder and close (Ctrl+S)");
    m_copy = new QPushButton("Copy", this);
    m_copy->setToolTip("Save the sheet, copy it to the clipboard and close (Ctrl+C)");
    m_copy->setDefault(true);

    auto* panel = new QWidget(this);
    panel->setFixedWidth(380);
    auto* side = new QVBoxLayout(panel);
    side->setContentsMargins(0, 0, 0, 0);
    side->addWidget(heading("Layout", panel));
    auto* form = new QFormLayout;
    form->addRow("Columns", m_columns);
    form->addRow("Gap", m_gap);
    form->addRow("Background", m_backgroundButton);
    side->addLayout(form);
    side->addSpacing(10);
    side->addWidget(heading("Text", panel));
    side->addLayout(scopeRow);
    side->addLayout(grid);
    side->addWidget(m_scopeHint);
    side->addWidget(placeholders);
    auto* style = new FlowLayout(nullptr, 8, 4);
    style->addWidget(m_textSize);
    style->addWidget(m_textColorButton);
    style->addWidget(m_textBox);
    side->addLayout(style);
    side->addStretch(1);
    side->addWidget(m_status);

    auto* canvas = new QVBoxLayout;
    canvas->addWidget(m_view, 1);
    if(m_video)
    {
        m_player = new QMediaPlayer(this);
        m_player->setVideoSink(m_view->sink());
        m_player->setLoops(QMediaPlayer::Infinite);
        m_play = new QToolButton(this);
        m_play->setToolTip("Play / pause (Space)");
        m_seek = new QSlider(Qt::Horizontal, this);
        m_time = new QLabel(this);
        m_time->setMinimumWidth(170);
        auto* playback = new QHBoxLayout;
        playback->addWidget(m_play);
        playback->addWidget(m_seek, 1);
        playback->addWidget(m_time);
        canvas->addLayout(playback);

        m_playTimer.setSingleShot(true);
        m_playTimer.setInterval(kPlayRenderDelayMs);
        connect(&m_playTimer, &QTimer::timeout, this, &SheetEditor::renderPlayback);
        connect(&m_playRender, &Exporter::finished, this, &SheetEditor::onPlaybackRendered);
        connect(&m_playRender, &Exporter::progress, this, [this](double fraction) {
            m_time->setText(QStringLiteral("Getting it ready… %1%").arg(int(fraction * 100)));
        });
        connect(m_play, &QToolButton::clicked, this, &SheetEditor::togglePlay);
        connect(new QShortcut(QKeySequence(Qt::Key_Space), this), &QShortcut::activated, this, &SheetEditor::togglePlay);
        connect(m_seek, &QSlider::sliderMoved, m_player, &QMediaPlayer::setPosition);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &SheetEditor::updatePlayControls);
        connect(m_player, &QMediaPlayer::durationChanged, this, &SheetEditor::updatePlayControls);
        connect(m_player, &QMediaPlayer::positionChanged, this, &SheetEditor::updatePlayControls);
        // Only once it is really playing (or paused) does a seek stick: while loading it is lost.
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if(status == QMediaPlayer::BufferedMedia && m_resumeAt >= 0 && m_player->isSeekable())
                m_player->setPosition(std::exchange(m_resumeAt, -1));
        });
    }

    auto* body = new QHBoxLayout;
    body->addLayout(canvas, 1);
    body->addWidget(panel);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(m_close);
    buttons->addSpacing(12);
    buttons->addWidget(m_save);
    buttons->addWidget(m_copy);

    auto* root = new QVBoxLayout(this);
    root->addLayout(body, 1);
    root->addLayout(buttons);

    // --- wiring ---
    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(60);
    connect(&m_renderTimer, &QTimer::timeout, this, &SheetEditor::render);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        if(!m_isNew)
            emit sheetChanged(m_id, m_state);
    });

    connect(m_columns, &QSpinBox::valueChanged, this, [this](int v) {
        m_state.columns = v;
        scheduleRender();
    });
    connect(m_gap, &QSpinBox::valueChanged, this, [this](int v) {
        m_state.gap = v;
        scheduleRender();
    });
    connect(m_backgroundButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_state.background, this, "Background colour");
        if(c.isValid())
            setBackground(c);
    });
    connect(scopes, &QButtonGroup::idClicked, this, [this](int id) {
        m_scope = Scope(id);
        loadFields();
    });
    for(int i = 0; i < 6; ++i)
        connect(m_fields[size_t(i)], &QLineEdit::textChanged, this, [this, i](const QString& text) {
            if(m_loading)
                return;
            if(SlotText* t = scopeText())
            {
                t->corners[size_t(i)] = text;
                if(m_scope == Scope::ThisTile && t->isEmpty())
                    m_state.perTile.remove(m_state.members.value(m_selected));
                scheduleRender();
            }
        });
    connect(m_textSize, &QSpinBox::valueChanged, this, [this](int v) {
        m_state.style.fontSize = v;
        scheduleRender();
    });
    connect(m_textColorButton, &QToolButton::clicked, this, [this] {
        const QColor c =
            QColorDialog::getColor(m_state.style.color, this, "Text colour", QColorDialog::ShowAlphaChannel);
        if(c.isValid())
            setTextColor(c);
    });
    connect(m_textBox, &QCheckBox::toggled, this, [this](bool on) {
        m_state.style.background = on;
        scheduleRender();
    });

    connect(m_view, &VideoView::drawPressed, this, [this](QPointF pos) {
        m_dragFrom = m_layout.tileAt(pos.toPoint());
        selectTile(m_dragFrom);
    });
    connect(m_view, &VideoView::drawMoved, this, [this](QPointF pos) {
        if(m_dragFrom < 0)
            return;
        const int over = m_layout.tileAt(pos.toPoint());
        m_view->setHighlight(m_layout.tileRect(over >= 0 ? over : m_dragFrom));
    });
    connect(m_view, &VideoView::drawReleased, this, [this](QPointF pos) {
        const int from = std::exchange(m_dragFrom, -1);
        const int to = m_layout.tileAt(pos.toPoint());
        if(from < 0 || to < 0 || from == to)
        {
            selectTile(from);
            return;
        }
        m_state.members.move(from, to);
        m_selected = to;
        render();
        selectTile(to);
        m_saveTimer.start(); // the new order
    });

    connect(m_close, &QPushButton::clicked, this, &QWidget::close);
    connect(m_save, &QPushButton::clicked, this, [this] { finish(Action::Save); });
    connect(m_copy, &QPushButton::clicked, this, [this] { finish(Action::Copy); });
    connect(new QShortcut(QKeySequence("Ctrl+S"), this), &QShortcut::activated, this, [this] { finish(Action::Save); });
    connect(new QShortcut(QKeySequence("Ctrl+C"), this), &QShortcut::activated, this, [this] { finish(Action::Copy); });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, [this] {
        if(m_selected >= 0)
            selectTile(-1);
        else
            close();
    });

    // --- initial state ---
    m_loading = true;
    setBackground(m_state.background);
    setTextColor(m_state.style.color);
    m_loading = false;
    loadFields();
    render();
    resize(1400, 820);

    const qsizetype missing = std::count_if(m_state.members.cbegin(), m_state.members.cend(),
                                            [this](const QString& id) { return !m_full.contains(id); });
    if(missing > 0)
        m_status->setText(QStringLiteral("%1 of the images %2 gone; %3 drawn as \"missing\".")
                              .arg(missing)
                              .arg(missing == 1 ? "is" : "are")
                              .arg(missing == 1 ? "it is" : "they are"));
}

QList<SheetTile> SheetEditor::tiles(bool preview) const
{
    QList<SheetTile> out;
    for(const QString& id : m_state.members)
    {
        SheetTile t;
        t.name = id;
        t.image = (preview ? m_preview : m_full).value(id);
        if(const HistoryEntry* e = m_store.find(id))
        {
            t.name = entryName(*e);
            t.created = e->created;
            t.size = m_full.contains(id) ? m_full.value(id).size() : e->size;
        }
        out << t;
    }
    return out;
}

void SheetEditor::scheduleRender()
{
    if(m_loading)
        return;
    m_renderTimer.start();
    m_saveTimer.start();
}

void SheetEditor::render()
{
    dropPlayback(); // before the still, which its last frame would cover
    m_view->setImage(renderSheet(scaledSheet(m_state, m_previewScale), tiles(true), m_id, m_created, &m_layout));
    m_view->setHighlight(m_layout.tileRect(m_selected));
}

void SheetEditor::dropPlayback()
{
    if(!m_video)
        return;
    if(!m_playPath.isEmpty())
        m_resumeAt = m_player->position();
    m_playRender.cancel();
    m_player->stop();
    m_player->setSource({});
    if(!m_playPath.isEmpty())
        QFile::remove(std::exchange(m_playPath, {}));
    if(m_wantPlay)
        m_playTimer.start();
    updatePlayControls();
}

void SheetEditor::renderPlayback()
{
    if(!m_playDir)
        m_playDir = std::make_unique<QTemporaryDir>();
    const QString base = m_playDir->filePath(QString::number(++m_playRenders));
    QList<const HistoryEntry*> entries;
    for(const QString& id : std::as_const(m_state.members))
        entries << m_store.find(id);
    SheetVideoParams p = sheetVideoParams(scaledSheet(m_state, m_previewScale), tiles(true), entries, m_id, m_created, base);
    if(!m_playDir->isValid() || p.tiles.isEmpty())
    {
        m_time->setText("Nothing to play");
        return;
    }
    p.preset = "veryfast";
    p.crf = 26;
    p.output = base + ".mp4";
    m_playRender.start(p);
    updatePlayControls();
}

void SheetEditor::onPlaybackRendered(bool ok, const QString& path, const QString& error)
{
    if(!ok)
    {
        m_time->setText("Could not play it");
        m_status->setText("Could not render the preview:\n" + error);
        return;
    }
    m_playPath = path;
    m_player->setSource(QUrl::fromLocalFile(path));
    if(m_wantPlay)
        m_player->play();
    updatePlayControls();
}

void SheetEditor::togglePlay()
{
    if(!m_playPath.isEmpty())
    {
        m_wantPlay = m_player->playbackState() != QMediaPlayer::PlayingState;
        if(m_wantPlay)
            m_player->play();
        else
            m_player->pause();
    }
    else if(m_playRender.isRunning() || m_playTimer.isActive())
    {
        m_wantPlay = false;
        m_playTimer.stop();
        m_playRender.cancel();
    }
    else
    {
        m_wantPlay = true;
        renderPlayback();
    }
    updatePlayControls();
}

void SheetEditor::updatePlayControls()
{
    const bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
    const bool working = m_playRender.isRunning() || m_playTimer.isActive();
    m_play->setText(playing || working ? "❚❚" : "▶");
    m_seek->setEnabled(!m_playPath.isEmpty());
    if(m_playPath.isEmpty())
    {
        m_seek->setRange(0, 0);
        if(!working)
            m_time->clear();
        else if(!m_playRender.isRunning())
            m_time->setText("Getting it ready…");
        return;
    }
    m_seek->setRange(0, int(m_player->duration()));
    if(!m_seek->isSliderDown())
        m_seek->setValue(int(m_player->position()));
    m_time->setText(formatPlayTime(m_player->position()) + " / " + formatPlayTime(m_player->duration()));
}

void SheetEditor::selectTile(int index)
{
    m_selected = index;
    m_view->setHighlight(m_layout.tileRect(index));
    if(m_scope == Scope::ThisTile)
        loadFields();
    else
        m_scopeHint->setText(index >= 0 ? QStringLiteral("Tile %1 selected — \"This tile\" adds text to it only.")
                                              .arg(index + 1)
                                        : QString());
}

SlotText* SheetEditor::scopeText()
{
    switch(m_scope)
    {
        case Scope::Sheet: return &m_state.sheetText;
        case Scope::EveryTile: return &m_state.everyTile;
        case Scope::ThisTile:
            if(m_selected < 0 || m_selected >= m_state.members.size())
                return nullptr;
            return &m_state.perTile[m_state.members[m_selected]];
    }
    return nullptr;
}

void SheetEditor::loadFields()
{
    m_loading = true;
    const SlotText* t = scopeText();
    for(size_t i = 0; i < m_fields.size(); ++i)
    {
        m_fields[i]->setText(t ? t->corners[i] : QString());
        m_fields[i]->setEnabled(t != nullptr);
    }
    if(m_scope == Scope::ThisTile && m_selected >= 0 && t && t->isEmpty())
        m_state.perTile.remove(m_state.members[m_selected]); // only looked at
    m_loading = false;

    switch(m_scope)
    {
        case Scope::Sheet: m_scopeHint->setText("Top row: a header band above the tiles; bottom row: a footer."); break;
        case Scope::EveryTile: m_scopeHint->setText("Drawn on each tile, e.g. {n} to number them."); break;
        case Scope::ThisTile:
            m_scopeHint->setText(m_selected >= 0 ? QStringLiteral("Added to tile %1, below the every-tile text.")
                                                       .arg(m_selected + 1)
                                                 : QStringLiteral("Click a tile on the sheet first."));
            break;
    }
}

void SheetEditor::setBackground(const QColor& color)
{
    m_state.background = color;
    m_backgroundButton->setIcon(icons::swatch(color));
    scheduleRender();
}

void SheetEditor::setTextColor(const QColor& color)
{
    m_state.style.color = color;
    m_textColorButton->setIcon(icons::swatch(color));
    scheduleRender();
}

QString SheetEditor::filePath(const QString& id) const
{
    return m_video ? m_settings.exportDir + "/" + id + ".mp4" : m_settings.screenshotDir + "/" + id + ".png";
}

void SheetEditor::finish(Action action)
{
    if(m_exporter.isRunning())
        return;
    if(m_video)
    {
        m_playTimer.stop();
        m_playRender.cancel(); // the machine has a better use for itself now
        updatePlayControls();
        renderVideo(action);
        return;
    }
    const QImage sheet = renderSheet(m_state, tiles(false), m_id, m_created);
    const QString path = m_master.isEmpty() ? filePath(m_id) : m_master;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly) || !sheet.save(&f, "PNG", 80) || !f.commit())
    {
        m_status->setText("Could not write " + path);
        emit notify("Save failed", path, true);
        return;
    }
    published(path, sheet.size(), 0.0, m_state, action);
}

void SheetEditor::renderVideo(Action action)
{
    auto layers = std::make_shared<QTemporaryDir>();
    const QList<SheetTile> all = tiles(false);
    QList<const HistoryEntry*> entries;
    for(const QString& id : std::as_const(m_state.members))
        entries << m_store.find(id);
    QSize size;
    SheetVideoParams p;
    if(layers->isValid())
        p = sheetVideoParams(m_state, all, entries, m_id, m_created, layers->filePath("sheet"), &size);
    if(p.tiles.isEmpty())
    {
        m_status->setText("There is nothing to render: are any of the recordings left?");
        return;
    }
    p.output = m_master.isEmpty() ? filePath(m_id) : m_master;

    const auto setBusy = [this](bool busy) {
        m_save->setEnabled(!busy);
        m_copy->setEnabled(!busy);
    };
    setBusy(true);
    m_status->setText("Rendering the video…");
    connect(&m_exporter, &Exporter::progress, this, [this](double fraction) {
        m_status->setText(QStringLiteral("Rendering the video… %1%").arg(int(fraction * 100)));
    });
    connect(
        &m_exporter, &Exporter::finished, this,
        [this, setBusy, layers, size, duration = p.duration, state = m_state, action](bool ok, const QString& path, const QString& error) {
            disconnect(&m_exporter, &Exporter::progress, this, nullptr);
            setBusy(false);
            if(ok)
                published(path, size, duration, state, action);
            else
            {
                m_status->setText("Could not render the video:\n" + error);
                emit notify("Save failed", error, true);
            }
        },
        Qt::SingleShotConnection);
    m_exporter.start(p);
}

void SheetEditor::published(const QString& path, QSize size, double duration, const SheetState& state, Action action)
{
    m_master = path;
    m_isNew = false;
    m_saveTimer.stop();

    HistoryEntry e;
    e.kind = m_video ? EntryKind::Recording : EntryKind::Screenshot;
    e.id = m_id;
    e.created = m_created;
    e.master = path;
    e.thumb = Settings::thumbsDir() + "/" + m_id + ".png";
    e.size = size;
    e.duration = duration;
    e.sheet = state;
    emit saved(e);

    if(action == Action::Copy)
    {
        copyFileToClipboard(path);
        emit notify("Copied to clipboard",
                    QStringLiteral("%1 (%2)").arg(QFileInfo(path).fileName(),
                                                  QLocale().formattedDataSize(QFileInfo(path).size(), 1)),
                    false);
    }
    close();
}

void SheetEditor::closeEvent(QCloseEvent* e)
{
    m_exporter.cancel();
    m_playTimer.stop();
    m_playRender.cancel();
    if(m_player)
        m_player->stop();
    if(m_saveTimer.isActive())
    {
        m_saveTimer.stop();
        if(!m_isNew)
            emit sheetChanged(m_id, m_state);
    }
    e->accept();
}

} // namespace qr
