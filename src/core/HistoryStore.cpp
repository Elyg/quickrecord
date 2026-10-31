#include "core/HistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>

namespace qr
{

namespace
{
QJsonObject editsToJson(const EditState& s)
{
    QJsonObject o{
        {"trimStartMs", s.trimStartMs},
        {"trimEndMs", s.trimEndMs},
        {"crop", QJsonArray{s.crop.x(), s.crop.y(), s.crop.width(), s.crop.height()}},
        {"text",
         QJsonObject{
             {"text", s.text.text},
             {"position", toString(s.text.position)},
             {"size", s.text.fontSize},
             {"color", s.text.color.name(QColor::HexArgb)},
             {"background", s.text.background},
         }},
    };
    if(!s.annotations.isEmpty())
    {
        QJsonArray annotations;
        for(const Annotation& a : s.annotations)
            annotations << toJson(a);
        o["annotations"] = annotations;
    }
    return o;
}

EditState editsFromJson(const QJsonObject& o)
{
    EditState s;
    s.trimStartMs = o["trimStartMs"].toInteger();
    s.trimEndMs = o["trimEndMs"].toInteger();
    const QJsonArray c = o["crop"].toArray();
    if(c.size() == 4)
        s.crop = QRect(c[0].toInt(), c[1].toInt(), c[2].toInt(), c[3].toInt());
    const QJsonObject t = o["text"].toObject();
    s.text.text = t["text"].toString();
    s.text.position = textPositionFromString(t["position"].toString());
    s.text.fontSize = t["size"].toInt(s.text.fontSize);
    s.text.color = QColor(t["color"].toString("#ffffffff"));
    s.text.background = t["background"].toBool(true);
    for(const auto& v : o["annotations"].toArray())
        s.annotations << annotationFromJson(v.toObject());
    return s;
}

QJsonObject toJson(const HistoryEntry& e)
{
    QJsonObject o = {
        {"kind", e.kind == EntryKind::Screenshot ? "screenshot" : "recording"},
        {"id", e.id},
        {"created", e.created.toString(Qt::ISODate)},
        {"master", e.master},
        {"thumb", e.thumb},
        {"duration", e.duration},
        {"width", e.size.width()},
        {"height", e.size.height()},
        {"hasAudio", e.hasAudio},
        {"exports", QJsonArray::fromStringList(e.exports)},
    };
    if(e.edits)
        o["edits"] = editsToJson(*e.edits);
    if(e.sheet)
        o["sheet"] = toJson(*e.sheet);
    return o;
}

HistoryEntry fromJson(const QJsonObject& o)
{
    HistoryEntry e;
    e.kind = o["kind"].toString() == "screenshot" ? EntryKind::Screenshot : EntryKind::Recording;
    e.id = o["id"].toString();
    e.created = QDateTime::fromString(o["created"].toString(), Qt::ISODate);
    e.master = o["master"].toString();
    e.thumb = o["thumb"].toString();
    e.duration = o["duration"].toDouble();
    e.size = QSize(o["width"].toInt(), o["height"].toInt());
    e.hasAudio = o["hasAudio"].toBool();
    for(const auto& v : o["exports"].toArray())
        e.exports << v.toString();
    if(o.contains("edits"))
        e.edits = editsFromJson(o["edits"].toObject());
    if(o.contains("sheet"))
        e.sheet = sheetFromJson(o["sheet"].toObject());
    return e;
}

bool hasSuffix(const QString& path, const QString& ext)
{
    return QFileInfo(path).suffix().compare(ext, Qt::CaseInsensitive) == 0;
}
} // namespace

QString entryName(const HistoryEntry& entry)
{
    return QFileInfo(entry.master).completeBaseName();
}

bool hasGeneratedName(const HistoryEntry& entry)
{
    static const QRegularExpression generated(R"(^(qr|qs|qsheet)-\d{8}-\d{6}(-\d+)?$)");
    return generated.match(entryName(entry)).hasMatch();
}

QString editedPath(const HistoryEntry& entry, const QString& ext)
{
    return QStringLiteral("%1/%2-edited.%3").arg(QFileInfo(entry.master).absolutePath(), entryName(entry), ext);
}

QString editedFor(const HistoryEntry& entry, const QString& ext)
{
    for(auto it = entry.exports.rbegin(); it != entry.exports.rend(); ++it)
        if(hasSuffix(*it, ext))
            return QFileInfo::exists(*it) ? *it : QString();
    return {};
}

QString latestEdited(const HistoryEntry& entry)
{
    for(auto it = entry.exports.rbegin(); it != entry.exports.rend(); ++it)
        if(QFileInfo::exists(*it))
            return *it;
    return {};
}

HistoryStore::HistoryStore(QString indexPath)
    : m_indexPath(std::move(indexPath))
{
}

bool HistoryStore::load()
{
    m_entries.clear();
    QFile f(m_indexPath);
    if(!f.exists())
        return true;
    if(!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if(err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    for(const auto& v : doc.object()["recordings"].toArray())
    {
        HistoryEntry e = fromJson(v.toObject());
        if(!e.id.isEmpty())
            m_entries << e;
    }
    return true;
}

bool HistoryStore::save() const
{
    QDir().mkpath(QFileInfo(m_indexPath).absolutePath());
    QJsonArray arr;
    for(const auto& e : m_entries)
        arr << toJson(e);
    QSaveFile f(m_indexPath);
    if(!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"recordings", arr}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

const HistoryEntry* HistoryStore::find(const QString& id) const
{
    for(const auto& e : m_entries)
        if(e.id == id)
            return &e;
    return nullptr;
}

HistoryEntry* HistoryStore::findMutable(const QString& id)
{
    for(auto& e : m_entries)
        if(e.id == id)
            return &e;
    return nullptr;
}

void HistoryStore::add(const HistoryEntry& entry)
{
    if(HistoryEntry* e = findMutable(entry.id))
        *e = entry;
    else
        m_entries.prepend(entry);
}

bool HistoryStore::remove(const QString& id)
{
    return m_entries.removeIf([&](const HistoryEntry& e) { return e.id == id; }) > 0;
}

QString HistoryStore::rename(const QString& id, const QString& name)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return "It is no longer in the history.";

    const QFileInfo master(e->master);
    QString wanted = name.trimmed();
    if(wanted.endsWith("." + master.suffix(), Qt::CaseInsensitive))
        wanted.chop(master.suffix().size() + 1);
    wanted = wanted.trimmed();
    // ":" is how macOS keeps a "/" typed in Finder; a leading "." would hide the file.
    static const QRegularExpression forbidden(R"([/:\x00-\x1f\x7f])");
    if(wanted.isEmpty() || wanted.startsWith('.') || wanted.contains(forbidden))
        return "A name can't be empty, start with a dot, or contain \"/\" or \":\".";
    if(wanted.toUtf8().size() + QByteArrayLiteral("-edited.").size() + master.suffix().size() > 255)
        return "That name is too long.";
    const QString old = master.completeBaseName();
    if(wanted == old)
        return {};

    struct Move
    {
        QString from, to;
    };
    QList<Move> moves{{e->master, master.dir().filePath(wanted + "." + master.suffix())}};
    QStringList exports = e->exports;
    for(QString& path : exports)
    {
        const QFileInfo file(path);
        if(file.completeBaseName() != old + "-edited")
            continue; // not named after it: stays where it is
        const QString to = file.dir().filePath(wanted + "-edited." + file.suffix());
        moves << Move{path, to};
        path = to;
    }
    for(const Move& m : std::as_const(moves))
        if(QFileInfo::exists(m.to) && m.to.compare(m.from, Qt::CaseInsensitive) != 0) // changing only the case
            return QStringLiteral("\"%1\" already exists.").arg(QFileInfo(m.to).fileName());

    for(int i = 0; i < moves.size(); ++i)
        if(!QFile::rename(moves[i].from, moves[i].to))
        {
            for(int undo = i - 1; undo >= 0; --undo)
                QFile::rename(moves[undo].to, moves[undo].from);
            return QStringLiteral("Could not rename \"%1\".").arg(QFileInfo(moves[i].from).fileName());
        }
    e->master = moves.first().to;
    e->exports = exports;
    return {};
}

bool HistoryStore::setEdited(const QString& id, const QString& path)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    const QString ext = QFileInfo(path).suffix();
    e->exports.removeIf([&](const QString& p) { return p == path || hasSuffix(p, ext); });
    e->exports << path;
    return true;
}

bool HistoryStore::removeExport(const QString& path)
{
    bool found = false;
    for(auto& e : m_entries)
        found |= e.exports.removeAll(path) > 0;
    return found;
}

double playLength(const HistoryEntry& entry)
{
    if(!entry.edits || latestEdited(entry).isEmpty())
        return entry.duration;
    const double end = entry.edits->trimEndMs > 0 ? entry.edits->trimEndMs / 1000.0 : entry.duration;
    return std::max(end - entry.edits->trimStartMs / 1000.0, 0.0);
}

bool isVideoFile(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == "mp4" || suffix == "mov";
}

QString importPath(const QString& source, const QString& dir)
{
    const QFileInfo file(source);
    QString name = file.completeBaseName();
    if(name.endsWith("-edited"))
        name += "-copy"; // untrackedVideos() would pass over it
    QString path = QDir(dir).filePath(name + "." + file.suffix());
    for(int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(dir).filePath(QStringLiteral("%1-%2.%3").arg(name).arg(n).arg(file.suffix()));
    return path;
}

bool HistoryStore::pruneMissing(QList<HistoryEntry>* removed)
{
    bool changed = false;
    for(auto it = m_entries.begin(); it != m_entries.end();)
    {
        if(!QFileInfo::exists(it->master))
        {
            if(removed)
                *removed << *it;
            it = m_entries.erase(it);
            changed = true;
            continue;
        }
        changed |= it->exports.removeIf([](const QString& path) { return !QFileInfo::exists(path); }) > 0;
        ++it;
    }
    return changed;
}

QStringList HistoryStore::untrackedVideos(const QString& dir) const
{
    QSet<QString> known;
    for(const HistoryEntry& e : m_entries)
    {
        known << QDir::cleanPath(e.master);
        for(const QString& path : e.exports)
            known << QDir::cleanPath(path);
    }
    QStringList videos;
    for(const QFileInfo& f : QDir(dir).entryInfoList(QDir::Files))
        if(isVideoFile(f.fileName()) && !f.completeBaseName().endsWith("-edited") &&
           !known.contains(QDir::cleanPath(f.absoluteFilePath())))
            videos << QDir::cleanPath(f.absoluteFilePath());
    return videos;
}

bool HistoryStore::setEdits(const QString& id, const EditState& edits)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    if(edits.isDefault())
        e->edits.reset();
    else
        e->edits = edits;
    return true;
}

bool HistoryStore::setSheet(const QString& id, const SheetState& sheet)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    e->sheet = sheet;
    return true;
}

bool HistoryStore::setThumb(const QString& id, const QString& path)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    e->thumb = path;
    return true;
}

} // namespace qr
