#ifndef CC_JSON_HPP
#define CC_JSON_HPP

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QSize>

QJsonObject parseJson(const QByteArray& msg);

QJsonValue to_json(const QPoint&);
QJsonValue to_json(const QSize&);
QJsonValue to_json(const QRect&);

template<class T> T from_json(const QJsonValue&);
template<> QPoint from_json<QPoint>(const QJsonValue& val);
template<> QSize from_json<QSize>(const QJsonValue& val);
template<> QRect from_json<QRect>(const QJsonValue& val);

#endif // CC_JSON_HPP
