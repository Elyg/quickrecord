#include "core/InputLog.h"

#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace qr;

namespace
{
InputEvent key(double t, const QString& label, bool typed = false)
{
    InputEvent e;
    e.time = t;
    e.label = label;
    e.typed = typed;
    return e;
}

InputEvent click(double t, QPoint pos)
{
    InputEvent e;
    e.kind = InputEvent::Kind::Click;
    e.time = t;
    e.pos = pos;
    return e;
}
} // namespace

TEST(InputLog, PathSitsNextToTheMaster)
{
    EXPECT_EQ(inputLogPath("/m/qr-20261018-105558.mp4"), "/m/qr-20261018-105558.input.json");
}

TEST(InputLog, SaveLoadRoundTrip)
{
    QTemporaryDir dir;
    const QString path = dir.filePath("a.input.json");
    const QList<InputEvent> events = {key(0.5, "⌘⇧R"), key(1.25, "h", true), click(2.0, QPoint(30, 40))};
    ASSERT_TRUE(saveInputLog(path, events));
    EXPECT_EQ(loadInputLog(path), events);
    EXPECT_TRUE(loadInputLog(dir.filePath("missing.json")).isEmpty());
}

TEST(InputLog, WallClockToRecordingTimeSkipsPauses)
{
    // Captured 1000–3000 ms and 5000–6000 ms (paused in between).
    const QList<CaptureSegment> segments = {{1000, 2000}, {5000, 1000}};
    const QList<InputEvent> wall = {key(500, "a"), key(1500, "b"), key(4000, "c"), key(5250, "d"), key(7000, "e")};
    const QList<InputEvent> rec = toRecordingTime(wall, segments);
    ASSERT_EQ(rec.size(), 2);
    EXPECT_EQ(rec[0].label, "b");
    EXPECT_DOUBLE_EQ(rec[0].time, 0.5);
    EXPECT_EQ(rec[1].label, "d");
    EXPECT_DOUBLE_EQ(rec[1].time, 2.25);
}

TEST(InputLog, TypingMergesShortcutsStaySeparateAndChipsExpire)
{
    const QList<InputEvent> events = {key(1.0, "h", true), key(1.2, "i", true), key(1.4, "⌘S"), key(1.5, "x", true),
                                      key(4.0, "y", true)};
    EXPECT_TRUE(inputFrameAt(events, 0.5).isEmpty());
    EXPECT_EQ(inputFrameAt(events, 1.3).keys, QStringList{"hi"});
    EXPECT_EQ(inputFrameAt(events, 1.6).keys, (QStringList{"hi", "⌘S", "x"}));
    EXPECT_EQ(inputFrameAt(events, 3.5).keys, QStringList{}); // all expired
    EXPECT_EQ(inputFrameAt(events, 4.1).keys, QStringList{"y"}); // too long a pause to continue "x"
}

TEST(InputLog, ClickRingsAnimateThenDisappear)
{
    const QList<InputEvent> events = {click(1.0, QPoint(10, 20))};
    EXPECT_TRUE(inputFrameAt(events, 0.9).rings.isEmpty());
    const InputFrame f = inputFrameAt(events, 1.0);
    ASSERT_EQ(f.rings.size(), 1);
    EXPECT_EQ(f.rings[0].first, QPoint(10, 20));
    EXPECT_DOUBLE_EQ(f.rings[0].second, 0.0);
    EXPECT_TRUE(inputFrameAt(events, 2.0).rings.isEmpty());
}

TEST(InputLog, FrameTimesCoverChangesWithinTheTrim)
{
    const QList<InputEvent> events = {key(1.0, "a", true), click(3.0, QPoint())};
    const QList<double> times = inputFrameTimes(events, 0.5, 3.2, 10);
    ASSERT_GE(times.size(), 4);
    EXPECT_DOUBLE_EQ(times.first(), 0.5);
    EXPECT_TRUE(times.contains(1.0));
    EXPECT_TRUE(times.contains(2.6)); // the chip expires
    EXPECT_TRUE(times.contains(3.0));
    EXPECT_TRUE(std::is_sorted(times.begin(), times.end()));
    EXPECT_LT(times.last(), 3.2);
}

TEST(InputLog, OverlayRendersChipsInTheCorner)
{
    InputFrame f;
    f.keys = {"⌘S"};
    const QImage img = renderInputOverlay(f, QSize(400, 300), TextPosition::BottomLeft);
    EXPECT_GT(qAlpha(img.pixel(40, 270)), 0);  // chip near the bottom-left
    EXPECT_EQ(qAlpha(img.pixel(370, 20)), 0); // nothing top-right
}

TEST(InputLog, SequenceListRepeatsTheLastFile)
{
    EXPECT_EQ(overlaySequenceList({"/c/0.png", "/c/it's.png"}, {0.5, 1.25}),
              "ffconcat version 1.0\nfile '/c/0.png'\nduration 0.500\nfile '/c/it'\\''s.png'\nduration 1.250\n"
              "file '/c/it'\\''s.png'\n");
}
