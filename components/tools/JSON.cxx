#include "JSON.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>

QJsonObject parseJson(const QByteArray& msg)
{
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(msg, &error);
  if( doc.isObject() && error.error == QJsonParseError::NoError )
    return doc.object();

  qWarning() << "Failed to parse json: " << error.errorString() << ": " << msg.left(100);
  return QJsonObject();
}

QJsonValue to_json(const QPoint& p)
{
  QJsonArray a;
  a.append(p.x());
  a.append(p.y());
  return a;
}

QJsonValue to_json(const QSize& size)
{
  return to_json(QPoint(size.width(), size.height()));
}

QJsonValue to_json(const QRect& rect)
{
  QJsonArray a;
  a.append(rect.left());
  a.append(rect.top());
  a.append(rect.width());
  a.append(rect.height());
  return a;
}

template<>
QPoint from_json<QPoint>(const QJsonValue& val)
{
  if( !val.isArray() )
    return QPoint();

  QJsonArray a = val.toArray();
  if( a.size() != 2 )
    return QPoint();

  return QPoint(a.at(0).toInt(0), a.at(1).toInt(0));
}

template<>
QSize from_json<QSize>(const QJsonValue& val)
{
  if( !val.isArray() )
    return QSize();

  QJsonArray a = val.toArray();
  if( a.size() != 2 )
    return QSize();

  return QSize(a.at(0).toInt(-1), a.at(1).toInt(-1));
}

template<>
QRect from_json<QRect>(const QJsonValue& val)
{
  if( !val.isArray() )
    return QRect();

  QJsonArray a = val.toArray();
  if( a.size() != 4 )
    return QRect();

  return QRect( a.at(0).toInt(-1), a.at(1).toInt(-1),
                a.at(2).toInt(-1), a.at(3).toInt(-1) );
}
