/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
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

#include "resourceaccessor_p.h"

#include <Qt3DRender/qrendertargetoutput.h>

#include <private/qrendertargetoutput_p.h>
#include <private/nodemanagers_p.h>
#include <private/rendertargetoutput_p.h>
#include <private/managers_p.h>

#include <QtCore/qmutex.h>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {
namespace Render {

RenderBackendResourceAccessor::~RenderBackendResourceAccessor()
{

}

ResourceAccessor::ResourceAccessor(AbstractRenderer *renderer, NodeManagers *mgr)
    : m_renderer(renderer)
    , m_textureManager(mgr->textureManager())
    , m_attachmentManager(mgr->attachmentManager())
    , m_entityManager(mgr->renderNodesManager())
{

}

// called by render plugins from arbitrary thread
bool ResourceAccessor::accessResource(ResourceType type,
                                      Qt3DCore::QNodeId nodeId,
                                      void **handle,
                                      QMutex **lock)
{
    switch (type) {

    // This is purely made so that Scene2D works, this should be completely
    // redesigned to avoid introducing this kind of coupling and reliance on
    // OpenGL
    case RenderBackendResourceAccessor::OGLTextureWrite:
        Q_FALLTHROUGH();
    case RenderBackendResourceAccessor::OGLTextureRead:
    {
        if (m_renderer->api() != API::OpenGL) {
            qWarning() << "Renderer plugin is not compatible with Scene2D";
            return false;
        }
        return m_renderer->accessOpenGLTexture(nodeId,
                                               reinterpret_cast<QOpenGLTexture **>(handle),
                                               lock,
                                               type == RenderBackendResourceAccessor::OGLTextureRead);
    }

    case RenderBackendResourceAccessor::OutputAttachment: {
        RenderTargetOutput *output = m_attachmentManager->lookupResource(nodeId);
        if (output) {
            Attachment **attachmentData = reinterpret_cast<Attachment **>(handle);
            *attachmentData = output->attachment();
            return true;
        }
        break;
    }

    case RenderBackendResourceAccessor::EntityHandle: {
        Entity *entity = m_entityManager->lookupResource(nodeId);
        if (entity) {
            Entity **pEntity = reinterpret_cast<Entity **>(handle);
            *pEntity = entity;
            return true;
        }
        break;
    }

    default:
        break;
    }
    return false;
}

} // namespace Render
} // namespace Qt3DRender

QT_END_NAMESPACE
