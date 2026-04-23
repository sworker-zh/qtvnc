#include "connectdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>

ConnectDialog::ConnectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("VNC Connection"));

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("e.g. 192.168.1.100"));

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(5900);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *formLayout = new QFormLayout(this);
    formLayout->addRow(tr("Host:"), m_hostEdit);
    formLayout->addRow(tr("Port:"), m_portSpin);
    formLayout->addRow(tr("Password:"), m_passwordEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    formLayout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (m_hostEdit->text().trimmed().isEmpty()) {
            m_hostEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString ConnectDialog::host() const
{
    return m_hostEdit->text().trimmed();
}

int ConnectDialog::port() const
{
    return m_portSpin->value();
}

QString ConnectDialog::password() const
{
    return m_passwordEdit->text();
}
