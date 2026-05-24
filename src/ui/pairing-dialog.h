#pragma once
#include <QDialog>
#include <QLineEdit>

class scrcpy;

class PairingDialog : public QDialog {
public:
	QLineEdit *edit_pair_addr;
	QLineEdit *edit_pair_code;
	QLineEdit *edit_connect_addr;
	scrcpy *bs_instance;

	PairingDialog(scrcpy *bs, QWidget *parent = nullptr);
};
