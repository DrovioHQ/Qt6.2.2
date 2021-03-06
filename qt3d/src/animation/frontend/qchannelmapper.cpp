/****************************************************************************
**
** Copyright (C) 2017 Klaralvdalens Datakonsult AB (KDAB).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qchannelmapper.h"
#include "qchannelmapper_p.h"
#include <Qt3DAnimation/qchannelmapping.h>

QT_BEGIN_NAMESPACE

namespace Qt3DAnimation {

QChannelMapperPrivate::QChannelMapperPrivate()
    : Qt3DCore::QNodePrivate()
{
}

/*!
    \class Qt3DAnimation::QChannelMapper
    \inmodule Qt3DAnimation
    \brief Allows to map the channels within the clip onto properties of
    objects in the application.

*/
QChannelMapper::QChannelMapper(Qt3DCore::QNode *parent)
    : Qt3DCore::QNode(*new QChannelMapperPrivate, parent)
{
}

QChannelMapper::QChannelMapper(QChannelMapperPrivate &dd, Qt3DCore::QNode *parent)
    : Qt3DCore::QNode(dd, parent)
{
}

QChannelMapper::~QChannelMapper()
{
}

void QChannelMapper::addMapping(QAbstractChannelMapping *mapping)
{
    Q_ASSERT(mapping);
    Q_D(QChannelMapper);
    if (!d->m_mappings.contains(mapping)) {
        d->m_mappings.append(mapping);

        // Ensures proper bookkeeping
        d->registerDestructionHelper(mapping, &QChannelMapper::removeMapping, d->m_mappings);

        // We need to add it as a child of the current node if it has been declared inline
        // Or not previously added as a child of the current node so that
        // 1) The backend gets notified about it's creation
        // 2) When the current node is destroyed, it gets destroyed as well
        if (!mapping->parent())
            mapping->setParent(this);

        d->update();
    }
}

void QChannelMapper::removeMapping(QAbstractChannelMapping *mapping)
{
    Q_ASSERT(mapping);
    Q_D(QChannelMapper);
    if (!d->m_mappings.removeOne(mapping))
        return;
    d->update();
    // Remove bookkeeping connection
    d->unregisterDestructionHelper(mapping);
}

QList<QAbstractChannelMapping *> QChannelMapper::mappings() const
{
    Q_D(const QChannelMapper);
    return d->m_mappings;
}

} // namespace Qt3DAnimation

QT_END_NAMESPACE
