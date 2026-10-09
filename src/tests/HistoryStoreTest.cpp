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
        ASSERT_TRUE(store.addExport("a", "/x/a.gif"));
        ASSERT_TRUE(store.addExport("a", "/x/a.mp4"));
        ASSERT_TRUE(store.addExport("a", "/x/a.gif")); // re-export moves to the end
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
    EXPECT_EQ(a->exports, QStringList({"/x/a.mp4", "/x/a.gif"}));
}

TEST(HistoryStore, RemoveAndUnknownIds)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    EXPECT_FALSE(store.remove("zzz"));
    EXPECT_FALSE(store.addExport("zzz", "/x"));
    EXPECT_TRUE(store.remove("a"));
    EXPECT_TRUE(store.entries().isEmpty());
}

TEST(HistoryStore, RemoveExport)
{
    QTemporaryDir dir;
    HistoryStore store(dir.filePath("index.json"));
    store.add(entry("a"));
    store.addExport("a", "/x/a.gif");
    store.addExport("a", "/x/a.mp4");
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
