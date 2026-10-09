#include "core/HistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qr
{

namespace
{
QJsonObject editsToJson(const EditState& s)
{
    return {
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
    return e;
}
} // namespace

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
    remove(entry.id);
    m_entries.prepend(entry);
}

bool HistoryStore::remove(const QString& id)
{
    return m_entries.removeIf([&](const HistoryEntry& e) { return e.id == id; }) > 0;
}

bool HistoryStore::addExport(const QString& id, const QString& path)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    e->exports.removeAll(path);
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

bool HistoryStore::setThumb(const QString& id, const QString& path)
{
    HistoryEntry* e = findMutable(id);
    if(!e)
        return false;
    e->thumb = path;
    return true;
}

} // namespace qr
