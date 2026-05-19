#include <QApplication>
#include "connectdialog.h"
#include "vncviewerwidget.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("QtVNC");

    while (true) {
        ConnectDialog dlg;
        if (dlg.exec() != QDialog::Accepted) {
            break;
        }

        VncViewerWidget viewer;
        bool connectionLost = false;

        QObject::connect(&viewer, &VncViewerWidget::connectionLost, &viewer, [&]() {
            connectionLost = true;
            viewer.close();
        });

        viewer.showMaximized();
        viewer.startConnection(dlg.host(), dlg.port(), dlg.password());

        app.exec();

        // If not connectionLost, user closed window intentionally -> exit
        if (!connectionLost) {
            break;
        }
        // Otherwise loop back to ConnectDialog
    }

    return 0;
}
