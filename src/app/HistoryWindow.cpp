#include "app/HistoryWindow.h"

#include "app/Clipboard.h"
#include "app/FlowLayout.h"
#include "app/Icons.h"
#include "app/PreviewPane.h"
#include "app/Settings.h"
#include "app/Thumbnailer.h"
#include "core/HistoryStore.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDir>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace qr
{

namespace
{
constexpr int kIdRole = Qt::UserRole + 1; // entry id
constexpr int kSheetPreviewWidth = 640;   // tile width of the multi-selection sheet preview
constexpr int kMaxSheetVideoTiles = 12;   // beyond this, the sheet preview stays a still

QColor badgeColor(const QString& text)
{
    if(text == "GIF")
        return QColor("#e5484d");
    if(text == "MP4")
        return QColor("#3e63dd");
    if(text == "SHEET")
        return QColor("#8e4ec6");
    return QColor("#30a46c"); // EDITED
}

// Small labels in the tile's top-left corner, left to right.
QPixmap withBadges(QPixmap tile, const QStringList& badges)
{
    if(badges.isEmpty())
        return tile;
    QPainter p(&tile);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(11);
    p.setFont(f);
    const QFontMetrics fm = p.fontMetrics();
    int x = 8;
    for(const QString& text : badges)
    {
        const QRect box(x, 8, fm.horizontalAdvance(text) + 10, fm.height() + 4);
        p.setPen(Qt::NoPen);
        p.setBrush(badgeColor(text));
        p.drawRoundedRect(box, 4, 4);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, text);
        x = box.right() + 5;
    }
    return tile;
}

// Lines of text under a thumbnail: never wrapped, each one shortened to fit on its own.
constexpr int kTileLines = 3;
constexpr int kTilePadding = 4;

// Draws thumbnails untinted when selected; the selection background marks the tile. Under the
// thumbnail, the first line of the item's text is its title, the others dimmed details.
class TileDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        // From the item: opt.text has its line breaks replaced already.
        const QStringList lines = index.data(Qt::DisplayRole).toString().split('\n');
        opt.text.clear();
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, p, opt.widget);

        const bool selected = opt.state & QStyle::State_Selected;
        QRect icon(QPoint(), opt.decorationSize);
        icon.moveTop(opt.rect.top() + kTilePadding);
        icon.moveLeft(opt.rect.center().x() - icon.width() / 2);
        opt.icon.paint(p, icon, Qt::AlignCenter, selected ? QIcon::Selected : QIcon::Normal);

        QColor title = opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);
        QColor detail = title;
        detail.setAlphaF(0.65);

        p->save();
        int y = icon.bottom() + 1 + kTilePadding;
        const int width = opt.rect.width() - 2 * kTilePadding;
        for(int i = 0; i < std::min(int(lines.size()), kTileLines); ++i)
        {
            QFont font = opt.font;
            if(i == 0)
                font.setWeight(QFont::Medium);
            p->setFont(font);
            p->setPen(i == 0 ? title : detail);
            const QFontMetrics fm(font);
            const QRect line(opt.rect.left() + kTilePadding, y, width, fm.lineSpacing());
            p->drawText(line, Qt::AlignCenter | Qt::TextSingleLine,
                        fm.elidedText(lines[i], i == 0 ? Qt::ElideMiddle : Qt::ElideRight, width));
            y += fm.lineSpacing();
        }
        p->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return QSize(option.decorationSize.width() + 2 * kTilePadding,
                     option.decorationSize.height() + kTileLines * option.fontMetrics.lineSpacing() + 3 * kTilePadding);
    }

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

// What Ctrl+C copies: the newest edited file, else the source.
QString copyPath(const HistoryEntry& e)
{
    const QString edited = latestEdited(e);
    return edited.isEmpty() ? e.master : edited;
}

QString sheetPreviewDir()
{
    return Settings::cacheDir() + "/sheet-preview";
}

// The videos among what is being dragged in.
QStringList draggedVideos(const QMimeData* mime)
{
    QStringList paths;
    for(const QUrl& url : mime->urls())
        if(url.isLocalFile() && isVideoFile(url.toLocalFile()))
            paths << url.toLocalFile();
    return paths;
}

// Tiles drag out as the files Ctrl+C copies, into the file manager or any app that takes files.
class Grid : public QListWidget
{
public:
    Grid(const HistoryStore& store, QWidget* parent)
        : QListWidget(parent)
        , m_store(store)
    {
    }

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override
    {
        QList<QUrl> urls;
        for(const QListWidgetItem* item : items)
            if(const HistoryEntry* e = m_store.find(item->data(kIdRole).toString()); e && QFileInfo::exists(copyPath(*e)))
                urls << QUrl::fromLocalFile(copyPath(*e));
        auto* mime = new QMimeData;
        mime->setUrls(urls);
        return mime;
    }

    void startDrag(Qt::DropActions) override
    {
        QListWidget::startDrag(Qt::CopyAction); // a move would take the tile off the grid
    }

private:
    const HistoryStore& m_store;
};
} // namespace

HistoryWindow::HistoryWindow(const HistoryStore& store, Settings& settings)
    : m_store(store)
    , m_settings(settings)
{
    m_tileWidth = std::clamp(settings.historyTileWidth, kMinHistoryTileWidth, kMaxHistoryTileWidth);
    m_tile = QSize(m_tileWidth, m_tileWidth * 9 / 16);
    setAttribute(Qt::WA_DeleteOnClose);
    setAcceptDrops(true);
    setWindowTitle("QuickRecord — History");
    setWindowIcon(icons::app());
    resize(1500, 760);

    m_list = makeGrid();
    m_shots = makeGrid();
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_list, "Recordings");
    m_tabs->addTab(m_shots, "Screenshots");

    m_preview = new PreviewPane(this);
    m_count = new QLabel(this);
    m_openFolder = new QPushButton("Open folder", this);
    m_makeSheet = new QPushButton("Make sheet", this);
    m_makeSheet->setToolTip("Put the selected items on one contact sheet");
    m_makeSheet->hide();

    m_tileTimer.setSingleShot(true);
    m_tileTimer.setInterval(120);
    connect(&m_tileTimer, &QTimer::timeout, this, &HistoryWindow::applyTileSize);

    // Wraps when the window is narrow.
    auto* top = new FlowLayout(nullptr, 16, 6);
    top->addWidget(m_count);
    top->addLayout(group({m_openFolder, m_makeSheet}, 6));

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
    connect(m_preview, &PreviewPane::variantChosen, this, &HistoryWindow::showVariant);

    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        openFolder(m_tabs->currentWidget() == m_shots ? m_settings.screenshotDir : m_settings.exportDir);
    });
    connect(m_makeSheet, &QPushButton::clicked, this, &HistoryWindow::requestSheet);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        setRecordingHover(nullptr);
        updateHeader();
        updatePreview();
    });

    for(QListWidget* list : {m_list, m_shots})
    {
        connect(list, &QListWidget::itemSelectionChanged, this, [this] {
            updateHeader();
            updatePreview();
        });
        connect(list, &QListWidget::itemActivated, this,
                [this](QListWidgetItem* item) { emit openRequested(item->data(kIdRole).toString()); });
        connect(list, &QListWidget::customContextMenuRequested, this,
                [this, list](const QPoint& pos) { showMenu(list, pos); });
        auto shortcut = [list](const QKeySequence& key) {
            auto* s = new QShortcut(key, list);
            s->setContext(Qt::WidgetWithChildrenShortcut);
            return s;
        };
        connect(shortcut(QKeySequence::Copy), &QShortcut::activated, this, &HistoryWindow::copySelection);
        connect(shortcut(QKeySequence::Delete), &QShortcut::activated, this, &HistoryWindow::requestDelete);
        connect(shortcut(QKeySequence(Qt::Key_Backspace)), &QShortcut::activated, this, &HistoryWindow::requestDelete);
        connect(shortcut(QKeySequence(Qt::Key_F2)), &QShortcut::activated, this, &HistoryWindow::requestRename);
    }
    connect(m_list, &QListWidget::itemEntered, this, &HistoryWindow::setRecordingHover);

    m_anim.setInterval(220);
    connect(&m_anim, &QTimer::timeout, this, [this] {
        if(!m_hover)
            return;
        const auto* e = m_store.find(m_hover->data(kIdRole).toString());
        if(!e)
            return;
        const auto& frames = framesFor(*e);
        if(frames.size() < 2)
            return;
        m_hoverFrame = (m_hoverFrame + 1) % int(frames.size());
        m_hover->setIcon(QIcon(frames[m_hoverFrame]));
    });

    m_sheetTimer.setSingleShot(true);
    m_sheetTimer.setInterval(150);
    connect(&m_sheetTimer, &QTimer::timeout, this, &HistoryWindow::showSheetPreview);
    connect(&m_sheetVideo, &Exporter::finished, this, [this](bool ok, const QString& path, const QString& error) {
        if(!ok)
            spdlog::warn("sheet preview failed: {}", error.toStdString());
        else if(path == m_sheetVideoPath)
            m_preview->play(path, QStringLiteral("Contact sheet of %1 items").arg(m_sheetPreviewOf.size()),
                            "A quick preview. Make sheet to arrange, label and save it.");
    });

    refresh();
}

HistoryWindow::~HistoryWindow()
{
    // A popped-out preview is a separate top-level window, not our child.
    if(m_preview && m_preview->parentWidget() == nullptr)
        delete m_preview;
    m_sheetVideo.cancel();
    QDir(sheetPreviewDir()).removeRecursively();
}

void HistoryWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    // Narrow (e.g. a tiled half screen): the preview goes below the grid instead of beside it.
    m_split->setOrientation(width() < 900 ? Qt::Vertical : Qt::Horizontal);
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
    auto* list = new Grid(m_store, this);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(m_tile);
    list->setGridSize(QSize(m_tile.width() + 24, m_tile.height() + kTileLines * list->fontMetrics().lineSpacing() + 20));
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setDragDropMode(QAbstractItemView::DragOnly);
    list->setUniformItemSizes(true);
    list->setMouseTracking(true);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setItemDelegate(new TileDelegate(list));
    list->viewport()->installEventFilter(this);
    return list;
}

QListWidget* HistoryWindow::currentList() const
{
    return m_tabs->currentWidget() == m_shots ? m_shots : m_list;
}

QStringList HistoryWindow::selectedIds(const QListWidget* list) const
{
    QStringList ids;
    for(int i = 0; i < list->count(); ++i)
        if(list->item(i)->isSelected())
            ids << list->item(i)->data(kIdRole).toString();
    return ids;
}

QStringList HistoryWindow::sheetMembers() const
{
    QList<const HistoryEntry*> members;
    for(const QString& id : selectedIds(currentList()))
        if(const HistoryEntry* e = m_store.find(id); e && !e->sheet && QFileInfo::exists(e->master))
            members << e;
    std::stable_sort(members.begin(), members.end(),
                     [](const HistoryEntry* a, const HistoryEntry* b) { return a->created < b->created; });
    QStringList ids;
    for(const HistoryEntry* e : members)
        ids << e->id;
    return ids;
}

void HistoryWindow::copyFiles(const QStringList& paths)
{
    if(paths.isEmpty())
        return;
    copyFilesToClipboard(paths);
    emit notify("Copied to clipboard",
                paths.size() == 1 ? QFileInfo(paths.first()).fileName() : QStringLiteral("%1 files").arg(paths.size()),
                false);
}

void HistoryWindow::copySelection()
{
    QStringList paths;
    for(const QString& id : selectedIds(currentList()))
        if(const HistoryEntry* e = m_store.find(id); e && QFileInfo::exists(copyPath(*e)))
            paths << copyPath(*e);
    copyFiles(paths);
}

void HistoryWindow::requestDelete()
{
    const QStringList ids = selectedIds(currentList());
    if(!ids.isEmpty())
        emit deleteRequested(ids);
}

void HistoryWindow::requestRename()
{
    if(const QStringList ids = selectedIds(currentList()); ids.size() == 1)
        emit renameRequested(ids.first());
}

QString HistoryWindow::tileText(const HistoryEntry& e, const QString& second, QStringList third) const
{
    const QString date = QLocale().toString(e.created, "MMM d, HH:mm");
    if(!hasGeneratedName(e))
        third << date; // the title doesn't say it
    if(e.sheet)
        third.prepend(QStringLiteral("sheet of %1").arg(e.sheet->members.size()));
    return QStringList{hasGeneratedName(e) ? date : entryName(e), second, third.join(" · ")}.join('\n');
}

void HistoryWindow::requestSheet()
{
    const QStringList ids = sheetMembers();
    if(ids.size() >= 2)
        emit sheetRequested(ids);
}

void HistoryWindow::setTileWidth(int width)
{
    m_tileWidth = std::clamp(width, kMinHistoryTileWidth, kMaxHistoryTileWidth);
    m_tileTimer.start();
}

void HistoryWindow::applyTileSize()
{
    const int width = m_tileWidth;
    if(width == m_tile.width())
        return;
    m_tile = QSize(width, width * 9 / 16);
    for(QListWidget* list : {m_list, m_shots})
    {
        list->setIconSize(m_tile);
        list->setGridSize(QSize(m_tile.width() + 24, m_tile.height() + kTileLines * list->fontMetrics().lineSpacing() + 20));
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

QStringList HistoryWindow::badges(const HistoryEntry& e) const
{
    if(e.sheet)
        return {"SHEET"};
    if(e.kind == EntryKind::Screenshot)
        return editedFor(e, "png").isEmpty() ? QStringList() : QStringList{"EDITED"};
    QStringList out;
    for(const char* ext : {"gif", "mp4"})
        if(!editedFor(e, ext).isEmpty())
            out << QString(ext).toUpper();
    return out;
}

const QList<QPixmap>& HistoryWindow::framesFor(const HistoryEntry& e)
{
    auto it = m_frames.find(e.id);
    if(it != m_frames.end() && !it->isEmpty())
        return *it;
    const QStringList marks = badges(e);
    QList<QPixmap> frames;
    const QPixmap sheet(e.thumb);
    if(!sheet.isNull())
    {
        const int cell = sheet.width() / kSpriteFrames;
        for(int i = 0; i < kSpriteFrames; ++i)
        {
            const QPixmap f = sheet.copy(i * cell, 0, cell, sheet.height());
            // tile pads missing frames with black; stop at the first fully black cell
            if(i > 0 && f.toImage().scaled(4, 4).pixelColor(1, 1).value() < 3)
                break;
            frames << withBadges(fitTile(f), marks);
        }
    }
    if(frames.isEmpty())
        frames << withBadges(fitTile({}), marks);
    return *m_frames.insert(e.id, frames);
}

void HistoryWindow::refresh()
{
    m_frames.clear(); // thumbnails may have been regenerated
    m_shotTiles.clear();
    m_previewId.clear(); // edited files may have changed
    m_sheetPreviewOf.clear();
    {
        // Rebuilding clears the selection; keep that from restarting the preview.
        const QSignalBlocker blockRecordings(m_list);
        const QSignalBlocker blockShots(m_shots);
        refreshRecordings();
        refreshShots();
    }
    updateHeader();
    updatePreview();
}

void HistoryWindow::refreshRecordings()
{
    setRecordingHover(nullptr);
    const QStringList selected = selectedIds(m_list);
    const QString current = m_list->currentItem() ? m_list->currentItem()->data(kIdRole).toString() : QString();
    m_list->clear();

    const QLocale loc;
    for(const HistoryEntry& e : m_store.entries())
    {
        if(e.kind != EntryKind::Recording)
            continue;
        const QFileInfo master(e.master);
        const bool hasMaster = master.exists();
        const QString length = QStringLiteral("%1 s").arg(e.duration, 0, 'f', 1);
        const QString pixels = QStringLiteral("%1×%2").arg(e.size.width()).arg(e.size.height());
        const QString text = tileText(e, hasMaster ? length + " · " + loc.formattedDataSize(master.size(), 1) : "(recording missing)",
                                      {pixels});

        auto* item = new QListWidgetItem(QIcon(framesFor(e).first()), text, m_list);
        item->setData(kIdRole, e.id);
        item->setToolTip(QStringLiteral("%1\nDouble-click to open in the editor").arg(e.master));
        if(!hasMaster)
            item->setForeground(QColor("#8a8f98"));
        if(e.id == current)
            m_list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        item->setSelected(selected.contains(e.id));
    }
}

QPixmap HistoryWindow::shotTile(const HistoryEntry& e)
{
    if(auto it = m_shotTiles.constFind(e.id); it != m_shotTiles.constEnd())
        return *it;
    QPixmap thumb(e.thumb);
    if(thumb.isNull())
    {
        const QString edited = editedFor(e, "png");
        thumb = readScaled(edited.isEmpty() ? e.master : edited);
    }
    return *m_shotTiles.insert(e.id, withBadges(fitTile(thumb), badges(e)));
}

void HistoryWindow::refreshShots()
{
    const QStringList selected = selectedIds(m_shots);
    const QString current = m_shots->currentItem() ? m_shots->currentItem()->data(kIdRole).toString() : QString();
    m_shots->clear();

    const QLocale loc;
    for(const HistoryEntry& e : m_store.entries())
    {
        if(e.kind != EntryKind::Screenshot)
            continue;
        const QFileInfo source(e.master);
        const bool hasSource = source.exists();
        const QString pixels = QStringLiteral("%1×%2").arg(e.size.width()).arg(e.size.height());
        const QString text = tileText(e, hasSource ? loc.formattedDataSize(source.size(), 1) : "(file missing)", {pixels});

        auto* item = new QListWidgetItem(QIcon(shotTile(e)), text, m_shots);
        item->setData(kIdRole, e.id);
        item->setToolTip(QStringLiteral("%1\nDouble-click to open in the editor").arg(e.master));
        if(!hasSource)
            item->setForeground(QColor("#8a8f98"));
        if(e.id == current)
            m_shots->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        item->setSelected(selected.contains(e.id));
    }
}

void HistoryWindow::updateHeader()
{
    const QListWidget* list = currentList();
    const int n = list->count();
    const qsizetype selected = selectedIds(list).size();
    QString text = QStringLiteral("%1 %2%3").arg(n).arg(list == m_shots ? "screenshot" : "recording").arg(n == 1 ? "" : "s");
    if(selected > 1)
        text += QStringLiteral(" · %1 selected").arg(selected);
    m_count->setText(text);
    m_makeSheet->setVisible(sheetMembers().size() >= 2);
}

QList<HistoryWindow::Variant> HistoryWindow::variants(const HistoryEntry& e) const
{
    QList<Variant> out{{"Source", e.master}};
    if(e.sheet)
        return out;
    if(e.kind == EntryKind::Screenshot)
    {
        if(const QString edited = editedFor(e, "png"); !edited.isEmpty())
            out << Variant{"Edited", edited};
        return out;
    }
    for(const char* ext : {"gif", "mp4"})
        if(const QString edited = editedFor(e, ext); !edited.isEmpty())
            out << Variant{QString(ext).toUpper(), edited};
    return out;
}

void HistoryWindow::updatePreview()
{
    QListWidget* list = currentList();
    const QStringList ids = selectedIds(list);
    if(ids.size() > 1)
    {
        m_previewId.clear();
        m_preview->setVariants({}, 0);
        if(sheetMembers().size() >= 2)
            m_sheetTimer.start();
        else
        {
            stopSheetVideo();
            m_sheetPreviewOf.clear();
            m_preview->clear(QStringLiteral("%1 %2 selected").arg(ids.size()).arg(list == m_shots ? "items" : "recordings"));
        }
        return;
    }
    m_sheetTimer.stop();
    stopSheetVideo();
    m_sheetPreviewOf.clear();
    const HistoryEntry* e = ids.isEmpty() ? nullptr : m_store.find(ids.first());
    if(!e)
    {
        m_previewId.clear();
        m_preview->setVariants({}, 0);
        m_preview->clear(list == m_shots ? "Select a screenshot to preview it." : "Select a recording to preview it.");
        return;
    }
    if(e->id == m_previewId)
        return; // keeps the version picked in the switch
    m_previewId = e->id;
    m_variants = variants(*e);
    // Starts on what Ctrl+C copies.
    int current = 0;
    for(int i = 0; i < m_variants.size(); ++i)
        if(m_variants[i].path == copyPath(*e))
            current = i;
    QStringList labels;
    for(const Variant& v : std::as_const(m_variants))
        labels << v.label;
    m_preview->setVariants(labels, current);
    showVariant(current);
}

void HistoryWindow::showVariant(int index)
{
    const HistoryEntry* e = m_store.find(m_previewId);
    if(!e || index < 0 || index >= m_variants.size())
        return;
    const QString path = m_variants[index].path;
    const QFileInfo fi(path);
    QString details = QFileInfo(path).fileName();
    if(fi.exists())
        details += " · " + QLocale().formattedDataSize(fi.size(), 1);
    m_preview->play(path, QLocale().toString(e->created, "MMM d yyyy, HH:mm:ss"), details + "\n" + path);
}

void HistoryWindow::showSheetPreview()
{
    const QStringList members = sheetMembers();
    if(members.size() < 2 || members == m_sheetPreviewOf)
        return;
    m_sheetPreviewOf = members;
    QList<HistoryEntry> entries;
    for(const QString& id : members)
        entries << *m_store.find(id);
    makeSheetFrames(entries, this, [this, members] { renderSheetPreview(members); });
}

void HistoryWindow::renderSheetPreview(const QStringList& members)
{
    if(members != m_sheetPreviewOf || sheetMembers() != members)
        return; // the selection moved on while the frames were made
    // One factor for every image, so the tiles keep their relative sizes.
    QList<QString> paths;
    int cellWidth = 0, cellHeight = 0;
    for(const QString& id : members)
    {
        paths << sheetImagePath(*m_store.find(id));
        const QSize size = QImageReader(paths.last()).size();
        cellWidth = std::max(cellWidth, size.width());
        cellHeight = std::max(cellHeight, size.height());
    }
    const double factor = cellWidth > kSheetPreviewWidth ? double(kSheetPreviewWidth) / cellWidth : 1.0;
    QList<SheetTile> tiles;
    for(int i = 0; i < members.size(); ++i)
    {
        const HistoryEntry* e = m_store.find(members[i]);
        QImageReader reader(paths[i]);
        const QSize size = reader.size();
        reader.setScaledSize(size * factor);
        tiles << SheetTile{reader.read(), entryName(*e), e->created, size};
    }
    const SheetState state = scaledSheet(newSheet(members, cellHeight), factor);
    const QImage sheet = renderSheet(state, tiles, {}, QDateTime());
    const bool playable = m_store.find(members.first())->kind == EntryKind::Recording && members.size() <= kMaxSheetVideoTiles;
    m_preview->showImage(sheet, QStringLiteral("Contact sheet of %1 items").arg(members.size()),
                         playable ? "Getting it ready to play…" : "Make sheet to arrange, label and save it.");
    if(playable)
        startSheetVideo(members, state, tiles);
    else
        stopSheetVideo();
}

void HistoryWindow::startSheetVideo(const QStringList& members, const SheetState& state, const QList<SheetTile>& tiles)
{
    // Named after what it shows, so going back to a selection plays at once.
    QList<const HistoryEntry*> entries;
    QByteArray key;
    for(const QString& id : members)
    {
        entries << m_store.find(id);
        const QFileInfo source(sheetVideoPath(*entries.last()));
        key += source.filePath().toUtf8() + QByteArray::number(source.lastModified().toMSecsSinceEpoch());
    }
    QDir().mkpath(sheetPreviewDir());
    const QString base = sheetPreviewDir() + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex();
    const QString path = base + ".mp4";
    if(path == m_sheetVideoPath && m_sheetVideo.isRunning())
        return;
    m_sheetVideo.cancel();
    m_sheetVideoPath = path;
    if(QFileInfo::exists(path))
    {
        m_preview->play(path, QStringLiteral("Contact sheet of %1 items").arg(members.size()),
                        "A quick preview. Make sheet to arrange, label and save it.");
        return;
    }
    SheetVideoParams p = sheetVideoParams(state, tiles, entries, {}, QDateTime(), base);
    if(p.tiles.isEmpty())
        return;
    p.preset = "veryfast";
    p.crf = 26;
    p.output = path;
    m_sheetVideo.start(p);
}

void HistoryWindow::stopSheetVideo()
{
    m_sheetVideo.cancel();
    m_sheetVideoPath.clear();
}

void HistoryWindow::setRecordingHover(QListWidgetItem* item)
{
    if(m_hover == item)
        return;
    if(m_hover)
    {
        if(const auto* e = m_store.find(m_hover->data(kIdRole).toString()))
            m_hover->setIcon(QIcon(framesFor(*e).first()));
    }
    m_hover = item;
    m_hoverFrame = 0;
    if(m_hover)
        m_anim.start();
    else
        m_anim.stop();
}

void HistoryWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if(!e->source() && !draggedVideos(e->mimeData()).isEmpty()) // not our own tiles
    {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

void HistoryWindow::dropEvent(QDropEvent* e)
{
    const QStringList videos = draggedVideos(e->mimeData());
    if(e->source() || videos.isEmpty())
        return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
    m_tabs->setCurrentWidget(m_list);
    emit videosDropped(videos);
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
                setTileWidth(m_tileWidth + notches * kHistoryTileStep);
            return true;
        }
    }
    if(event->type() == QEvent::Leave && watched == m_list->viewport())
        setRecordingHover(nullptr);
    return QWidget::eventFilter(watched, event);
}

void HistoryWindow::showMenu(QListWidget* list, const QPoint& pos)
{
    QListWidgetItem* item = list->itemAt(pos);
    if(!item)
        return;
    const QStringList ids = selectedIds(list);
    QMenu menu(this);
    if(ids.size() > 1 && item->isSelected())
    {
        menu.addAction(QStringLiteral("Copy %1 files").arg(ids.size()), this, &HistoryWindow::copySelection);
        if(sheetMembers().size() >= 2)
            menu.addAction("Make sheet", this, &HistoryWindow::requestSheet);
        menu.addSeparator();
        menu.addAction(QStringLiteral("Delete %1 items…").arg(ids.size()), this, &HistoryWindow::requestDelete);
        menu.exec(list->viewport()->mapToGlobal(pos));
        return;
    }

    const QString id = item->data(kIdRole).toString();
    const HistoryEntry* e = m_store.find(id);
    if(!e)
        return;
    const bool hasSource = QFileInfo::exists(e->master);
    menu.addAction("Open in editor", this, [this, id] { emit openRequested(id); })->setEnabled(hasSource);
    menu.addSeparator();
    if(e->sheet)
        menu.addAction("Copy", this, [this, path = e->master] { copyFiles({path}); })->setEnabled(hasSource);
    else
    {
        const QStringList exts = e->kind == EntryKind::Screenshot ? QStringList{"png"} : QStringList{"gif", "mp4"};
        for(const QString& ext : exts)
        {
            const QString edited = editedFor(*e, ext);
            if(edited.isEmpty())
                continue;
            const QString label = e->kind == EntryKind::Screenshot ? QStringLiteral("Copy edited")
                                                                   : "Copy edited " + ext.toUpper();
            menu.addAction(label, this, [this, edited] { copyFiles({edited}); });
        }
        menu.addAction("Copy source", this, [this, path = e->master] { copyFiles({path}); })->setEnabled(hasSource);
    }
    menu.addSeparator();
    menu.addAction("Show in folder", this, [path = e->master] { openFolder(QFileInfo(path).absolutePath()); });
    menu.addSeparator();
    menu.addAction("Rename…", this, [this, id] { emit renameRequested(id); })->setEnabled(hasSource);
    menu.addAction("Delete…", this, [this, id] { emit deleteRequested({id}); });
    menu.exec(list->viewport()->mapToGlobal(pos));
}

} // namespace qr
