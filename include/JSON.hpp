#ifndef CC_JSON_HPP
#define CC_JSON_HPP

#include "color.h"
#include <QByteArray>
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRect>
#include <QSize>
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
template<> QString from_json<QString>(const QJsonValue&, const QString&);
template<> quintptr from_json<quintptr>(const QJsonValue&, const quintptr&);
template<> int32_t from_json<int32_t>(const QJsonValue&, const int32_t&);
template<> uint32_t from_json<uint32_t>(const QJsonValue&, const uint32_t&);
template<> uint64_t from_json<uint64_t>(const QJsonValue&, const uint64_t&);
template<> QVariantList from_json<QVariantList>(const QJsonValue&, const QVariantList&);
template<> QStringList from_json<QStringList>(const QJsonValue&, const QStringList&);
template<> QSet<QString> from_json<QSet<QString>>(const QJsonValue&, const QSet<QString>&);
template<> QColor from_json<QColor>(const QJsonValue&, const QColor&);
template<> LinksRouting::Color from_json<LinksRouting::Color>(const QJsonValue&, const LinksRouting::Color&);
template<> QPoint from_json<QPoint>(const QJsonValue&, const QPoint&);
template<> QSize from_json<QSize>(const QJsonValue&, const QSize&);
template<> QRect from_json<QRect>(const QJsonValue&, const QRect&);

#endif // CC_JSON_HPP
