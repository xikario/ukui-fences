#pragma once

#include <QRegion>
#include <QVector>

class QWidget;

namespace KWinBlur {

const char *propertyName();
QVector<quint32> encodeRegion(const QRegion &region);
bool isSupported();
bool request(QWidget *widget, const QRegion &region = QRegion());
bool clear(QWidget *widget);
bool hasProperty(const QWidget *widget);

} // namespace KWinBlur
