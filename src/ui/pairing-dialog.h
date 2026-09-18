#pragma once
#include <QDialog>
#include <QLineEdit>
#include <obs.h>
#include "pairing-task.h"


class PairingDialog : public QDialog {
public:
	QLineEdit *edit_pair_addr;
	QLineEdit *edit_pair_code;
	QLineEdit *edit_connect_addr;


	PairingDialog(obs_source_t *source, QWidget *parent = nullptr);
	~PairingDialog() override;
	void done(int result) override;
private:
	obs_weak_source_t *source_;
	PairingTask task_;

};
