#include "app/HistoryWindow.h"

#include "app/Clipboard.h"
#include "app/Icons.h"
#include "app/PreviewPane.h"
#include "app/Settings.h"
#include "app/Thumbnailer.h"
#include "core/HistoryStore.h"

#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMovie>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QWheelEvent>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace qr
{

namespace
{
constexpr int kMinTileWidth = 120;
constexpr int kMaxTileWidth = 480;
constexpr int kTileStep = 20;
constexpr int kIdRole = Qt::UserRole + 1;   // entry id (all tabs; source id on exports)
constexpr int kPathRole = Qt::UserRole + 2; // export file path

// Small format label in the tile's top-left corner.
QPixmap withBadge(QPixmap tile, const QString& text)
{
    QPainter p(&tile);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(11);
    p.setFont(f);
    const QFontMetrics fm = p.fontMetrics();
    const QRect box(8, 8, fm.horizontalAdvance(text) + 10, fm.height() + 4);
    p.setPen(Qt::NoPen);
    p.setBrush(text == "GIF" ? QColor("#e5484d") : (text == "PNG" ? QColor("#30a46c") : QColor("#3e63dd")));
    p.drawRoundedRect(box, 4, 4);
    p.setPen(Qt::white);
    p.drawText(box, Qt::AlignCenter, text);
    return tile;
}

// Draws thumbnails untinted when selected; the selection background marks the tile.
class TileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if(!(option->state & QStyle::State_Selected) || option->icon.isNull())
            return;
        const qreal dpr = option->widget ? option->widget->devicePixelRatioF() : 1.0;
        const QPixmap pm = option->icon.pixmap(option->decorationSize, dpr);
        QIcon icon(pm);
        icon.addPixmap(pm, QIcon::Selected);
        option->icon = icon;
    }
};

void openFolder(const QString& dir)
{
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool isGif(const QString& path)
{
    return path.endsWith(".gif", Qt::CaseInsensitive);
}

bool isPng(const QString& path)
{
    return path.endsWith(".png", Qt::CaseInsensitive);
}

// Newest file among `paths` that still exists, or empty.
QString lastExisting(const QStringList& paths)
{
    for(auto it = paths.rbegin(); it != paths.rend(); ++it)
        if(QFileInfo::exists(*it))
            return *it;
    return {};
}
} // namespace

HistoryWindow::HistoryWindow(const HistoryStore& store, Settings& settings)
    : m_store(store)
    , m_settings(settings)
{
    const int width = std::clamp(settings.historyTileWidth, kMinTileWidth, kMaxTileWidth);
    m_tile = QSize(width, width * 9 / 16);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("QuickRecord — History");
    setWindowIcon(icons::app());
    resize(1500, 760);

    m_list = makeGrid();
    m_shots = makeGrid();
    m_exports = makeGrid();
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_list, "Recordings");
    m_tabs->addTab(m_shots, "Screenshots");
    m_tabs->addTab(m_exports, "Exports");

    m_preview = new PreviewPane(this);
    m_count = new QLabel(this);
    m_openExports = new QPushButton(this);
    m_openMasters = new QPushButton(this);

    m_tileSlider = new QSlider(Qt::Horizontal, this);
    m_tileSlider->setRange(kMinTileWidth, kMaxTileWidth);
    m_tileSlider->setSingleStep(kTileStep);
    m_tileSlider->setPageStep(kTileStep * 3);
    m_tileSlider->setValue(width);
    m_tileSlider->setFixedWidth(160);
    m_tileSlider->setToolTip("Thumbnail size (Ctrl+wheel over the grid)");
    auto* sizeLabel = new QLabel("Thumbnails", this);
    sizeLabel->setStyleSheet("color: palette(placeholder-text);");
    m_tileTimer.setSingleShot(true);
    m_tileTimer.setInterval(120);
    connect(m_tileSlider, &QSlider::valueChanged, &m_tileTimer, qOverload<>(&QTimer::start));
    connect(&m_tileTimer, &QTimer::timeout, this, &HistoryWindow::applyTileSize);

    auto* top = new QHBoxLayout;
    top->addWidget(m_count);
    top->addStretch(1);
    top->addWidget(sizeLabel);
    top->addWidget(m_tileSlider);
    top->addSpacing(16);
    top->addWidget(m_openExports);
    top->addWidget(m_openMasters);

    auto* root = new QVBoxLayout(this);
    root->addLayout(top);
    m_split = new QSplitter(Qt::Horizontal, this);
    m_split->addWidget(m_tabs);
    m_split->addWidget(m_preview);
    m_split->setStretchFactor(0, 3);
    m_split->setStretchFactor(1, 2);
    m_split->setChildrenCollapsible(false);
    root->addWidget(m_split, 1);
    connect(m_preview, &PreviewPane::detachToggled, this, &HistoryWindow::togglePreviewDetached);

    connect(m_openExports, &QPushButton::clicked, this, [this] {
        openFolder(m_tabs->currentWidget() == m_shots ? m_settings.screenshotDir : m_settings.exportDir);
    });
    connect(m_openMasters, &QPushButton::clicked, this, [this] {
        openFolder(m_tabs->currentWidget() == m_shots ? Settings::shotsDir() : Settings::mastersDir());
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        setRecordingHover(nullptr);
        setExportHover(nullptr);
        updateFolderButtons();
        updateCount();
        updatePreview();
    });
    updateFolderButtons();
    for(QListWidget* list : {m_list, m_shots, m_exports})
        connect(list, &QListWidget::currentItemChanged, this, &HistoryWindow::updatePreview);

    connect(m_list, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { emit openRequested(item->data(kIdRole).toString()); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &HistoryWindow::showRecordingMenu);
    connect(m_list, &QListWidget::itemEntered, this, &HistoryWindow::setRecordingHover);

    connect(m_shots, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* item) { emit openRequested(item->data(kIdRole).toString()); });
    connect(m_shots, &QListWidget::customContextMenuRequested, this, &HistoryWindow::showShotMenu);

    connect(m_exports, &QListWidget::itemActivated, this, [](QListWidgetItem* item) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->data(kPathRole).toString()));
    });
    connect(m_exports, &QListWidget::customContextMenuRequested, this, &HistoryWindow::showExportMenu);
    connect(m_exports, &QListWidget::itemEntered, this, &HistoryWindow::setExportHover);

    m_anim.setInterval(220);
    connect(&m_anim, &QTimer::timeout, this, [this] {
        if(!m_hover)
            return;
        const auto* e = m_store.find(m_hover->data(kIdRole).toString());
        if(!e)
            return;
        const auto& frames = framesFor(e->id, e->thumb);
        if(frames.size() < 2)
            return;
        m_hoverFrame = (m_hoverFrame + 1) % int(frames.size());
        m_hover->setIcon(QIcon(frames[m_hoverFrame]));
    });

    refresh();
}

HistoryWindow::~HistoryWindow()
{
    // A popped-out preview is a separate top-level window, not our child.
    if(m_preview && m_preview->parentWidget() == nullptr)
        delete m_preview;
}

void HistoryWindow::togglePreviewDetached()
{
    if(m_preview->parentWidget() == m_split)
    {
        const QSize size = m_preview->size().expandedTo(QSize(900, 650));
        m_preview->setParent(nullptr, Qt::Window);
        m_preview->setWindowIcon(windowIcon());
        m_preview->resize(size);
        m_preview->setDetached(true);
        m_preview->show();
        m_preview->raise();
        m_preview->activateWindow();
    }
    else
    {
        m_split->insertWidget(1, m_preview);
        m_preview->setDetached(false);
        m_preview->show();
    }
}

QListWidget* HistoryWindow::makeGrid()
{
    auto* list = new QListWidget(this);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(m_tile);
    // Room for three lines of text under the thumbnail.
    list->setGridSize(QSize(m_tile.width() + 24, m_tile.height() + 3 * list->fontMetrics().lineSpacing() + 20));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setUniformItemSizes(false); // tiles have two or three lines of text
    list->setWordWrap(true);
    list->setMouseTracking(true);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setItemDelegate(new TileDelegate(list));
    list->viewport()->installEventFilter(this);
    return list;
}

void HistoryWindow::applyTileSize()
{
    const int width = m_tileSlider->value();
    if(width == m_tile.width())
        return;
    m_tile = QSize(width, width * 9 / 16);
    for(QListWidget* list : {m_list, m_shots, m_exports})
    {
        list->setIconSize(m_tile);
        list->setGridSize(QSize(m_tile.width() + 24, m_tile.height() + 3 * list->fontMetrics().lineSpacing() + 20));
    }
    m_settings.historyTileWidth = width;
    m_settings.save();
    refresh(); // every thumbnail is rendered at the tile size
}

QPixmap HistoryWindow::fitTile(const QPixmap& src) const
{
    // Rendered at the screen's pixel density so thumbnails stay sharp on scaled displays.
    const qreal dpr = qApp->devicePixelRatio();
    QPixmap tile(m_tile * dpr);
    tile.setDevicePixelRatio(dpr);
    tile.fill(Qt::transparent); // letterboxing blends into the list background
    QPainter p(&tile);
    if(!src.isNull())
    {
        QPixmap scaled = src.scaled(m_tile * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        const QSizeF size = scaled.deviceIndependentSize();
        p.drawPixmap(QPointF((m_tile.width() - size.width()) / 2, (m_tile.height() - size.height()) / 2), scaled);
    }
    else
    {
        p.setPen(QColor("#6b707a"));
        p.drawText(QRect(QPoint(0, 0), m_tile), Qt::AlignCenter, "no preview");
    }
    return tile;
}

QPixmap HistoryWindow::readScaled(const QString& path) const
{
    QImageReader reader(path);
    reader.setScaledSize(reader.size().scaled(m_tile * qApp->devicePixelRatio(), Qt::KeepAspectRatio));
    return QPixmap::fromImage(reader.read());
}

const QList<QPixmap>& HistoryWindow::framesFor(const QString& id, const QString& thumbPath)
{
    auto it = m_frames.find(id);
    if(it != m_frames.end() && !it->isEmpty())
        return *it;
    QList<QPixmap> frames;
    const QPixmap sheet(thumbPath);
    if(!sheet.isNull())
    {
        const int cell = sheet.width() / kSpriteFrames;
        for(int i = 0; i < kSpriteFrames; ++i)
        {
            const QPixmap f = sheet.copy(i * cell, 0, cell, sheet.height());
            // tile pads missing frames with black; stop at the first fully black cell
            if(i > 0 && f.toImage().scaled(4, 4).pixelColor(1, 1).value() < 3)
                break;
            frames << fitTile(f);
        }
    }
    if(frames.isEmpty())
        frames << fitTile({});
    return *m_frames.insert(id, frames);
}

void HistoryWindow::refresh()
{
    m_frames.clear(); // thumbnails may have been regenerated
    m_shotTiles.clear();
    m_exportTiles.clear();
    {
        // Rebuilding clears the selection; keep that from restarting the preview.
        const QSignalBlocker blockRecordings(m_list);
        const QSignalBlocker blockShots(m_shots);
        const QSignalBlocker blockExports(m_exports);
        refreshRecordings();
        refreshShots();
        refreshExports();
    }
    updateCount();
    updatePreview();
}

void HistoryWindow::refreshRecordings()
{
    setRecordingHover(nullptr);
    const QString selected = m_list->currentItem() ? m_list->currentItem()->data(kIdRole).toString() : QString();
    m_list->clear();

    const QLocale loc;
    for(const HistoryEntry& e : m_store.entries())
    {
        if(e.kind != EntryKind::Recording)
            continue;
        const QFileInfo master(e.master);
        const bool hasMaster = master.exists();
        QString text = loc.toString(e.created, "MMM d, HH:mm") +
                       QStringLiteral("\n%1 s · %2×%3").arg(e.duration, 0, 'f', 1).arg(e.size.width()).arg(e.size.height());
        if(hasMaster)
            text += " · " + loc.formattedDataSize(master.size(), 1);
        QStringList extras;
        if(!e.exports.isEmpty())
            extras << QStringLiteral("%1 export%2").arg(e.exports.size()).arg(e.exports.size() > 1 ? "s" : "");
        if(e.edits)
            extras << "edited";
        if(!hasMaster)
            extras << "(master missing)";
        if(!extras.isEmpty())
            text += "\n" + extras.join(" · ");

        auto* item = new QListWidgetItem(QIcon(framesFor(e.id, e.thumb).first()), text, m_list);
        item->setData(kIdRole, e.id);
        item->setToolTip(QStringLiteral("%1\nDouble-click to open in the editor").arg(e.master));
        if(!hasMaster)
            item->setForeground(QColor("#8a8f98"));
        if(e.id == selected)
            m_list->setCurrentItem(item);
    }
}

QPixmap HistoryWindow::shotTile(const QString& id, const QString& thumbPath, const QString& original)
{
    if(auto it = m_shotTiles.constFind(id); it != m_shotTiles.constEnd())
        return *it;
    QPixmap thumb(thumbPath);
    if(thumb.isNull())
        thumb = readScaled(original);
    return *m_shotTiles.insert(id, fitTile(thumb));
}

void HistoryWindow::refreshShots()
{
    const QString selected = m_shots->currentItem() ? m_shots->currentItem()->data(kIdRole).toString() : QString();
    m_shots->clear();

    const QLocale loc;
    for(const HistoryEntry& e : m_store.entries())
    {
        if(e.kind != EntryKind::Screenshot)
            continue;
        const QFileInfo original(e.master);
        const bool hasOriginal = original.exists();
        QString text = loc.toString(e.created, "MMM d, HH:mm") +
                       QStringLiteral("\n%1×%2").arg(e.size.width()).arg(e.size.height());
        if(hasOriginal)
            text += " · " + loc.formattedDataSize(original.size(), 1);
        QStringList extras;
        if(!e.exports.isEmpty())
            extras << QStringLiteral("%1 saved").arg(e.exports.size());
        if(e.edits)
            extras << "edited";
        if(!hasOriginal)
            extras << "(original missing)";
        if(!extras.isEmpty())
            text += "\n" + extras.join(" · ");

        auto* item = new QListWidgetItem(QIcon(shotTile(e.id, e.thumb, e.master)), text, m_shots);
        item->setData(kIdRole, e.id);
        item->setToolTip(QStringLiteral("%1\nDouble-click to open in the editor").arg(e.master));
        if(!hasOriginal)
            item->setForeground(QColor("#8a8f98"));
        if(e.id == selected)
            m_shots->setCurrentItem(item);
    }
}

QList<HistoryWindow::ExportFile> HistoryWindow::collectExports() const
{
    QList<ExportFile> out;
    QSet<QString> seen;
    auto add = [&](const QString& path, const QString& sourceId) {
        const QFileInfo fi(path);
        if(!fi.isFile() || seen.contains(fi.absoluteFilePath()))
            return;
        seen.insert(fi.absoluteFilePath());
        out << ExportFile{fi.absoluteFilePath(), sourceId, fi.lastModified(), fi.size()};
    };

    for(const HistoryEntry& e : m_store.entries())
        for(const QString& path : e.exports)
            add(path, e.id);

    // Also list our files in the export folders that nothing references any more
    // (e.g. the recording was deleted but its exports were kept).
    const auto scan = [&](const QString& dir, const QStringList& patterns) {
        for(const QFileInfo& fi : QDir(dir).entryInfoList(patterns, QDir::Files))
        {
            // "qr-20261009-153012-2.gif" → "qr-20261009-153012"
            const QString id = fi.completeBaseName().left(QStringLiteral("qr-yyyyMMdd-HHmmss").size());
            add(fi.absoluteFilePath(), m_store.find(id) ? id : QString());
        }
    };
    scan(m_settings.exportDir, {"qr-*.gif", "qr-*.mp4"});
    scan(m_settings.screenshotDir, {"qs-*.png"});

    std::sort(out.begin(), out.end(), [](const ExportFile& a, const ExportFile& b) { return a.modified > b.modified; });
    return out;
}

QPixmap HistoryWindow::exportTile(const ExportFile& f)
{
    if(auto it = m_exportTiles.constFind(f.path); it != m_exportTiles.constEnd())
        return *it;
    QPixmap tile;
    if(isGif(f.path) || isPng(f.path))
        tile = withBadge(fitTile(readScaled(f.path)), isGif(f.path) ? "GIF" : "PNG");
    else
    {
        const HistoryEntry* e = m_store.find(f.sourceId);
        tile = withBadge(e ? framesFor(e->id, e->thumb).first() : fitTile({}), "MP4");
    }
    m_exportTiles.insert(f.path, tile);
    return tile;
}

void HistoryWindow::refreshExports()
{
    setExportHover(nullptr);
    const QString selected =
        m_exports->currentItem() ? m_exports->currentItem()->data(kPathRole).toString() : QString();
    m_exports->clear();

    const QLocale loc;
    for(const ExportFile& f : collectExports())
    {
        const QString text = QFileInfo(f.path).fileName() + "\n" + loc.formattedDataSize(f.size, 1) + " · " +
                             loc.toString(f.modified, "MMM d, HH:mm");
        auto* item = new QListWidgetItem(QIcon(exportTile(f)), text, m_exports);
        item->setData(kPathRole, f.path);
        item->setData(kIdRole, f.sourceId);
        item->setToolTip(QStringLiteral("%1\nDouble-click to open").arg(f.path));
        if(f.path == selected)
            m_exports->setCurrentItem(item);
    }
}

void HistoryWindow::updateFolderButtons()
{
    const bool shots = m_tabs->currentWidget() == m_shots;
    m_openExports->setText(shots ? "Open screenshot folder" : "Open export folder");
    m_openMasters->setText(shots ? "Open originals folder" : "Open recordings folder");
}

void HistoryWindow::updateCount()
{
    auto* list = static_cast<QListWidget*>(m_tabs->currentWidget());
    const int n = list->count();
    const QString noun = list == m_exports ? "export" : (list == m_shots ? "screenshot" : "recording");
    m_count->setText(QStringLiteral("%1 %2%3").arg(n).arg(noun).arg(n == 1 ? "" : "s"));
}

void HistoryWindow::updatePreview()
{
    auto* list = static_cast<QListWidget*>(m_tabs->currentWidget());
    const bool exports = list == m_exports;
    QListWidgetItem* item = list->currentItem();
    if(!item)
    {
        m_preview->clear(exports ? "Select an export to preview it."
                                 : (list == m_shots ? "Select a screenshot to preview it."
                                                    : "Select a recording to preview it."));
        return;
    }
    if(exports)
    {
        const QString path = item->data(kPathRole).toString();
        m_preview->play(path, QFileInfo(path).fileName(), item->text().section('\n', 1, -1) + "\n" + path);
        return;
    }
    const HistoryEntry* e = m_store.find(item->data(kIdRole).toString());
    if(!e)
        return;
    m_preview->play(e->master, QLocale().toString(e->created, "MMM d yyyy, HH:mm:ss"),
                    item->text().section('\n', 1, -1) + "\n" + e->master);
}

void HistoryWindow::setRecordingHover(QListWidgetItem* item)
{
    if(m_hover == item)
        return;
    if(m_hover)
    {
        if(const auto* e = m_store.find(m_hover->data(kIdRole).toString()))
            m_hover->setIcon(QIcon(framesFor(e->id, e->thumb).first()));
    }
    m_hover = item;
    m_hoverFrame = 0;
    if(m_hover)
        m_anim.start();
    else
        m_anim.stop();
}

void HistoryWindow::setExportHover(QListWidgetItem* item)
{
    if(m_exportHover == item)
        return;
    if(m_movie)
    {
        m_movie->stop();
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    if(m_exportHover)
    {
        if(auto it = m_exportTiles.constFind(m_exportHover->data(kPathRole).toString()); it != m_exportTiles.constEnd())
            m_exportHover->setIcon(QIcon(*it));
    }
    m_exportHover = item;
    if(!item || !isGif(item->data(kPathRole).toString()))
        return;

    m_movie = new QMovie(item->data(kPathRole).toString(), {}, this);
    connect(m_movie, &QMovie::frameChanged, this, [this, item] {
        if(m_exportHover == item && m_movie)
            item->setIcon(QIcon(withBadge(fitTile(m_movie->currentPixmap()), "GIF")));
    });
    m_movie->start();
}

bool HistoryWindow::eventFilter(QObject* watched, QEvent* event)
{
    if(event->type() == QEvent::Wheel)
    {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if(wheel->modifiers() & Qt::ControlModifier)
        {
            const int notches = wheel->angleDelta().y() / 120;
            if(notches != 0)
                m_tileSlider->setValue(m_tileSlider->value() + notches * kTileStep);
            return true;
        }
    }
    if(event->type() == QEvent::Leave)
    {
        if(watched == m_list->viewport())
            setRecordingHover(nullptr);
        else if(watched == m_exports->viewport())
            setExportHover(nullptr);
    }
    return QWidget::eventFilter(watched, event);
}

void HistoryWindow::showRecordingMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_list->itemAt(pos);
    if(!item)
        return;
    const QString id = item->data(kIdRole).toString();
    const HistoryEntry* e = m_store.find(id);
    if(!e)
        return;
    const bool hasMaster = QFileInfo::exists(e->master);
    const QString lastExport = lastExisting(e->exports);

    QMenu menu(this);
    menu.addAction("Open in editor", this, [this, id] { emit openRequested(id); })->setEnabled(hasMaster);
    menu.addSeparator();
    QAction* copyExport = menu.addAction(
        lastExport.isEmpty() ? QStringLiteral("Copy last export") : "Copy " + QFileInfo(lastExport).fileName(), this,
        [this, lastExport] {
            copyFileToClipboard(lastExport);
            emit notify("Copied to clipboard", QFileInfo(lastExport).fileName(), false);
        });
    copyExport->setEnabled(!lastExport.isEmpty());
    menu.addAction("Copy master MP4", this,
                   [this, master = e->master] {
                       copyFileToClipboard(master);
                       emit notify("Copied to clipboard", QFileInfo(master).fileName(), false);
                   })
        ->setEnabled(hasMaster);
    menu.addSeparator();
    if(!lastExport.isEmpty())
        menu.addAction("Show export in folder", this, [lastExport] { openFolder(QFileInfo(lastExport).absolutePath()); });
    menu.addAction("Show master in folder", this, [] { openFolder(Settings::mastersDir()); });
    menu.addSeparator();
    menu.addAction("Delete…", this, [this, id] { emit deleteRequested(id); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void HistoryWindow::showShotMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_shots->itemAt(pos);
    if(!item)
        return;
    const QString id = item->data(kIdRole).toString();
    const HistoryEntry* e = m_store.find(id);
    if(!e)
        return;
    const bool hasOriginal = QFileInfo::exists(e->master);
    const QString lastSaved = lastExisting(e->exports);
    auto copy = [this](const QString& path) {
        copyFileToClipboard(path);
        emit notify("Copied to clipboard", QFileInfo(path).fileName(), false);
    };

    QMenu menu(this);
    menu.addAction("Open in editor", this, [this, id] { emit openRequested(id); })->setEnabled(hasOriginal);
    menu.addSeparator();
    menu.addAction(lastSaved.isEmpty() ? QStringLiteral("Copy last saved") : "Copy " + QFileInfo(lastSaved).fileName(),
                   this, [copy, lastSaved] { copy(lastSaved); })
        ->setEnabled(!lastSaved.isEmpty());
    menu.addAction("Copy original", this, [copy, original = e->master] { copy(original); })->setEnabled(hasOriginal);
    menu.addSeparator();
    if(!lastSaved.isEmpty())
        menu.addAction("Show saved in folder", this, [lastSaved] { openFolder(QFileInfo(lastSaved).absolutePath()); });
    menu.addAction("Show original in folder", this, [] { openFolder(Settings::shotsDir()); });
    menu.addSeparator();
    menu.addAction("Delete…", this, [this, id] { emit deleteRequested(id); });
    menu.exec(m_shots->viewport()->mapToGlobal(pos));
}

void HistoryWindow::showExportMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_exports->itemAt(pos);
    if(!item)
        return;
    const QString path = item->data(kPathRole).toString();
    const QString sourceId = item->data(kIdRole).toString();
    const HistoryEntry* source = m_store.find(sourceId);

    QMenu menu(this);
    menu.addAction("Copy", this, [this, path] {
        copyFileToClipboard(path);
        emit notify("Copied to clipboard", QFileInfo(path).fileName(), false);
    });
    menu.addAction("Open", this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    menu.addAction("Show in folder", this, [path] { openFolder(QFileInfo(path).absolutePath()); });
    menu.addSeparator();
    menu.addAction("Open source in editor", this, [this, sourceId] { emit openRequested(sourceId); })
        ->setEnabled(source && QFileInfo::exists(source->master));
    menu.addSeparator();
    menu.addAction("Delete file…", this, [this, path] { emit deleteExportRequested(path); });
    menu.exec(m_exports->viewport()->mapToGlobal(pos));
}

} // namespace qr
