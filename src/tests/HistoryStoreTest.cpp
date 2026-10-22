#include "core/HistoryStore.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace qr;

namespace
{
HistoryEntry entry(const QString& id)
{
    HistoryEntry e;
    e.id = id;
    e.created = QDateTime(QDate(2026, 10, 9), QTime(15, 30, 12));
    e.master = "/m/" + id + ".mp4";
    e.thumb = "/t/" + id + ".png";
    e.duration = 4.5;
    e.size = QSize(800, 600);
    e.hasAudio = true;
    return e;
}
} // namespace

TEST(HistoryStore, RoundTripNewestFirst)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("sub/index.json");
    {
        HistoryStore store(path);
        ASSERT_TRUE(store.load()); // missing file is fine
        store.add(entry("a"));
        store.add(entry("b"));
        ASSERT_TRUE(store.setEdited("a", "/x/a-edited.gif"));
        ASSERT_TRUE(store.setEdited("a", "/x/a-edited.mp4"));
        ASSERT_TRUE(store.setEdited("a", "/x/a-edited.gif")); // saved again: moves to the end
        ASSERT_TRUE(store.save());
    }
    HistoryStore store(path);
    ASSERT_TRUE(store.load());
    ASSERT_EQ(store.entries().size(), 2);
    EXPECT_EQ(store.entries()[0].id, "b");
    const HistoryEntry* a = store.find("a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->created, entry("a").created);
    EXPECT_EQ(a->size, QSize(800, 600));
    EXPECT_DOUBLE_EQ(a->duration, 4.5);
    EXPECT_TRUE(a->hasAudio);
    EXPECT_EQ(a->exports, QStringList({"/x/a-edited.mp4", "/x/a-edited.gif"}));
}

TEST(HistoryStore, SetEditedKeepsOneFilePerExtension)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    store.setEdited("a", "/x/a-edited.gif");
    store.setEdited("a", "/x/a-edited.mp4");
    store.setEdited("a", "/y/other.GIF"); // replaces the GIF, whatever its name
    EXPECT_EQ(store.find("a")->exports, QStringList({"/x/a-edited.mp4", "/y/other.GIF"}));
}

TEST(HistoryStore, EditedPathsAndLookups)
{
    QTemporaryDir dir;
    auto touch = [&](const QString& name) {
        QFile f(dir.filePath(name));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        return f.fileName();
    };
    HistoryEntry e = entry("qr-1");
    e.master = dir.filePath("qr-1.mp4");
    EXPECT_EQ(editedPath(e, "gif"), dir.filePath("qr-1-edited.gif"));
    EXPECT_EQ(latestEdited(e), QString());

    e.exports = {touch("qr-1-edited.gif"), dir.filePath("qr-1-edited.mp4")}; // the MP4 is gone
    EXPECT_EQ(editedFor(e, "gif"), dir.filePath("qr-1-edited.gif"));
    EXPECT_EQ(editedFor(e, "mp4"), QString());
    EXPECT_EQ(latestEdited(e), dir.filePath("qr-1-edited.gif"));
    touch("qr-1-edited.mp4");
    EXPECT_EQ(latestEdited(e), dir.filePath("qr-1-edited.mp4"));
}

TEST(HistoryStore, SheetRoundTrip)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("index.json");
    SheetState sheet;
    sheet.members = {"qs-1", "qs-2", "qs-3"};
    sheet.columns = 2;
    sheet.gap = 24;
    sheet.background = QColor(10, 20, 30);
    sheet.sheetText[TextPosition::BottomCenter] = "Release {date}";
    sheet.everyTile[TextPosition::TopLeft] = "{n}";
    sheet.perTile["qs-2"][TextPosition::BottomRight] = "the bug";
    sheet.style.fontSize = 40;
    sheet.style.color = QColor(255, 255, 0, 200);
    sheet.style.background = false;
    {
        HistoryStore store(path);
        HistoryEntry e = entry("qsheet-1");
        e.kind = EntryKind::Screenshot;
        store.add(e);
        store.add(entry("qs-1"));
        ASSERT_TRUE(store.setSheet("qsheet-1", sheet));
        EXPECT_FALSE(store.setSheet("zzz", sheet));
        ASSERT_TRUE(store.save());
    }
    HistoryStore store(path);
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.find("qsheet-1")->sheet.has_value());
    EXPECT_EQ(*store.find("qsheet-1")->sheet, sheet);
    EXPECT_FALSE(store.find("qs-1")->sheet.has_value());
}

TEST(HistoryStore, RemoveAndUnknownIds)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    EXPECT_FALSE(store.remove("zzz"));
    EXPECT_FALSE(store.setEdited("zzz", "/x.gif"));
    EXPECT_TRUE(store.remove("a"));
    EXPECT_TRUE(store.entries().isEmpty());
}

TEST(HistoryStore, AddingAKnownIdReplacesItInPlace)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    store.add(entry("b"));
    HistoryEntry a = entry("a");
    a.duration = 9;
    store.add(a);
    ASSERT_EQ(store.entries().size(), 2);
    EXPECT_EQ(store.entries()[0].id, "b"); // a stays where it was
    EXPECT_DOUBLE_EQ(store.entries()[1].duration, 9);
}

TEST(HistoryStore, RemoveExport)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    store.setEdited("a", "/x/a.gif");
    store.setEdited("a", "/x/a.mp4");
    EXPECT_TRUE(store.removeExport("/x/a.gif"));
    EXPECT_FALSE(store.removeExport("/x/a.gif"));
    EXPECT_EQ(store.find("a")->exports, QStringList({"/x/a.mp4"}));
}

TEST(HistoryStore, EditsRoundTripAndDefaultsAreDropped)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("index.json");
    EditState edits;
    edits.trimStartMs = 500;
    edits.trimEndMs = 2500;
    edits.crop = QRect(10, 20, 300, 200);
    edits.text.text = "Hello";
    edits.text.position = TextPosition::TopCenter;
    edits.text.fontSize = 40;
    edits.text.color = QColor(255, 0, 0, 128);
    edits.text.background = false;
    Annotation arrow;
    arrow.tool = AnnotationTool::Arrow;
    arrow.color = QColor(0, 128, 255, 200);
    arrow.width = 6;
    arrow.points = {QPointF(1.5, 2), QPointF(100, 50.25)};
    Annotation label;
    label.tool = AnnotationTool::Text;
    label.points = {QPointF(10, 10)};
    label.text = "Click here\nthen here";
    edits.annotations = {arrow, label};
    {
        HistoryStore store(path);
        store.add(entry("a"));
        store.add(entry("b"));
        ASSERT_TRUE(store.setEdits("a", edits));
        ASSERT_TRUE(store.setEdits("b", EditState{})); // default → nothing stored
        ASSERT_TRUE(store.save());
    }
    HistoryStore store(path);
    ASSERT_TRUE(store.load());
    ASSERT_TRUE(store.find("a")->edits.has_value());
    EXPECT_EQ(*store.find("a")->edits, edits);
    EXPECT_FALSE(store.find("b")->edits.has_value());
    EXPECT_FALSE(store.setEdits("zzz", edits));
}

TEST(HistoryStore, KindRoundTrip)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("index.json");
    {
        HistoryStore store(path);
        HistoryEntry shot = entry("qs-1");
        shot.kind = EntryKind::Screenshot;
        shot.duration = 0;
        store.add(entry("qr-1"));
        store.add(shot);
        ASSERT_TRUE(store.save());
    }
    HistoryStore store(path);
    ASSERT_TRUE(store.load());
    EXPECT_EQ(store.find("qr-1")->kind, EntryKind::Recording);
    EXPECT_EQ(store.find("qs-1")->kind, EntryKind::Screenshot);
}

TEST(HistoryStore, CorruptIndexFailsToLoad)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("index.json");
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write("{not json");
    f.close();
    HistoryStore store(path);
    EXPECT_FALSE(store.load());
}

TEST(HistoryStore, PruneMissingDropsDeletedMastersAndExports)
{
    QTemporaryDir dir;
    auto touch = [&](const QString& name) {
        QFile f(dir.filePath(name));
        EXPECT_TRUE(f.open(QIODevice::WriteOnly));
        return f.fileName();
    };
    HistoryStore store(dir.filePath("index.json"));
    HistoryEntry kept = entry("kept");
    kept.master = touch("kept.mp4");
    kept.exports = {touch("kept.gif"), dir.filePath("deleted.gif")};
    HistoryEntry gone = entry("gone");
    gone.master = dir.filePath("gone.mp4");
    store.add(kept);
    store.add(gone);

    QList<HistoryEntry> removed;
    ASSERT_TRUE(store.pruneMissing(&removed));
    ASSERT_EQ(removed.size(), 1);
    EXPECT_EQ(removed.first().id, "gone");
    ASSERT_EQ(store.entries().size(), 1);
    EXPECT_EQ(store.entries().first().exports, QStringList{dir.filePath("kept.gif")});
    EXPECT_FALSE(store.pruneMissing()); // nothing left to drop
}
