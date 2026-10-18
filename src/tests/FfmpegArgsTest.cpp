#include "core/FfmpegArgs.h"

#include <gtest/gtest.h>

using namespace qr;

namespace
{
// Value following `flag` in args, or empty.
QString after(const QStringList& args, const QString& flag)
{
    const int i = args.indexOf(flag);
    return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : QString();
}

ExportParams exportParams()
{
    ExportParams p;
    p.input = "/m/in.mp4";
    p.start = 1.25;
    p.end = 4.0;
    p.fps = 15;
    p.sourceWidth = 1920;
    p.maxWidth = 800;
    p.gifQuality = 80;
    p.output = "/o/out.gif";
    return p;
}
} // namespace

TEST(FfmpegArgs, CaptureRegionAndMic)
{
    CaptureParams p;
    p.display = ":0.0";
    p.physicalRect = QRect(100, 200, 640, 480);
    p.fps = 30;
    p.drawMouse = false;
    p.audio = AudioInput::Pulse;
    p.output = "/w/seg0.mkv";
    const QStringList a = captureArgs(p);
    EXPECT_EQ(after(a, "-video_size"), "640x480");
    EXPECT_EQ(after(a, "-framerate"), "30");
    EXPECT_EQ(after(a, "-draw_mouse"), "0");
    EXPECT_EQ(a.count("-i"), 2);
    EXPECT_EQ(after(a, "-i"), ":0.0+100,200");
    EXPECT_TRUE(a.contains("pulse"));
    EXPECT_EQ(after(a, "-c:a"), "aac");
    EXPECT_EQ(a.last(), "/w/seg0.mkv");
}

TEST(FfmpegArgs, AVFoundationCropsTheDisplayAndTakesMicFromSameInput)
{
    CaptureParams p;
    p.source = CaptureSource::AVFoundation;
    p.display = "Capture screen 1";
    p.physicalRect = QRect(100, 200, 640, 480);
    p.fps = 25;
    p.audio = AudioInput::AVFoundation;
    p.output = "/w/seg0.mkv";
    QStringList a = captureArgs(p);
    EXPECT_EQ(after(a, "-f"), "avfoundation");
    EXPECT_EQ(after(a, "-framerate"), "25");
    EXPECT_EQ(after(a, "-capture_cursor"), "1");
    EXPECT_EQ(a.count("-i"), 1);
    EXPECT_EQ(after(a, "-i"), "Capture screen 1:default");
    EXPECT_EQ(after(a, "-vf"), "crop=640:480:100:200");
    EXPECT_EQ(after(a, "-c:a"), "aac");
    EXPECT_FALSE(a.contains("-video_size"));

    p.audio = AudioInput::None;
    a = captureArgs(p);
    EXPECT_EQ(after(a, "-i"), "Capture screen 1:none");
    EXPECT_FALSE(a.contains("-c:a"));
}

TEST(FfmpegArgs, CaptureWithoutAudioHasNoAudioCodec)
{
    CaptureParams p;
    p.physicalRect = QRect(0, 0, 100, 100);
    p.output = "/w/seg0.mkv";
    const QStringList a = captureArgs(p);
    EXPECT_EQ(a.count("-i"), 1);
    EXPECT_FALSE(a.contains("-c:a"));
}

TEST(FfmpegArgs, ConcatListEscapesQuotes)
{
    EXPECT_EQ(concatList({"/a/s0.mkv", "/it's/s1.mkv"}), "file '/a/s0.mkv'\nfile '/it'\\''s/s1.mkv'\n");
}

TEST(FfmpegArgs, OutputWidthNeverUpscalesAndIsEven)
{
    EXPECT_EQ(outputWidth(1920, 800), 800);
    EXPECT_EQ(outputWidth(640, 800), 640);
    EXPECT_EQ(outputWidth(641, 0), 640);
    EXPECT_EQ(outputWidth(1920, 801), 800);
}

TEST(FfmpegArgs, VideoFilter)
{
    ExportParams p = exportParams();
    EXPECT_EQ(videoFilter(p), "fps=15,scale=800:-2:flags=lanczos");
    p.maxWidth = 0;
    EXPECT_EQ(videoFilter(p), "fps=15");
    p.fps = 0;
    EXPECT_EQ(videoFilter(p), "");
}

TEST(FfmpegArgs, Mp4ExportTrimsAndDropsAudio)
{
    ExportParams p = exportParams();
    p.output = "/o/out.mp4";
    const QStringList a = mp4ExportArgs(p);
    EXPECT_EQ(after(a, "-ss"), "1.250");
    EXPECT_EQ(after(a, "-t"), "2.750");
    EXPECT_EQ(after(a, "-progress"), "pipe:2");
    EXPECT_TRUE(a.contains("-an"));
    EXPECT_EQ(a.last(), "/o/out.mp4");
    p.keepAudio = true;
    EXPECT_FALSE(mp4ExportArgs(p).contains("-an"));
}

TEST(FfmpegArgs, GifskiPipeline)
{
    const ExportParams p = exportParams();
    const QStringList feed = gifskiFeederArgs(p);
    EXPECT_EQ(after(feed, "-f"), "yuv4mpegpipe");
    EXPECT_EQ(feed.last(), "-");
    const QStringList g = gifskiArgs(p);
    EXPECT_EQ(after(g, "--width"), "800");
    EXPECT_EQ(after(g, "--fps"), "15");
    EXPECT_EQ(after(g, "--quality"), "80");
    EXPECT_EQ(after(g, "-o"), "/o/out.gif");
    EXPECT_EQ(g.last(), "-");
}

TEST(FfmpegArgs, PaletteFallback)
{
    const QStringList a = paletteGifArgs(exportParams());
    EXPECT_TRUE(after(a, "-vf").startsWith("fps=15,scale=800:-2:flags=lanczos,split"));
    EXPECT_TRUE(after(a, "-vf").contains("palettegen"));
    EXPECT_EQ(after(a, "-loop"), "0");
}

TEST(FfmpegArgs, OverlayUsesFiltergraphAndKeepsTrimAsOutputOption)
{
    ExportParams p = exportParams();
    p.overlayImage = "/c/text.png";
    p.output = "/o/out.mp4";
    p.keepAudio = true;
    const QStringList a = mp4ExportArgs(p);
    EXPECT_EQ(a.count("-i"), 2);
    // -t must come after the overlay input, or it would only limit the still image.
    EXPECT_GT(a.indexOf("-t"), a.lastIndexOf("/c/text.png"));
    EXPECT_FALSE(a.contains("-vf"));
    EXPECT_EQ(after(a, "-filter_complex"), "[0:v][1:v]overlay=0:0:format=auto,fps=15,scale=800:-2:flags=lanczos[v]");
    EXPECT_EQ(a.count("-map"), 2);
    EXPECT_TRUE(a.contains("0:a?"));

    const QStringList feed = gifskiFeederArgs(p);
    EXPECT_EQ(feed.count("-map"), 1);
    EXPECT_EQ(after(feed, "-map"), "[v]");

    const QString pal = after(paletteGifArgs(p), "-filter_complex");
    EXPECT_TRUE(pal.startsWith("[0:v][1:v]overlay=0:0:format=auto,fps=15,"));
    EXPECT_TRUE(pal.endsWith("paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle[v]"));
}

TEST(FfmpegArgs, CropComesFirstAndDrivesScaling)
{
    ExportParams p = exportParams();
    p.crop = QRect(10, 20, 1200, 600);
    EXPECT_EQ(videoFilter(p), "crop=1200:600:10:20,fps=15,scale=800:-2:flags=lanczos");
    EXPECT_EQ(after(gifskiArgs(p), "--width"), "800");
    p.crop = QRect(10, 20, 400, 300); // narrower than maxWidth: no scale
    EXPECT_EQ(videoFilter(p), "crop=400:300:10:20,fps=15");
    EXPECT_EQ(after(gifskiArgs(p), "--width"), "400");
}

TEST(FfmpegArgs, CropBeforeOverlay)
{
    ExportParams p = exportParams();
    p.crop = QRect(10, 20, 1200, 600);
    p.overlayImage = "/c/text.png";
    EXPECT_EQ(after(mp4ExportArgs(p), "-filter_complex"),
              "[0:v]crop=1200:600:10:20[c];[c][1:v]overlay=0:0:format=auto,fps=15,scale=800:-2:flags=lanczos[v]");
}

TEST(FfmpegArgs, Sprite)
{
    const QStringList a = spriteArgs("/m/in.mp4", 4.0, 8, 240, "/t/s.png");
    EXPECT_EQ(after(a, "-vf"), "fps=2.0000,scale=240:-2,tile=8x1");
    EXPECT_EQ(a.last(), "/t/s.png");
}

TEST(FfmpegArgs, ParseProgress)
{
    EXPECT_DOUBLE_EQ(parseProgressSeconds("out_time_us=2500000"), 2.5);
    EXPECT_DOUBLE_EQ(parseProgressSeconds("out_time_ms=1000000"), 1.0);
    EXPECT_DOUBLE_EQ(parseProgressSeconds("out_time_us=N/A"), -1.0);
    EXPECT_DOUBLE_EQ(parseProgressSeconds("frame=12"), -1.0);
}

TEST(FfmpegArgs, OverlaySequenceIsAConcatInputCompositedLikeTheImage)
{
    ExportParams p = exportParams();
    p.overlaySequence = "/c/keys.txt";
    const QStringList a = mp4ExportArgs(p);
    const int seq = a.indexOf("/c/keys.txt");
    ASSERT_GT(seq, 4);
    EXPECT_EQ(a.mid(seq - 5, 5), (QStringList{"-f", "concat", "-safe", "0", "-i"}));
    EXPECT_TRUE(after(a, "-filter_complex").contains("[1:v]overlay=0:0"));
}

TEST(FfmpegArgs, CaptureReportsProgressOnStdout)
{
    CaptureParams p;
    p.physicalRect = QRect(0, 0, 100, 100);
    p.output = "/w/seg0.mkv";
    EXPECT_EQ(after(captureArgs(p), "-progress"), "pipe:1");
}
