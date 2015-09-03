#include "JSON.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

//------------------------------------------------------------------------------
QString to_qstring(const QString& str)
{
  return str;
}

//------------------------------------------------------------------------------
QJsonObject parseJson(const QByteArray& msg)
{
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(msg, &error);
  if( doc.isObject() && error.error == QJsonParseError::NoError )
    return doc.object();

  qWarning() << "Failed to parse json: " << error.errorString() << ": " << msg.left(100);
  return QJsonObject();
}

//------------------------------------------------------------------------------
QJsonArray to_json(const QPoint& p)
{
  QJsonArray a;
  a.append(p.x());
  a.append(p.y());
  return a;
}

//------------------------------------------------------------------------------
QJsonArray to_json(const QSize& size)
{
  return to_json(QPoint(size.width(), size.height()));
}

//------------------------------------------------------------------------------
QJsonArray to_json(const QRect& rect)
{
  QJsonArray a;
  a.append(rect.left());
  a.append(rect.top());
  a.append(rect.width());
  a.append(rect.height());
  return a;
}

//------------------------------------------------------------------------------
QJsonValue to_json(const QVariant& var)
{
  return QJsonValue::fromVariant(var);
}

//------------------------------------------------------------------------------
template<>
QString from_json<QString>( const QJsonValue& val,
                            const QString& def )
{
  return val.toString(def);
}

//----------------------------------------------------------------------------
template<> QUrl from_json<QUrl>( const QJsonValue& val,
                                 const QUrl& def )
{
  QUrl url(QUrl::fromPercentEncoding(val.toString().toLocal8Bit()));
  return url.isValid() ? url : def;
}

//------------------------------------------------------------------------------
template<>
quintptr from_json<quintptr>( const QJsonValue& val,
                              const quintptr& def )
{
  bool ok;
  quintptr ret = val.toVariant().toULongLong(&ok);
  return ok ? ret : def;
}

//----------------------------------------------------------------------------
template<>
int32_t from_json<int32_t>( const QJsonValue& val,
                            const int32_t& def )
{
  bool ok;
  int32_t ret = val.toVariant().toInt(&ok);
  return ok ? ret : def;
}

//----------------------------------------------------------------------------
template<>
uint32_t from_json<uint32_t>( const QJsonValue& val,
                              const uint32_t& def )
{
  bool ok;
  uint32_t ret = val.toVariant().toUInt(&ok);
  return ok ? ret : def;
}

//----------------------------------------------------------------------------
template<>
uint64_t from_json<uint64_t>( const QJsonValue& val,
                              const uint64_t& def )
{
  bool ok;
  uint64_t ret = val.toVariant().toULongLong(&ok);
  return ok ? ret : def;
}

//------------------------------------------------------------------------------
template<>
QVariantList from_json<QVariantList>( const QJsonValue& val,
                                      const QVariantList& def )
{
  if( !val.isArray() )
    return def;

  return val.toArray().toVariantList();
}

//------------------------------------------------------------------------------
template<>
QStringList from_json<QStringList>( const QJsonValue& val,
                                    const QStringList& def )
{
  if( !val.isArray() )
    return def;

  QStringList list;
  for(auto entry: val.toArray())
    list << entry.toString();

  return list;
}

//------------------------------------------------------------------------------
template<>
QSet<QString> from_json<QSet<QString>>( const QJsonValue& val,
                                        const QSet<QString>& def )
{
  if( !val.isArray() )
    return def;

  QSet<QString> set;
  for(auto entry: val.toArray())
    set << entry.toString();

  return set;
}

//------------------------------------------------------------------------------
template<>
QColor from_json<QColor>( const QJsonValue& val,
                          const QColor& def )
{
  return from_json<QString>(val, def.name(QColor::HexArgb));
}

//------------------------------------------------------------------------------
template<>
LinksRouting::Color from_json<LinksRouting::Color>( const QJsonValue& val,
                                                    const LinksRouting::Color& def )
{
  QColor color_def;
  color_def.setRgbF(def.r, def.g, def.b, def.a);
  QColor color = from_json<QColor>(val, color_def);
  return {
    static_cast<float>(color.redF()),
    static_cast<float>(color.greenF()),
    static_cast<float>(color.blueF()),
    static_cast<float>(color.alphaF())
  };
}

//------------------------------------------------------------------------------
template<>
QPoint from_json<QPoint>( const QJsonValue& val,
                          const QPoint& def )
{
  if( !val.isArray() )
    return def;

  QJsonArray a = val.toArray();
  if( a.size() != 2 )
    return def;

  return QPoint(a.at(0).toInt(0), a.at(1).toInt(0));
}

//------------------------------------------------------------------------------
template<>
QSize from_json<QSize>( const QJsonValue& val,
                        const QSize& def )
{
  if( !val.isArray() )
    return def;

  QJsonArray a = val.toArray();
  if( a.size() != 2 )
    return def;

  return QSize(a.at(0).toInt(-1), a.at(1).toInt(-1));
}

//------------------------------------------------------------------------------
template<>
QRect from_json<QRect>( const QJsonValue& val,
                        const QRect& def )
{
  if( !val.isArray() )
    return def;

  QJsonArray a = val.toArray();
  if( a.size() != 4 )
    return def;

  return QRect( a.at(0).toInt(-1), a.at(1).toInt(-1),
                a.at(2).toInt(-1), a.at(3).toInt(-1) );
}
