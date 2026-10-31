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

namespace
{
const char* const kProbeHeader = "Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'clip.mp4':\n"
                                 "  Duration: 00:01:05.50, start: 0.000000, bitrate: 609 kb/s\n";
const char* const kVideoStream = "  Stream #0:0[0x1](und): Video: h264 (High) (avc1 / 0x31637661), yuv420p(progressive), "
                                 "1280x720 [SAR 1:1 DAR 16:9], 607 kb/s, 24 fps, 24 tbr, 12288 tbn (default)\n";
const char* const kAudioStream = "  Stream #0:1[0x2](und): Audio: aac (LC) (mp4a / 0x6134706D), 44100 Hz, stereo, fltp, 128 kb/s\n";
} // namespace

TEST(FfmpegArgs, ProbeReadsDurationSizeAndAudio)
{
    const auto info = parseProbe(QString(kProbeHeader) + kVideoStream + kAudioStream);
    ASSERT_TRUE(info.has_value());
    EXPECT_DOUBLE_EQ(info->duration, 65.5);
    EXPECT_EQ(info->size, QSize(1280, 720));
    EXPECT_TRUE(info->hasAudio);
}

TEST(FfmpegArgs, ProbeWithoutAudio)
{
    const auto info = parseProbe(QString(kProbeHeader) + kVideoStream);
    ASSERT_TRUE(info.has_value());
    EXPECT_FALSE(info->hasAudio);
}

TEST(FfmpegArgs, ProbeRejectsWhatIsNoWholeVideo)
{
    EXPECT_FALSE(parseProbe("").has_value());
    // A copy in progress: no index yet.
    EXPECT_FALSE(parseProbe("[mov,mp4 @ 0x1] moov atom not found\nclip.mp4: Invalid data found when processing input\n").has_value());
    EXPECT_FALSE(parseProbe(QString("  Duration: N/A, bitrate: N/A\n") + kVideoStream).has_value());
    EXPECT_FALSE(parseProbe(QString(kProbeHeader) + kAudioStream).has_value()); // audio only
}

TEST(FfmpegArgs, FrameSeeksBeforeTheInput)
{
    const QStringList args = frameArgs("in.mp4", 2.5, "out.png");
    EXPECT_EQ(after(args, "-ss"), "2.500");
    EXPECT_LT(args.indexOf("-ss"), args.indexOf("-i"));
    EXPECT_EQ(after(args, "-frames:v"), "1");
    EXPECT_EQ(args.last(), "out.png");
}

TEST(FfmpegArgs, SheetVideoPlaysEveryClipBetweenBackgroundAndLabels)
{
    SheetVideoParams p;
    p.tiles = {{"a.mp4", QRect(16, 16, 640, 360)}, {"b.mov", QRect(672, 16, 640, 359)}};
    p.background = "bg.png";
    p.labels = "labels.png";
    p.color = "#112233";
    p.duration = 7.5;
    p.output = "out.mp4";
    const QStringList args = sheetVideoArgs(p);

    QStringList inputs;
    for(int i = 0; i + 1 < args.size(); ++i)
        if(args[i] == "-i")
            inputs << args[i + 1];
    EXPECT_EQ(inputs, (QStringList{"bg.png", "a.mp4", "b.mov", "labels.png"}));
    EXPECT_EQ(after(args, "-t"), "7.500"); // the stills last as long as the longest clip

    const QString graph = after(args, "-filter_complex");
    EXPECT_TRUE(graph.contains("[1:v]fps=30,scale=640:360"));
    EXPECT_TRUE(graph.contains("[2:v]fps=30,scale=640:359"));
    EXPECT_TRUE(graph.contains("tpad=stop_mode=clone:stop_duration=7.500")); // the shorter ones wait
    EXPECT_TRUE(graph.contains("[0:v][t0]overlay=16:16:shortest=1[s0]"));
    EXPECT_TRUE(graph.contains("[s0][t1]overlay=672:16:shortest=1[s1]"));
    EXPECT_TRUE(graph.contains("[s1][3:v]overlay=0:0")); // labels last, over the clips
    EXPECT_TRUE(graph.contains("pad=ceil(iw/2)*2:ceil(ih/2)*2:color=0x112233"));
    EXPECT_EQ(after(args, "-map"), "[out]");
    EXPECT_TRUE(args.contains("-an"));
    EXPECT_EQ(args.last(), "out.mp4");
}

TEST(FfmpegArgs, SheetVideoEncoderSettings)
{
    SheetVideoParams p;
    p.tiles = {{"a.mp4", QRect(0, 0, 64, 36)}};
    p.duration = 1.0;
    EXPECT_EQ(after(sheetVideoArgs(p), "-preset"), "medium"); // a saved sheet
    EXPECT_EQ(after(sheetVideoArgs(p), "-crf"), "20");
    p.preset = "veryfast";
    p.crf = 26;
    EXPECT_EQ(after(sheetVideoArgs(p), "-preset"), "veryfast");
    EXPECT_EQ(after(sheetVideoArgs(p), "-crf"), "26");
}
