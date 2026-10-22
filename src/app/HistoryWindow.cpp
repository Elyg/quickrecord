#include "app/HistoryWindow.h"

#include "app/Clipboard.h"
#include "app/FlowLayout.h"
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
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace qr
{

namespace
{
constexpr int kMinTileWidth = 120;
constexpr int kMaxTileWidth = 480;
constexpr int kTileStep = 20;
constexpr int kIdRole = Qt::UserRole + 1; // entry id
constexpr int kSheetPreviewWidth = 640;   // tile width of the multi-selection sheet preview

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

// What Ctrl+C copies: the newest edited file, else the source.
QString copyPath(const HistoryEntry& e)
{
    const QString edited = latestEdited(e);
    return edited.isEmpty() ? e.master : edited;
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
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_list, "Recordings");
    m_tabs->addTab(m_shots, "Screenshots");

    m_preview = new PreviewPane(this);
    m_count = new QLabel(this);
    m_openFolder = new QPushButton("Open folder", this);
    m_makeSheet = new QPushButton("Make sheet", this);
    m_makeSheet->setToolTip("Put the selected screenshots on one contact sheet");
    m_makeSheet->hide();

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

    // Wraps when the window is narrow.
    auto* top = new FlowLayout(nullptr, 16, 6);
    top->addWidget(m_count);
    top->addLayout(group({sizeLabel, m_tileSlider}, 6));
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

    refresh();
}

HistoryWindow::~HistoryWindow()
{
    // A popped-out preview is a separate top-level window, not our child.
    if(m_preview && m_preview->parentWidget() == nullptr)
        delete m_preview;
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
    if(currentList() != m_shots)
        return {};
    QList<const HistoryEntry*> shots;
    for(const QString& id : selectedIds(m_shots))
        if(const HistoryEntry* e = m_store.find(id); e && !e->sheet && QFileInfo::exists(e->master))
            shots << e;
    std::stable_sort(shots.begin(), shots.end(),
                     [](const HistoryEntry* a, const HistoryEntry* b) { return a->created < b->created; });
    QStringList ids;
    for(const HistoryEntry* e : shots)
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

void HistoryWindow::requestSheet()
{
    const QStringList ids = sheetMembers();
    if(ids.size() >= 2)
        emit sheetRequested(ids);
}

void HistoryWindow::applyTileSize()
{
    const int width = m_tileSlider->value();
    if(width == m_tile.width())
        return;
    m_tile = QSize(width, width * 9 / 16);
    for(QListWidget* list : {m_list, m_shots})
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
        QString text = loc.toString(e.created, "MMM d, HH:mm") +
                       QStringLiteral("\n%1 s · %2×%3").arg(e.duration, 0, 'f', 1).arg(e.size.width()).arg(e.size.height());
        if(hasMaster)
            text += " · " + loc.formattedDataSize(master.size(), 1);
        if(!hasMaster)
            text += "\n(recording missing)";

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
        QString text = loc.toString(e.created, "MMM d, HH:mm") +
                       QStringLiteral("\n%1×%2").arg(e.size.width()).arg(e.size.height());
        if(hasSource)
            text += " · " + loc.formattedDataSize(source.size(), 1);
        if(e.sheet)
            text += QStringLiteral("\nsheet of %1").arg(e.sheet->members.size());
        if(!hasSource)
            text += "\n(file missing)";

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
            m_sheetPreviewOf.clear();
            m_preview->clear(QStringLiteral("%1 %2 selected").arg(ids.size()).arg(list == m_shots ? "items" : "recordings"));
        }
        return;
    }
    m_sheetTimer.stop();
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
    // One factor for every image, so the tiles keep their relative sizes.
    QList<QString> paths;
    int cellWidth = 0, cellHeight = 0;
    for(const QString& id : members)
    {
        const HistoryEntry* e = m_store.find(id);
        const QString edited = editedFor(*e, "png");
        paths << (edited.isEmpty() ? e->master : edited);
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
        tiles << SheetTile{reader.read(), e->id, e->created, size};
    }
    const QImage sheet = renderSheet(scaledSheet(newSheet(members, cellHeight), factor), tiles, {}, QDateTime());
    m_preview->showImage(sheet, QStringLiteral("Contact sheet of %1 screenshots").arg(members.size()),
                         "Make sheet to arrange, label and save it.");
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
    menu.addAction("Delete…", this, [this, id] { emit deleteRequested({id}); });
    menu.exec(list->viewport()->mapToGlobal(pos));
}

} // namespace qr
