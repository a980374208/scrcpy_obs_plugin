#include "pairing-dialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRegularExpressionValidator>
#include <QPushButton>
#include <QMessageBox>
#include <QPointer>
#include <obs-module.h>
#include "adb/adb.h"

PairingDialog::PairingDialog(obs_source_t *source, QWidget *parent)
	: QDialog(parent), source_(obs_source_get_weak_source(source)), task_(this)
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

		task_.start(QString::fromUtf8(sc_adb_get_executable().c_str()), pairAddr, pairCode, connectAddr,
			[this, btnPair, connectAddr](bool success, bool connect_success, const QString &out_err) {
				// QMessageBox runs a nested UI event loop; it may destroy its parent.
				QPointer<PairingDialog> dialog(this);
				// 恢复按钮状态
				btnPair->setEnabled(true);
				btnPair->setText(QString::fromUtf8(obs_module_text("PairDevice")));

				if (success) {
					// 配对成功后保存配置到 settings
					obs_source_t *source = obs_weak_source_get_source(source_);
					if (!source) { reject(); return; }
					if (obs_source_removed(source)) {
						obs_source_release(source);
						reject(); return;
					}
					obs_data_t *settings = obs_source_get_settings(source);
					obs_data_set_string(settings, "pair_info", connectAddr.toUtf8().constData());
					obs_data_release(settings);
					obs_source_release(source);

					if (connect_success) {
						QString info_msg =
							QString::fromUtf8(
								obs_module_text("PairAndConnectSuccess"))
								.arg(connectAddr);
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
					if (dialog) dialog->accept();
				} else {
					QString error_msg = QString::fromUtf8(obs_module_text("PairFailed"))
								    .arg(out_err);
					QMessageBox::critical(this,
							      QString::fromUtf8(obs_module_text("PairTitle")),
							      error_msg);
				}
			}, [this] {
				obs_source_t *source = obs_weak_source_get_source(source_);
				if (!source) return false;
				bool alive = !obs_source_removed(source);
				obs_source_release(source);
				return alive;
			});
	});
}

PairingDialog::~PairingDialog()
{
	task_.cancel();
	obs_weak_source_release(source_);
}

void PairingDialog::done(int result)
{
	task_.cancel(); // reject(), accept() and window close all retire the current run.
	QDialog::done(result);
}
