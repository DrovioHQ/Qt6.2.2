/****************************************************************************
**
** Copyright (C) 2021 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
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

#include <QtCore>
#include <QtQml>
#include <QtTest>

class tst_QQC : public QObject
{
    Q_OBJECT
private slots:
    void packaging();
};

void tst_QQC::packaging()
{
    QVERIFY(QFile::exists(":/Test/main.qml"));
    QVERIFY(QFileInfo(":/Test/main.qml").size() > 0);
    QVERIFY(QFile::exists(":/Test/main.cpp"));
    QVERIFY(QFileInfo(":/Test/main.cpp").size() > 0);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl("qrc:/Test/main.qml"));
    QScopedPointer<QObject> obj(component.create());
    QVERIFY(!obj.isNull());
    QCOMPARE(obj->property("success").toInt(), 42);
}

QTEST_MAIN(tst_QQC)

#include "main.moc"
