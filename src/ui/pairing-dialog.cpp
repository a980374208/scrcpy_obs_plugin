#include "pairing-dialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRegularExpressionValidator>
#include <QPushButton>
#include <QMessageBox>
#include <obs-module.h>
#include <thread>
#include <QMetaObject>
#include "srccpy.hpp"
#include "adb/adb.h"

PairingDialog::PairingDialog(scrcpy *bs, QWidget *parent)
	: QDialog(parent), bs_instance(bs)
{
	setWindowTitle(QString::fromUtf8(obs_module_text("PairTitle")));

	QFormLayout *formLayout = new QFormLayout(this);

	edit_pair_addr = new QLineEdit(this);
	edit_pair_addr->setPlaceholderText("192.168.0.1:5555");
	edit_pair_addr->setMaxLength(22);
	QRegularExpression regIpPort("^[0-9\\.:]*$");
	edit_pair_addr->setValidator(new QRegularExpressionValidator(regIpPort, this));

	edit_pair_code = new QLineEdit(this);
	edit_pair_code->setPlaceholderText("123456");
	edit_pair_code->setMaxLength(6);
	QRegularExpression regCode("^[0-9]{0,6}$");
	edit_pair_code->setValidator(new QRegularExpressionValidator(regCode, this));

	edit_connect_addr = new QLineEdit(this);
	edit_connect_addr->setPlaceholderText("192.168.0.1:5555");
	edit_connect_addr->setMaxLength(22);
	edit_connect_addr->setValidator(new QRegularExpressionValidator(regIpPort, this));

	formLayout->addRow(QString::fromUtf8(obs_module_text("PairAddress")), edit_pair_addr);
	formLayout->addRow(QString::fromUtf8(obs_module_text("PairCode")), edit_pair_code);
	formLayout->addRow(QString::fromUtf8(obs_module_text("ConnectAddress")), edit_connect_addr);

	QHBoxLayout *btnLayout = new QHBoxLayout();
	QPushButton *btnPair = new QPushButton(QString::fromUtf8(obs_module_text("PairDevice")), this);
	QPushButton *btnCancel = new QPushButton(QString::fromUtf8(obs_module_text("Cancel")), this);
	btnLayout->addWidget(btnPair);
	btnLayout->addWidget(btnCancel);

	formLayout->addRow(btnLayout);

	connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
	connect(btnPair, &QPushButton::clicked, this, [this, btnPair]() {
		QString pairAddr = edit_pair_addr->text();
		QString pairCode = edit_pair_code->text();
		QString connectAddr = edit_connect_addr->text();

		QRegularExpression regStrict("^(?:[0-9]{1,3}\\.){3}[0-9]{1,3}:[0-9]{1,5}$");

		if (!regStrict.match(pairAddr).hasMatch()) {
			QMessageBox::warning(this, QString::fromUtf8(obs_module_text("FormatError")),
					     QString::fromUtf8(obs_module_text("PairAddrFormatError")));
			edit_pair_addr->setFocus();
			return;
		}
		if (pairCode.length() != 6) {
			QMessageBox::warning(this, QString::fromUtf8(obs_module_text("FormatError")),
					     QString::fromUtf8(obs_module_text("PairCodeFormatError")));
			edit_pair_code->setFocus();
			return;
		}
		if (!connectAddr.isEmpty() && !regStrict.match(connectAddr).hasMatch()) {
			QMessageBox::warning(this, QString::fromUtf8(obs_module_text("FormatError")),
					     QString::fromUtf8(obs_module_text("ConnectAddrFormatError")));
			edit_connect_addr->setFocus();
			return;
		}

		// 禁用按钮，并更新提示文本
		btnPair->setEnabled(false);
		btnPair->setText(QString::fromUtf8(obs_module_text("PairingInProgress")));

		std::string s_pair_addr = pairAddr.toStdString();
		std::string s_pair_code = pairCode.toStdString();
		std::string s_connect_addr = connectAddr.toStdString();

		std::thread([this, s_pair_addr, s_pair_code, s_connect_addr, btnPair]() {
			sc_intr intr;
			std::string out_err;
			bool success = sc_adb_pair(intr, s_pair_addr, s_pair_code, out_err);

			bool connect_success = false;
			if (success && !s_connect_addr.empty()) {
				connect_success = sc_adb_connect(intr, s_connect_addr, 0);
			}

			QMetaObject::invokeMethod(this, [this, success, connect_success, out_err, s_connect_addr, btnPair]() {
				// 恢复按钮状态
				btnPair->setEnabled(true);
				btnPair->setText(QString::fromUtf8(obs_module_text("PairDevice")));

				if (success) {
					// 配对成功后保存配置到 settings
					obs_data_t *settings = obs_source_get_settings(bs_instance->get_source());
					obs_data_set_string(settings, "pair_info", s_connect_addr.c_str());
					obs_data_release(settings);

					if (connect_success) {
						QString info_msg =
							QString::fromUtf8(
								obs_module_text("PairAndConnectSuccess"))
								.arg(QString::fromStdString(s_connect_addr));
						QMessageBox::information(
							this,
							QString::fromUtf8(obs_module_text("PairTitle")),
							info_msg);
					} else {
						QMessageBox::information(
							this,
							QString::fromUtf8(obs_module_text("PairTitle")),
							QString::fromUtf8(
								obs_module_text("PairSuccessNeedConnect")));
					}
					// 仅在成功时关闭对话框
					accept();
				} else {
					QString error_msg = QString::fromUtf8(obs_module_text("PairFailed"))
								    .arg(QString::fromStdString(out_err));
					QMessageBox::critical(this,
							      QString::fromUtf8(obs_module_text("PairTitle")),
							      error_msg);
				}
			}, Qt::QueuedConnection);
		}).detach();
	});
}
