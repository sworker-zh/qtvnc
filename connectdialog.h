#ifndef CONNECTDIALOG_H
#define CONNECTDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>

class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectDialog(QWidget *parent = nullptr);

    QString host() const;
    int port() const;
    QString password() const;
    void setHost(const QString &host);

private:
    QLineEdit *m_hostEdit;
    QSpinBox *m_portSpin;
    QLineEdit *m_passwordEdit;
};

#endif // CONNECTDIALOG_H
