#ifndef CC_JSON_HPP
#define CC_JSON_HPP

#include "color.h"
#include "linkdescription.h"

#include <QByteArray>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRect>
#include <QSize>
#include <QUrl>
#include <QVariant>

QJsonObject parseJson(const QByteArray& msg);

QJsonArray to_json(const QPoint&);
QJsonArray to_json(const QSize&);
QJsonArray to_json(const QRect&);
QJsonValue to_json(const QVariant&);

template<class V>
QJsonObject to_json(const QMap<QString, V>& map)
{
  QJsonObject obj;
  for(auto it = map.begin(); it != map.end(); ++it)
    obj[it.key()] = to_json(it.value());
  return obj;
}

template<class V>
QJsonObject to_json(const QMap<QUrl, V>& map)
{
  QJsonObject obj;
  for(auto it = map.begin(); it != map.end(); ++it)
    obj[it.key().toString()] = to_json(it.value());
  return obj;
}

template<class V>
QJsonArray to_json(const QMap<QPair<QString, QString>, V>& map)
{
  QJsonArray vec;
  for(auto it = map.begin(); it != map.end(); ++it)
  {
    QJsonObject entry = to_json(it.value());
    entry["nodes"] = QJsonArray::fromStringList({
      it.key().first,
      it.key().second
    });
    vec << entry;
  }
  return vec;
}

template<class V>
QJsonArray to_json(const QSet<V>& set)
{
  QJsonArray array;
  for(auto const& v: set)
    array << to_json(v);
  return array;
}

template<class T> T from_json(const QJsonValue&, const T& def = T());

#endif // CC_JSON_HPP
