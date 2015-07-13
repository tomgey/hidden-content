#ifndef CC_JSON_HPP
#define CC_JSON_HPP

#include "color.h"
#include <QByteArray>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QSize>
#include <QVariant>

QJsonObject parseJson(const QByteArray& msg);

QJsonValue to_json(const QPoint&);
QJsonValue to_json(const QSize&);
QJsonValue to_json(const QRect&);

template<class T> T from_json(const QJsonValue&, const T& def = T());
template<> QString from_json<QString>(const QJsonValue&, const QString&);
template<> quintptr from_json<quintptr>(const QJsonValue&, const quintptr&);
template<> int32_t from_json<int32_t>(const QJsonValue&, const int32_t&);
template<> uint32_t from_json<uint32_t>(const QJsonValue&, const uint32_t&);
template<> uint64_t from_json<uint64_t>(const QJsonValue&, const uint64_t&);
template<> QVariantList from_json<QVariantList>(const QJsonValue&, const QVariantList&);
template<> QStringList from_json<QStringList>(const QJsonValue&, const QStringList&);
template<> QColor from_json<QColor>(const QJsonValue&, const QColor&);
template<> LinksRouting::Color from_json<LinksRouting::Color>(const QJsonValue&, const LinksRouting::Color&);
template<> QPoint from_json<QPoint>(const QJsonValue&, const QPoint&);
template<> QSize from_json<QSize>(const QJsonValue&, const QSize&);
template<> QRect from_json<QRect>(const QJsonValue&, const QRect&);

#endif // CC_JSON_HPP
