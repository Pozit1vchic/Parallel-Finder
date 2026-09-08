// Splash screen (spec section 6): 16px radius, theme colors only.
import QtQuick
import QtQuick.Controls.Basic
import PfUi
import PfUiBridge

Rectangle {
    id: root
    color: Theme.background
    radius: Theme.radiusSplash

    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: L10n.t("app.title")
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTitle
            font.weight: Font.DemiBold
        }

        ProgressBar {
            id: progress
            anchors.horizontalCenter: parent.horizontalCenter
            width: 220
            indeterminate: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: L10n.t("splash.loading")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBody
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        text: "v" + AppInfo.version + "  ·  " + L10n.t("app.stage")
        color: Theme.textDisabled
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
    }
}
