/****************************************************************************
**
** Copyright (C) 2018 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the documentation of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:FDL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Free Documentation License Usage
** Alternatively, this file may be used under the terms of the GNU Free
** Documentation License version 1.3 as published by the Free Software
** Foundation and appearing in the file included in the packaging of
** this file. Please review the following information to ensure
** the GNU Free Documentation License version 1.3 requirements
** will be met: https://www.gnu.org/licenses/fdl-1.3.html.
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Pane {
    id: root
    implicitWidth: 400
    implicitHeight: 600
    padding: 10

    readonly property color measurementColor: "darkorange"
    readonly property int barLeftMargin: 10
    readonly property int textTopMargin: 12

    Component {
        id: measurementComponent

        Rectangle {
            color: root.measurementColor
            width:  1
            height: parent.height

            Rectangle {
                width: 5
                height: 1
                color: root.measurementColor
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                width: 5
                height: 1
                color: root.measurementColor
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
            }

            Text {
                x: 8
                text: parent.height
                height: parent.height
                color: root.measurementColor
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 20

        ColumnLayout {
            spacing: root.textTopMargin

            Button {
                id: button
                text: qsTr("Button")

                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }

            }
            Text {
                text: "Roboto " + button.font.pixelSize
                color: root.measurementColor
            }
        }

        ColumnLayout {
            spacing: root.textTopMargin

            ItemDelegate {
                id: itemDelegate
                text: qsTr("ItemDelegate")

                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }

            }
            Text {
                text: "Roboto " + itemDelegate.font.pixelSize
                color: root.measurementColor
            }
        }

        ColumnLayout {
            spacing: root.textTopMargin

            CheckDelegate {
                id: checkDelegate
                text: qsTr("CheckDelegate")

                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }

            }
            Text {
                text: "Roboto " + checkDelegate.font.pixelSize
                color: root.measurementColor
            }
        }

        ColumnLayout {
            spacing: root.textTopMargin

            RadioDelegate {
                id: radioDelegate
                text: qsTr("RadioDelegate")

                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }

            }
            Text {
                text: "Roboto " + radioDelegate.font.pixelSize
                color: root.measurementColor
            }
        }

        ColumnLayout {
            spacing: root.textTopMargin

            ComboBox {
                id: comboBox
                model: [ qsTr("ComboBox") ]

                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }

            }
            Text {
                text: "Roboto " + comboBox.font.pixelSize
                color: root.measurementColor
            }
        }

        ColumnLayout {
            spacing: root.textTopMargin

            Item {
                implicitWidth: groupBox.implicitWidth
                implicitHeight: groupBox.implicitHeight

                GroupBox {
                    id: groupBox
                    title: qsTr("GroupBox")
                }
                Loader {
                    sourceComponent: measurementComponent
                    height: parent.height
                    anchors.left: parent.right
                    anchors.leftMargin: root.barLeftMargin
                }
            }
            Text {
                text: "Roboto " + groupBox.font.pixelSize
                color: root.measurementColor
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
