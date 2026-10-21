#include "core/InputLog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace qr
{

namespace
{
constexpr double kKeyHold = 1.6;   // seconds a chip stays after its last key
constexpr double kTypedGap = 1.2;  // typing pauses shorter than this continue the same chip
constexpr int kMaxChips = 4;
constexpr int kMaxTyped = 24;      // characters shown of a typed run (its end)
constexpr double kRingLife = 0.45; // seconds a click ring expands and fades

QString chipText(const QString& raw)
{
    if(raw.trimmed().isEmpty())
        return QStringLiteral("␣");
    return raw.size() > kMaxTyped ? "…" + raw.right(kMaxTyped - 1) : raw;
}
} // namespace

QString inputLogPath(const QString& masterPath)
{
    const QFileInfo fi(masterPath);
    return fi.path() + "/" + fi.completeBaseName() + ".input.json";
}

bool saveInputLog(const QString& path, const QList<InputEvent>& events)
{
    QJsonArray list;
    for(const InputEvent& e : events)
    {
        QJsonObject o{{"t", std::round(e.time * 1000) / 1000}};
        if(e.kind == InputEvent::Kind::Click)
        {
            o["k"] = "click";
            o["x"] = e.pos.x();
            o["y"] = e.pos.y();
        }
        else
        {
            o["k"] = "key";
            o["label"] = e.label;
            if(e.typed)
                o["typed"] = true;
        }
        list.append(o);
    }
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"events", list}}).toJson(QJsonDocument::Compact));
    return f.commit();
}

QList<InputEvent> loadInputLog(const QString& path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly))
        return {};
    QList<InputEvent> out;
    for(const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).object().value("events").toArray())
    {
        const QJsonObject o = v.toObject();
        InputEvent e;
        e.time = o.value("t").toDouble();
        if(o.value("k").toString() == "click")
        {
            e.kind = InputEvent::Kind::Click;
            e.pos = QPoint(o.value("x").toInt(), o.value("y").toInt());
        }
        else
        {
            e.label = o.value("label").toString();
            e.typed = o.value("typed").toBool();
            if(e.label.isEmpty())
                continue;
        }
        out << e;
    }
    std::stable_sort(out.begin(), out.end(), [](const InputEvent& a, const InputEvent& b) { return a.time < b.time; });
    return out;
}

QList<InputEvent> toRecordingTime(const QList<InputEvent>& events, const QList<CaptureSegment>& segments)
{
    QList<InputEvent> out;
    for(InputEvent e : events)
    {
        qint64 before = 0; // recording time at the start of the segment
        for(const CaptureSegment& s : segments)
        {
            const double offset = e.time - double(s.startMs);
            if(offset >= 0 && offset < double(s.lengthMs))
            {
                e.time = (double(before) + offset) / 1000.0;
                out << e;
                break;
            }
            before += s.lengthMs;
        }
    }
    return out;
}

InputFrame inputFrameAt(const QList<InputEvent>& events, double t)
{
    struct Chip
    {
        QString text;
        double last;
        bool typed;
    };
    QList<Chip> chips;
    InputFrame out;
    for(const InputEvent& e : events)
    {
        if(e.time > t)
            continue;
        if(e.kind == InputEvent::Kind::Click)
        {
            if(t - e.time < kRingLife)
                out.rings.append({e.pos, (t - e.time) / kRingLife});
            continue;
        }
        if(e.typed && !chips.isEmpty() && chips.last().typed && e.time - chips.last().last <= kTypedGap)
        {
            chips.last().text += e.label;
            chips.last().last = e.time;
        }
        else
            chips.append({e.label, e.time, e.typed});
    }
    for(const Chip& c : chips)
        if(c.last >= t - kKeyHold)
            out.keys << chipText(c.text);
    if(out.keys.size() > kMaxChips)
        out.keys = out.keys.mid(out.keys.size() - kMaxChips);
    return out;
}

QList<double> inputFrameTimes(const QList<InputEvent>& events, double start, double end, int ringFps)
{
    QList<double> times{start};
    auto add = [&](double t) {
        if(t > start && t < end)
            times << t;
    };
    for(const InputEvent& e : events)
    {
        add(e.time);
        if(e.kind == InputEvent::Kind::Key)
            add(e.time + kKeyHold);
        else
        {
            for(int k = 1; k < std::ceil(kRingLife * ringFps); ++k)
                add(e.time + double(k) / ringFps);
            add(e.time + kRingLife);
        }
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return b - a < 1e-6; }), times.end());
    return times;
}

QImage renderInputOverlay(const InputFrame& input, QSize frame, TextPosition corner, QPoint origin)
{
    QImage img(frame, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if(input.isEmpty() || frame.isEmpty())
        return img;
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const double px = std::clamp(frame.height() * 0.045, 14.0, 96.0);

    for(const auto& [pos, progress] : input.rings)
    {
        const QPointF c = QPointF(pos - origin);
        const double r = px * (0.45 + 0.8 * progress);
        const int alpha = int(255 * (1.0 - progress));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, alpha / 2), std::max(2.0, px * 0.12) + 2));
        p.drawEllipse(c, r, r);
        p.setPen(QPen(QColor(255, 204, 51, alpha), std::max(2.0, px * 0.12)));
        p.drawEllipse(c, r, r);
    }

    if(!input.keys.isEmpty())
    {
        QFont f;
        f.setPixelSize(int(px));
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int padX = int(px * 0.45), padY = int(px * 0.25), gap = int(px * 0.3);
        const int h = fm.height() + 2 * padY;
        QList<int> widths;
        int total = 0;
        for(const QString& k : input.keys)
        {
            widths << fm.horizontalAdvance(k) + 2 * padX;
            total += widths.last();
        }
        total += gap * (int(input.keys.size()) - 1);
        const QRect row = placeText(corner, frame, QSize(total, h), int(px * 0.8));
        int x = row.left();
        for(int i = 0; i < input.keys.size(); ++i)
        {
            const QRect chip(x, row.top(), widths[i], h);
            p.setPen(QPen(QColor(255, 255, 255, 60), std::max(1.0, px / 24)));
            p.setBrush(QColor(20, 20, 24, 215));
            p.drawRoundedRect(chip, px * 0.3, px * 0.3);
            p.setPen(Qt::white);
            p.drawText(chip, Qt::AlignCenter, input.keys[i]);
            x += widths[i] + gap;
        }
    }
    return img;
}

QString overlaySequenceList(const QStringList& files, const QList<double>& durations)
{
    QString out = QStringLiteral("ffconcat version 1.0\n");
    auto file = [](QString path) {
        path.replace("'", "'\\''");
        return QStringLiteral("file '%1'\n").arg(path);
    };
    for(int i = 0; i < files.size() && i < durations.size(); ++i)
        out += file(files[i]) + QStringLiteral("duration %1\n").arg(durations[i], 0, 'f', 3);
    // The concat demuxer ignores the last duration unless the file is listed again.
    if(!files.isEmpty())
        out += file(files.last());
    return out;
}

QString writeOverlaySequence(const QList<double>& times, double end, const QString& dir,
                             const std::function<InputFrame(double)>& frameAt,
                             const std::function<QImage(const InputFrame&)>& compose, QString* error)
{
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    QStringList files;
    QList<double> durations;
    InputFrame previous;
    for(int i = 0; i < times.size(); ++i)
    {
        const double length = (i + 1 < times.size() ? times[i + 1] : end) - times[i];
        const InputFrame frame = frameAt(times[i]);
        if(!files.isEmpty() && frame == previous)
        {
            durations.last() += length; // nothing changed: hold the previous image longer
            continue;
        }
        const QString file = QStringLiteral("%1/%2.png").arg(dir).arg(files.size(), 5, 10, QLatin1Char('0'));
        if(!compose(frame).save(file))
        {
            if(error)
                *error = "Could not write " + file;
            return {};
        }
        files << file;
        durations << length;
        previous = frame;
    }
    const QString list = dir + "/sequence.txt";
    QFile f(list);
    if(!f.open(QIODevice::WriteOnly) || f.write(overlaySequenceList(files, durations).toUtf8()) < 0)
    {
        if(error)
            *error = "Could not write " + list;
        return {};
    }
    return list;
}

} // namespace qr
