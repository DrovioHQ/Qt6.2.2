/****************************************************************************
**
** Copyright (C) 2017 Denis Shienkov <denis.shienkov@gmail.com>
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtSerialBus module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef PEAKCANBACKEND_H
#define PEAKCANBACKEND_H

#include <QtSerialBus/qcanbusdevice.h>
#include <QtSerialBus/qcanbusdeviceinfo.h>
#include <QtSerialBus/qcanbusframe.h>

#include <QtCore/qlist.h>
#include <QtCore/qvariant.h>

QT_BEGIN_NAMESPACE

class PeakCanBackendPrivate;

class PeakCanBackend : public QCanBusDevice
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(PeakCanBackend)
    Q_DISABLE_COPY(PeakCanBackend)
public:
    explicit PeakCanBackend(const QString &name, QObject *parent = nullptr);
    ~PeakCanBackend();

    bool open() override;
    void close() override;

    void setConfigurationParameter(ConfigurationKey key, const QVariant &value) override;

    bool writeFrame(const QCanBusFrame &newData) override;

    QString interpretErrorFrame(const QCanBusFrame &errorFrame) override;

    static bool canCreate(QString *errorReason);
    static QList<QCanBusDeviceInfo> interfaces();

    void resetController() override;
    bool hasBusStatus() const override;
    CanBusStatus busStatus() override;
    QCanBusDeviceInfo deviceInfo() const override;

private:
    enum class Availability {
        Available = 1U, // matches PCAN_CHANNEL_AVAILABLE
        Occupied  = 2U  // matches PCAN_CHANNEL_OCCUPIED
    };
    static QList<QCanBusDeviceInfo> interfacesByChannelCondition(Availability available);
    static QList<QCanBusDeviceInfo> interfacesByAttachedChannels(Availability available, bool *ok);
    static QList<QCanBusDeviceInfo> attachedInterfaces(Availability available);

    PeakCanBackendPrivate * const d_ptr;
};

QT_END_NAMESPACE

#endif // PEAKCANBACKEND_H
