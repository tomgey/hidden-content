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
QJsonArray parseJsonArray(const QByteArray& msg)
{
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(msg, &error);
  if( doc.isArray() && error.error == QJsonParseError::NoError )
    return doc.array();

  qWarning() << "Failed to parse json: " << error.errorString() << error.offset << ": " << msg.left(200);
  return QJsonArray();
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
  if( val.isUndefined() )
    return def;

  return val.toVariant().toString();
}

//------------------------------------------------------------------------------
template<> QUrl from_json<QUrl>( const QJsonValue& val,
                                 const QUrl& def )
{
  QUrl url(QUrl::fromPercentEncoding(val.toString().toLocal8Bit()));
  return url.isValid() ? url : def;
}

//------------------------------------------------------------------------------
template<> bool from_json<bool>( const QJsonValue& val,
                                 const bool& def )
{
  QVariant var = val.toVariant();
  if( !var.isValid() )
    return def;

  return var.toBool();
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
PropertyObjectMap from_json<PropertyObjectMap>( const QJsonValue& val,
                                                const PropertyObjectMap& def)
{
  if( !val.isObject() )
    return def;

  PropertyObjectMap map;
  QJsonObject obj = val.toObject();

  for(auto it = obj.begin(); it != obj.end(); ++it)
    map[it.key()] = it.value().toObject().toVariantMap();

  return map;
}

//------------------------------------------------------------------------------
template<>
QColor from_json<QColor>( const QJsonValue& val,
                          const QColor& def )
{
  QString color_str = from_json<QString>(val);
  if( color_str.isEmpty() )
    return def;

  return QColor(color_str);
}

//------------------------------------------------------------------------------
template<>
QPointF from_json<QPointF>( const QJsonValue& val,
                            const QPointF& def )
{
  if( !val.isArray() )
    return def;

  QJsonArray a = val.toArray();
  if( a.size() != 2 )
    return def;

  return QPointF( a.at(0).toDouble(std::nan("")),
                  a.at(1).toDouble(std::nan("")) );
}

//------------------------------------------------------------------------------
template<>
QPoint from_json<QPoint>( const QJsonValue& val,
                          const QPoint& def )
{
  return from_json<QPointF>(val, def).toPoint();
}

//------------------------------------------------------------------------------
template<>
float2 from_json<float2>( const QJsonValue& val,
                          const float2& def )
{
  return from_json<QPointF>(val, def.toQPointF());
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
