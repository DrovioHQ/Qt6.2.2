/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt Toolkit.
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

#include <gst/gst.h>

#include "qgstreamermessage_p.h"

QT_BEGIN_NAMESPACE

/*!
    \class QGstreamerMessage
    \internal
*/

QGstreamerMessage::QGstreamerMessage(GstMessage* message):
    m_message(message)
{
    gst_message_ref(m_message);
}

QGstreamerMessage::QGstreamerMessage(QGstreamerMessage const& m):
    m_message(m.m_message)
{
    gst_message_ref(m_message);
}

QGstreamerMessage::QGstreamerMessage(const QGstStructure &structure)
{
    gst_structure_get(structure.structure, "message", GST_TYPE_MESSAGE, &m_message, nullptr);
}

QGstreamerMessage::~QGstreamerMessage()
{
    if (m_message != nullptr)
        gst_message_unref(m_message);
}

GstMessage* QGstreamerMessage::rawMessage() const
{
    return m_message;
}

QGstreamerMessage& QGstreamerMessage::operator=(QGstreamerMessage const& rhs)
{
    if (rhs.m_message != m_message) {
        if (rhs.m_message != nullptr)
            gst_message_ref(rhs.m_message);

        if (m_message != nullptr)
            gst_message_unref(m_message);

        m_message = rhs.m_message;
    }

    return *this;
}

QT_END_NAMESPACE
