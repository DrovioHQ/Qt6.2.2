/****************************************************************************
**
** Copyright (C) 2017 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com, author Milian Wolff <milian.wolff@kdab.com>
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtWebChannel module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/


#ifndef TESTOBJECT_H
#define TESTOBJECT_H

#include <QObject>
#include <QProperty>
#include <QVariantMap>

QT_BEGIN_NAMESPACE

class TestObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap objectMap READ objectMap CONSTANT)
    Q_PROPERTY(QString stringProperty READ stringProperty WRITE setStringProperty BINDABLE bindableStringProperty)
public:
    explicit TestObject(QObject *parent = Q_NULLPTR);
    ~TestObject();

    QVariantMap objectMap() const;
    QString stringProperty() const;
    QBindable<QString> bindableStringProperty() { return &m_stringProperty; }

public slots:
    void triggerSignals();

    int testOverload(int i);
    QString testOverload(const QString &str);
    QString testOverload(const QString &str, int i);
    int testVariantType(const QVariant &val);
    bool testEmbeddedObjects(const QVariantList &list);
    void setStringProperty(const QString &stringProperty);

signals:
    void testSignalBool(bool testBool);
    void testSignalInt(int testInt);

    void testOverloadSignal(int i);
    void testOverloadSignal(const QString &str);
    void testOverloadSignal(const QString &str, int i);

private:
    QObject *embeddedObject;
    Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(TestObject, QString, m_stringProperty, "foo")
};

QT_END_NAMESPACE

#endif // TESTOBJECT_H
