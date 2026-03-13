#include "settingsdialog.h"
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QComboBox>
#include <QStandardPaths>
// #include <QRegExp>
#include <QRegularExpressionValidator>

SettingsDialog::SettingsDialog(const AppConfig& config, QWidget *parent) 
    : QDialog(parent), m_config(config) {
	// 定义正则：匹配【不包含】空格和各种 shell 危险符号的字符串
	// ^ 开始, $ 结束, [^...] 排除括号里的字符
	QRegularExpression safeRegex("^[^\\s'\"\\$\\(\\)\\`&|<>;\\\\]+$");

	// 创建验证器
	m_safeValidator = new QRegularExpressionValidator(safeRegex, this);

    setWindowTitle(tr("Pigeonpiper Configuration"));
    resize(800, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    QWidget *scrollContent = new QWidget(scrollArea);
    QVBoxLayout *contentLayout = new QVBoxLayout(scrollContent);

    // ================= 1. 全局配置 =================
    QGroupBox *globalGroup = new QGroupBox(tr("Global Configuration"));
    QGridLayout *globalLayout = new QGridLayout(globalGroup);
    
    targetPathEdit = new QLineEdit(m_config.targetPath);
	targetPathEdit->setPlaceholderText("user@host:/path/to/dir");
	targetPathEdit->setValidator(m_safeValidator);
	targetPathEdit->setToolTip(tr("Target host (e.g., user@192.168.1.1:/path/to/dir)\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    QPushButton *btnTestTarget = new QPushButton(tr("Test"));
    globalLayout->addWidget(new QLabel(tr("Target Path:")), 0, 0);
    globalLayout->addWidget(targetPathEdit, 0, 1);
    globalLayout->addWidget(btnTestTarget, 0, 2);

    archiveFmtEdit = new QLineEdit(m_config.archiveFormat);
	archiveFmtEdit->setValidator(m_safeValidator);
	archiveFmtEdit->setToolTip(tr("Archive file name without extension\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
	QPushButton *btnHelpDate = new QPushButton("");
	btnHelpDate->setIcon(style()->standardIcon(QStyle::SP_MessageBoxQuestion));
	btnHelpDate->setToolTip(tr("Show Date Placeholders"));
    globalLayout->addWidget(new QLabel(tr("Archive Name:")), 1, 0);
    globalLayout->addWidget(archiveFmtEdit, 1, 1);
	globalLayout->addWidget(btnHelpDate, 1, 2);

	comboLanguage = new QComboBox();
	comboLanguage->addItem("简体中文", "zh_CN");
	comboLanguage->addItem("English", "en");
	
	int langIndex = comboLanguage->findData(m_config.language);
	if (langIndex != -1) comboLanguage->setCurrentIndex(langIndex);
	
	globalLayout->addWidget(new QLabel(tr("Language (need restart):")), 2, 0);
	globalLayout->addWidget(comboLanguage, 2, 1);
	
	// --- 新增：自定义 rsync 路径 ---
    customRsyncEdit = new QLineEdit(m_config.customRsyncPath);
    customRsyncEdit->setPlaceholderText(tr("Leave empty to use system rsync"));
    customRsyncEdit->setToolTip(tr("Absolute path to custom rsync executable.\nLeave empty to use system default."));
    QPushButton *btnCheckEnv = new QPushButton(tr("Check Env Dependencies"));
    globalLayout->addWidget(new QLabel(tr("Custom rsync path:")), 3, 0);
    globalLayout->addWidget(customRsyncEdit, 3, 1);
	globalLayout->addWidget(btnCheckEnv, 3, 2);
	
    contentLayout->addWidget(globalGroup);

    connect(btnTestTarget, &QPushButton::clicked, this, &SettingsDialog::testTargetReachability);
    connect(btnCheckEnv, &QPushButton::clicked, this, &SettingsDialog::checkEnvDependencies);
	connect(btnHelpDate, &QPushButton::clicked, this, [this]() {
		QMessageBox::information(this, tr("Date Placeholders Help"),
			tr("Supported date/time placeholders:\n\n"
			   "%Y - Year (e.g., 2026)\n"
			   "%m - Month (01-12)\n"
			   "%d - Day (01-31)\n"
			   "%H - Hour (00-23)\n"
			   "%M - Minute (00-59)\n"
			   "%S - Second (00-59)\n\n"));
	});

    // ================= 2. 转换工具配置 =================
    QGroupBox *converterGroup = new QGroupBox(tr("Converter Rules"));
    QVBoxLayout *converterMainLayout = new QVBoxLayout(converterGroup);
	
    rulesLayout = new QVBoxLayout();
    converterMainLayout->addLayout(rulesLayout);
	
    QPushButton *btnAddRule = new QPushButton(tr("Add Rule"));
    converterMainLayout->addWidget(btnAddRule);
    
    contentLayout->addWidget(converterGroup);

    connect(btnAddRule, &QPushButton::clicked, [this]() { addRuleRow(); });
    for (const auto& rule : m_config.rules) {
        addRuleRow(rule.ext, rule.cmd);
    }

    // ================= 3. 源服务器配置 =================
    QGroupBox *serverGroup = new QGroupBox(tr("Source Servers"));
    QVBoxLayout *serverMainLayout = new QVBoxLayout(serverGroup);

    serversLayout = new QVBoxLayout();
    serverMainLayout->addLayout(serversLayout);
	
    QPushButton *btnAddServer = new QPushButton(tr("Add Server"));
    serverMainLayout->addWidget(btnAddServer);
	
    contentLayout->addWidget(serverGroup);

    connect(btnAddServer, &QPushButton::clicked, [this]() { addServerBox(); });
    for (const auto& srv : m_config.servers) {
        addServerBox(srv);
    }

    // ================= 底部按钮 =================
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton(tr("Save"));
	btnSave->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    QPushButton *btnCancel = new QPushButton(tr("Cancel"));
	btnCancel->setIcon(style()->standardIcon(QStyle::SP_DialogCancelButton));
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnSave);
    bottomLayout->addWidget(btnCancel);
    mainLayout->addLayout(bottomLayout);

    connect(btnSave, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

void SettingsDialog::addRuleRow(const QString& ext, const QString& cmd) {
    QWidget *rowWidget = new QWidget(this);
    QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0, 0, 0, 0);

    QLineEdit *extEdit = new QLineEdit(ext);
    extEdit->setPlaceholderText(".ext");
	extEdit->setValidator(m_safeValidator);
	extEdit->setToolTip(tr("File extension (e.g., .ext)\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    QLineEdit *cmdEdit = new QLineEdit(cmd);
    cmdEdit->setPlaceholderText("/path/to/cmd");
	cmdEdit->setValidator(m_safeValidator);
	cmdEdit->setToolTip(tr("Command path without arguments\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    
    QPushButton *btnCheck = new QPushButton(tr("Check Command"));
    QPushButton *btnDel = new QPushButton(tr("Delete"));

    rowLayout->addWidget(extEdit, 1);
    rowLayout->addWidget(cmdEdit, 3);
    rowLayout->addWidget(btnCheck);
    rowLayout->addWidget(btnDel);

    connect(btnCheck, &QPushButton::clicked, [this, cmdEdit]() {
        QString cmdStr = cmdEdit->text().split(" ").first();
        QString path = QStandardPaths::findExecutable(cmdStr);
        if (path.isEmpty()) QMessageBox::warning(this, tr("Check"), QString(tr("Command '%1' NOT found in PATH.")).arg(cmdStr));
        else QMessageBox::information(this, tr("Check"), QString(tr("Command found at:\n%1")).arg(path));
    });

    connect(btnDel, &QPushButton::clicked, [rowWidget]() {
        rowWidget->deleteLater();
    });

    rulesLayout->addWidget(rowWidget);
}

void SettingsDialog::addServerBox(const ServerConfig& srv) {
    QGroupBox *srvBox = new QGroupBox();
    QVBoxLayout *srvLayout = new QVBoxLayout(srvBox);

    // 服务器顶栏
    QWidget *topWidget = new QWidget();
    QHBoxLayout *topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);

    QLineEdit *hostEdit = new QLineEdit(srv.host);
    hostEdit->setPlaceholderText("user@host");
	hostEdit->setValidator(m_safeValidator);
	hostEdit->setToolTip(tr("Remote host (e.g., user@192.168.1.1)\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    QPushButton *btnTestHost = new QPushButton(tr("Test"));
    QPushButton *btnAddPath = new QPushButton(tr("Add Path"));
    QPushButton *btnDelServer = new QPushButton(tr("Delete Server"));

    topLayout->addWidget(new QLabel(tr("Host:")));
    topLayout->addWidget(hostEdit, 1);
    topLayout->addWidget(btnTestHost);
    topLayout->addWidget(btnAddPath);
    topLayout->addWidget(btnDelServer);
    srvLayout->addWidget(topWidget);

    // 路径列表
    QVBoxLayout *pathsLayout = new QVBoxLayout();
    srvLayout->addLayout(pathsLayout);

    connect(btnTestHost, &QPushButton::clicked, [this, hostEdit]() {
        QString host = hostEdit->text();
        if(host.isEmpty()) return;
        int ret = QProcess::execute("ssh", {"-o", "BatchMode=yes", "-o", "ConnectTimeout=5", host, "exit"});
        if (ret == 0) QMessageBox::information(this, tr("Test"), tr("Server is reachable via SSH."));
        else QMessageBox::critical(this, tr("Test"), tr("Failed to connect to server."));
    });

    connect(btnAddPath, &QPushButton::clicked, [this, pathsLayout, hostEdit]() {
        addPathRow(pathsLayout, hostEdit->text());
    });

    connect(btnDelServer, &QPushButton::clicked, [srvBox]() {
        srvBox->deleteLater();
    });
	
    for (const auto& path : srv.paths) {
        addPathRow(pathsLayout, srv.host, path.label, path.path);
    }

    serversLayout->addWidget(srvBox);
}

void SettingsDialog::addPathRow(QVBoxLayout* pathsLayout, const QString& hostEditStr, const QString& label, const QString& path) {
    QWidget *rowWidget = new QWidget();
    QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(20, 0, 0, 0); // 缩进表示层级

    QLineEdit *labelEdit = new QLineEdit(label);
    labelEdit->setPlaceholderText(tr("Path Label"));
	labelEdit->setValidator(m_safeValidator);
	labelEdit->setToolTip(tr("A unique label for this path\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    QLineEdit *pathEdit = new QLineEdit(path);
    pathEdit->setPlaceholderText("/path/to/dir");
	pathEdit->setValidator(m_safeValidator);
	pathEdit->setToolTip(tr("Full path on the remote server\n"
						"Input cannot contain spaces or special shell characters:\n"
						"' \" $ ( ) ` & | < > ; \\"));
    
    QPushButton *btnTestPath = new QPushButton(tr("Check Path"));
    QPushButton *btnDelPath = new QPushButton(tr("Delete Path"));

    rowLayout->addWidget(labelEdit, 1);
    rowLayout->addWidget(pathEdit, 2);
    rowLayout->addWidget(btnTestPath);
    rowLayout->addWidget(btnDelPath);

    connect(btnTestPath, &QPushButton::clicked, [this, hostEditStr, pathEdit]() {
        if(hostEditStr.isEmpty() || pathEdit->text().isEmpty()) return;
        int ret = QProcess::execute("ssh", {"-o", "BatchMode=yes", "-o", "ConnectTimeout=5", hostEditStr, "ls", "-d", pathEdit->text()});
        if (ret == 0) QMessageBox::information(this, tr("Test"), tr("Path exists and is accessible."));
        else QMessageBox::critical(this, tr("Test"), tr("Path not found or permission denied."));
    });

    connect(btnDelPath, &QPushButton::clicked, [rowWidget]() {
        rowWidget->deleteLater();
    });
	
    pathsLayout->addWidget(rowWidget);
}

void SettingsDialog::checkEnvDependencies() {
    // int rsyncOk = QProcess::execute("rsync", {"--version"});
    // int tarOk = QProcess::execute("tar", {"--version"});
    // QString msg = QString(tr("Dependencies Check:\n\nrsync: %1\ntar: %2"))
                    // .arg(rsyncOk == 0 ? tr("Installed") : tr("Missing or Error"))
                    // .arg(tarOk == 0 ? tr("Installed") : tr("Missing or Error"));
    // QMessageBox::information(this, tr("Environment Check"), msg);
	
	QString rsyncCmd = customRsyncEdit->text().trimmed();
    if (rsyncCmd.isEmpty()) {
        rsyncCmd = "rsync";
    }
	
	// 内部 Lambda 函数：用于查询版本
    auto getVersion = [](const QString& cmd) -> QString {
        QProcess proc;
        proc.start(cmd, {"--version"});
        // 设置 1 秒超时，避免挂死
        if (!proc.waitForFinished(1000) || proc.exitCode() != 0) {
            return ""; // 查询失败显示“失败”
        }
        
        QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        // 正则匹配类似 3.1.2 或 1.26 的数字点格式
        QRegularExpression re("(\\d+\\.\\d+(\\.\\d+)?)");
        QRegularExpressionMatch match = re.match(output);
        
        return match.hasMatch() ? match.captured(1) : "";
    };

    QString rsyncVer = getVersion(rsyncCmd);
    QString tarVer = getVersion("tar");
	QString rsyncDisplay = rsyncVer.isEmpty() ? tr("Failed") : rsyncVer;
    QString tarDisplay = tarVer.isEmpty() ? tr("Failed") : tarVer;

    QString msg = QString(tr("Dependencies Check:\n\nrsync (%1): %2\ntar: %3"))
                    .arg(rsyncCmd == "rsync" ? tr("System") : tr("Custom"))
                    .arg(rsyncDisplay)
                    .arg(tarDisplay);
    
    QMessageBox::information(this, tr("Environment Check"), msg);
}

void SettingsDialog::testTargetReachability() {
    QString target = targetPathEdit->text();
    if(target.isEmpty()) return;
    
    // 如果包含 ":"，认为是远程路径，使用 ssh 测试；否则认为是本地路径
    if (target.contains(":")) {
        QStringList parts = target.split(":");
        int ret = QProcess::execute("ssh", {"-o", "BatchMode=yes", "-o", "ConnectTimeout=5", parts[0], "mkdir", "-p", parts[1]});
        if (ret == 0) QMessageBox::information(this, tr("Test"), tr("Remote target path is reachable and writable."));
        else QMessageBox::critical(this, tr("Test"), tr("Remote target path test failed."));
    } else {
        int ret = QProcess::execute("mkdir", {"-p", target});
        if (ret == 0) QMessageBox::information(this, tr("Test"), tr("Local target path is valid."));
        else QMessageBox::critical(this, tr("Test"), tr("Cannot create local target path."));
    }
}

void SettingsDialog::onSave() {
    m_config.targetPath = targetPathEdit->text();
    m_config.archiveFormat = archiveFmtEdit->text();
	m_config.language = comboLanguage->currentData().toString();
	m_config.customRsyncPath = customRsyncEdit->text().trimmed();
    m_config.rules.clear();
    m_config.servers.clear();

    // 遍历抓取 Rules
    for (int i = 0; i < rulesLayout->count(); ++i) {
        QWidget *rowWidget = rulesLayout->itemAt(i)->widget();
        if (rowWidget) {
            QList<QLineEdit*> edits = rowWidget->findChildren<QLineEdit*>();
            if (edits.size() >= 2) {
                QString ext = edits[0]->text().trimmed();
                QString cmd = edits[1]->text().trimmed();
                if (!ext.isEmpty() && !cmd.isEmpty()) {
                    m_config.rules.append({ext, cmd});
                }
            }
        }
    }

    // 遍历抓取 Servers 和 Paths
    for (int i = 0; i < serversLayout->count(); ++i) {
        QGroupBox *srvBox = qobject_cast<QGroupBox*>(serversLayout->itemAt(i)->widget());
        if (srvBox) {
            ServerConfig sc;
            // host在第一个QWidget（顶栏）的QLineEdit中
            QList<QLineEdit*> srvEdits = srvBox->findChildren<QLineEdit*>();
            if (!srvEdits.isEmpty()) {
                sc.host = srvEdits.first()->text().trimmed();
                if (sc.host.isEmpty()) continue;
            }

            // 寻找Paths（跳过第一个表示host的QLineEdit）
            // 因为paths结构固定：labelEdit 是找到的奇数索引，pathEdit是偶数索引（排除第一个）
            for (int j = 1; j < srvEdits.size(); j += 2) {
                if (j + 1 < srvEdits.size()) {
                    QString label = srvEdits[j]->text().trimmed();
                    QString path = srvEdits[j+1]->text().trimmed();
                    if (!label.isEmpty() && !path.isEmpty()) {
                        sc.paths.append({label, path});
                    }
                }
            }
            m_config.servers.append(sc);
        }
    }

    accept();
}

AppConfig SettingsDialog::getConfig() const {
    return m_config;
}