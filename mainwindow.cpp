#include "mainwindow.h"
#include "settingsdialog.h"
#include <QMenuBar>
#include <QTabWidget>
#include <QHeaderView>
#include <QDebug>
#include <QApplication>
#include <QMessageBox>
#include <QLocale>
// #include <QRegExp>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_transferMgr(new TransferManager(this)) {
    m_config = ConfigManager::loadConfig();
    setupUi();
    createMenus();
	checkRsyncVersion();
    loadServerTree();
    
    connect(m_transferMgr, &TransferManager::taskUpdated, this, &MainWindow::refreshTaskUi);
    connect(m_transferMgr, &TransferManager::globalProgress, [this](int p, const QString& s) {
        overallProgress->setValue(p);
        overallStatus->setText(s);
    });
}

MainWindow::~MainWindow() {}

QString MainWindow::formatSize(qint64 bytes) {
    if (bytes < 0) return "";
    double size = bytes;
    int i = 0;
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    while (size >= 1024.0 && i < 4) {
        size /= 1024.0;
        i++;
    }
    return i == 0 ? QString::number(bytes) + " " + units[i] : QString::number(size, 'f', 2) + " " + units[i];
}

void MainWindow::setupUi() {
    resize(1000, 600);
    QTabWidget *tabWidget = new QTabWidget(this);
    setCentralWidget(tabWidget);

    // Tab 1: Browse Files
    // QSplitter *browseSplitter = new QSplitter(Qt::Horizontal);
	browseSplitter = new QSplitter(Qt::Horizontal);
    serverTree = new QTreeWidget();
    serverTree->setHeaderLabel(tr("Servers"));
    connect(serverTree, &QTreeWidget::itemClicked, this, &MainWindow::onTreeItemClicked);

    QWidget *rightWidget = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);

    // Toolbar
    QHBoxLayout *toolbar = new QHBoxLayout();
    QPushButton *btnAdd = new QPushButton(tr("Add to Task"));
    // QPushButton *btnRefresh = new QPushButton("Refresh");
	QPushButton *btnRefresh = new QPushButton("");
	btnRefresh->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
	btnRefresh->setToolTip(tr("Refresh"));
    activityIndicator = new QLabel(tr("Idle"));
    activityIndicator->setFixedWidth(100);
    activityIndicator->setAlignment(Qt::AlignCenter);
    
    breadcrumbWidget = new QWidget();
    breadcrumbLayout = new QHBoxLayout(breadcrumbWidget);
	breadcrumbLayout->setSpacing(0);
    breadcrumbLayout->setContentsMargins(0,0,0,0);
    
    toolbar->addWidget(btnAdd);
    toolbar->addWidget(btnRefresh);
    toolbar->addWidget(activityIndicator);
    toolbar->addWidget(breadcrumbWidget);
    toolbar->addStretch();
    
    // File Tree View
    fileTreeView = new QTreeView();
    fileModel = new QStandardItemModel(0, 4, this);
    fileModel->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Type"), tr("ModTime")});
	
	fileSortModel = new FileSortProxyModel(this);
    fileSortModel->setSourceModel(fileModel);
    fileTreeView->setModel(fileSortModel); // 绑定 ProxyModel
    // fileTreeView->setModel(fileModel);
    fileTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers); // 禁止直接编辑文本
    fileTreeView->setAlternatingRowColors(true);
	fileTreeView->setSortingEnabled(true); // 开启排序功能
    fileTreeView->sortByColumn(0, Qt::AscendingOrder); // 默认按文件名升序
	
	// 恢复 TreeView 列宽状态
	if (!m_config.fileTreeState.isEmpty()) {
        fileTreeView->header()->restoreState(QByteArray::fromBase64(m_config.fileTreeState));
    }
    
    connect(fileTreeView, &QTreeView::doubleClicked, this, &MainWindow::onDirDoubleClicked);

    rightLayout->addLayout(toolbar);
    rightLayout->addWidget(fileTreeView);

    browseSplitter->addWidget(serverTree);
    browseSplitter->addWidget(rightWidget);
	if (!m_config.browseSplitterState.isEmpty()) {
        browseSplitter->restoreState(QByteArray::fromBase64(m_config.browseSplitterState));
    } else {
        browseSplitter->setStretchFactor(1, 3); // 默认比例
    }
    tabWidget->addTab(browseSplitter, tr("Browse Files"));

    // Tab 2: Transfer Tasks View
    taskView = new QTableView();
    taskModel = new QStandardItemModel(0, 4, this);
    taskModel->setHorizontalHeaderLabels({tr("Status"), tr("Path"), tr("Progress"), tr("Action")});
    taskView->setModel(taskModel);
	taskView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // taskView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
	taskView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive); // 改为交互式调整
    taskView->horizontalHeader()->setStretchLastSection(true); // 最后一列自适应
    taskView->setSelectionMode(QAbstractItemView::NoSelection);
	
	// 恢复 TableView 列宽状态
    if (!m_config.taskTableState.isEmpty()) {
        taskView->horizontalHeader()->restoreState(QByteArray::fromBase64(m_config.taskTableState));
    }
    
	QSplitter *taskSplitter = new QSplitter(Qt::Vertical);
	
    QWidget *bottomWidget = new QWidget();
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottomWidget);
    
    QHBoxLayout *ctrlLayout = new QHBoxLayout();
    radioGz = new QRadioButton(".tar.gz"); radioGz->setChecked(true);
    radioXz = new QRadioButton(".tar.xz");
    QPushButton *btnStart = new QPushButton(tr("Start"));
	btnStart->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    QPushButton *btnStop = new QPushButton(tr("Stop"));
	btnStop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    QPushButton *btnClear = new QPushButton(tr("Clear Finished"));
	btnClear->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    

	ctrlLayout->addWidget(new QLabel(tr("Archive Format:")));
    ctrlLayout->addWidget(radioGz);
    ctrlLayout->addWidget(radioXz);
    ctrlLayout->addWidget(btnStart);
    ctrlLayout->addWidget(btnStop);
    ctrlLayout->addWidget(btnClear);
	
	QHBoxLayout *statusLayout = new QHBoxLayout();
    overallProgress = new QProgressBar();
    overallStatus = new QLabel(tr("Ready"));
	statusLayout->addWidget(overallStatus, 1);
	statusLayout->addWidget(overallProgress, 3);
    
    bottomLayout->addLayout(ctrlLayout);
    // bottomLayout->addWidget(overallStatus);
    // bottomLayout->addWidget(overallProgress);
	bottomLayout->addLayout(statusLayout);
	
	taskSplitter->addWidget(taskView);
    taskSplitter->addWidget(bottomWidget);
	taskSplitter->setStretchFactor(0, 1); // 让上半部分占据所有剩余空间
	// taskSplitter->setStretchFactor(1, 0); // 底部不拉伸
	taskSplitter->setCollapsible(1, false); // 禁止底部被完全折叠隐藏
    tabWidget->addTab(taskSplitter, tr("Transfer Tasks"));

    // Connects
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshDirectory);
    connect(btnAdd, &QPushButton::clicked, this, &MainWindow::addToDownloadTasks);
    connect(btnStart, &QPushButton::clicked, [this]() {
        m_transferMgr->start(m_config.targetPath, m_config.archiveFormat, radioXz->isChecked(), m_config.rules);
    });
    connect(btnStop, &QPushButton::clicked, m_transferMgr, &TransferManager::stop);
    // connect(btnClear, &QPushButton::clicked, m_transferMgr, &TransferManager::clearFinished);
	connect(btnClear, &QPushButton::clicked, [this]() {
        m_transferMgr->clearFinishedTasks();
        refreshTaskUi(); // 刷新 UI，清除已完成的任务行
    });

    rsyncProcess = new QProcess(this);
    connect(rsyncProcess, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(onRsyncFinished(int, QProcess::ExitStatus)));
}

void MainWindow::createMenus() {
    QMenu *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *actSettings = fileMenu->addAction(tr("Settings"));
    QAction *actExit = fileMenu->addAction(tr("Exit"));
	QMenu *helpMenu = menuBar()->addMenu(tr("Help"));
	QAction *actAbout = helpMenu->addAction(tr("About"));
    connect(actSettings, &QAction::triggered, this, &MainWindow::openSettings);
    connect(actExit, &QAction::triggered, qApp, &QApplication::quit);
	connect(actAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::setUiBusy(bool busy) {
    // 1. 禁用/启用文件列表视图（这会自动禁用双击信号）
    fileTreeView->setEnabled(!busy);
	serverTree->setEnabled(!busy);
    
    // 2. 禁用/启用导航相关的按钮（假设你有刷新按钮、返回上级按钮等）
    // btnRefresh->setEnabled(!busy);
    // backButton->setEnabled(!busy);
	breadcrumbWidget->setEnabled(!busy);
    
    // 3. 改变光标状态，给用户明确的“忙碌”反馈
    if (busy) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
}

void MainWindow::openSettings() {
    SettingsDialog dlg(m_config, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_config = dlg.getConfig();
        ConfigManager::saveConfig(m_config);
        loadServerTree();
    }
}

// 关于窗口
void MainWindow::showAboutDialog() {
    QDialog *aboutDlg = new QDialog(this);
    aboutDlg->setWindowTitle(tr("About This Program"));

    QVBoxLayout *mainVLayout = new QVBoxLayout(aboutDlg);
    // 【核心】：让布局决定窗口大小，用户无法手动拉大或缩小
    mainVLayout->setSizeConstraint(QLayout::SetFixedSize); 

    QHBoxLayout *contentLayout = new QHBoxLayout();
    
    // 图标部分
    QLabel *iconLabel = new QLabel();
    iconLabel->setPixmap(QPixmap(":/images/icon.png").scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    iconLabel->setAlignment(Qt::AlignTop);
    
    // 文本部分
    QVBoxLayout *textLayout = new QVBoxLayout();
    QLabel *titleLabel = new QLabel("<b>Pigeon Piper</b>");
    // QLabel *descLabel = new QLabel(tr("A professional file transfer and conversion tool..."));
	QLabel *descLabel = new QLabel(tr(
		"V1.0\n"
        "A professional file transfer and conversion tool.\n\n"
        "Features:\n"
        "• Remote server file browsing\n"
        "• Auto format conversion and packing\n"
        "• Secure Rsync & SSH integration\n"
		"require rsync>=3.1.0 tar>=1.26（support xz）\n"
		"MIT License\n"
		"© 2026 XeonM"
    ));
    
    // 【关键】：开启自动换行，并允许 QLabel 根据内容调整大小
    descLabel->setWordWrap(true);
    // 如果文本非常长，可以限制最大宽度，高度会自动延伸
    descLabel->setMaximumWidth(450); 

    textLayout->addWidget(titleLabel);
    textLayout->addWidget(descLabel);
    
    contentLayout->addWidget(iconLabel);
    contentLayout->addSpacing(10);
    contentLayout->addLayout(textLayout);

    mainVLayout->addLayout(contentLayout);

    QPushButton *btnClose = new QPushButton(tr("Close"));
    connect(btnClose, &QPushButton::clicked, aboutDlg, &QDialog::accept);
    mainVLayout->addWidget(btnClose, 0, Qt::AlignRight);

    aboutDlg->exec();
}

void MainWindow::checkRsyncVersion() {
	m_rsyncCmd = m_config.customRsyncPath.trimmed();
    if (m_rsyncCmd.isEmpty()) m_rsyncCmd = "rsync";
    QProcess process;
    process.start(m_rsyncCmd, QStringList() << "--version");
    
    // 等待进程结束，最多等 2 秒，防止异常挂起卡死 UI
    if (!process.waitForFinished(2000)) {
        QMessageBox::warning(this, tr("Environment Error"), tr("Failed to check system rsync version."));
        m_transferMgr->setRsyncCommand(m_rsyncCmd);
        return;
    }

    QString output = process.readAllStandardOutput();
    
    // 提取 rsync 版本号，如 "rsync  version 3.1.2  protocol version 31"
    // QRegExp rx("version\\s+(\\d+)\\.(\\d+)\\.(\\d+)");
    QRegularExpression re("version\\s+(\\d+)\\.(\\d+)\\.(\\d+)");
    QRegularExpressionMatch match = re.match(output);
	
    // if (rx.indexIn(output) != -1) {
        // int major = rx.cap(1).toInt();
        // int minor = rx.cap(2).toInt();
        // int patch = rx.cap(3).toInt();
	if (match.hasMatch()) {
		int major = match.captured(1).toInt();
		int minor = match.captured(2).toInt();
		int patch = match.captured(3).toInt();

        if (major < 3 || (major == 3 && minor == 0 && patch < 9)) {
            // < 3.0.9：弹框警告，无其他动作（保持默认 "rsync"）
            QMessageBox::warning(this, tr("Rsync Version Too Low"), 
                tr("Detected system rsync version is %1.%2.%3.\n"
                   "This software requires at least rsync 3.0.9.").arg(major).arg(minor).arg(patch));
        }
    } 

    // 将决定的命令路径同步给 TransferManager
    m_transferMgr->setRsyncCommand(m_rsyncCmd);
}

void MainWindow::loadServerTree() {
    serverTree->clear();
    for (const auto& srv : m_config.servers) {
        QTreeWidgetItem *srvItem = new QTreeWidgetItem(serverTree, {srv.host});
        for (const auto& path : srv.paths) {
            QTreeWidgetItem *pathItem = new QTreeWidgetItem(srvItem, {path.label});
            pathItem->setData(0, Qt::UserRole, path.path);
        }
    }
    serverTree->expandAll();
}

void MainWindow::onTreeItemClicked(QTreeWidgetItem *item, int column) {
	Q_UNUSED(column);
	
	// 如果进程正在运行，直接拒绝处理
	if (rsyncProcess->state() == QProcess::Running) {
        return;
    }
	
    if (item->parent()) { // Is a Path Label
        currentHost = item->parent()->text(0);
        currentLabel = item->text(0);
        currentBasePath = item->data(0, Qt::UserRole).toString();
        currentSubPath = "";
		targetSubPath = "";
        // updateBreadcrumbs();
        refreshDirectory();
    }
}

void MainWindow::updateBreadcrumbs() {
    QLayoutItem *child;
    while ((child = breadcrumbLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    breadcrumbLayout->addWidget(new QLabel(currentHost + " : "));
	
    // QPushButton *btnLabel = new QPushButton(currentLabel);
	// connect(btnLabel, &QPushButton::clicked, [this](){ targetSubPath = ""; refreshDirectory(); });
    // breadcrumbLayout->addWidget(btnLabel);
    
    // QStringList subs = currentSubPath.split('/', QString::SkipEmptyParts);
	// QString accum;
    // for (const QString& sub : subs) {
        // // breadcrumbLayout->addWidget(new QLabel(" / "));
        // QPushButton *btnSub = new QPushButton(sub);
		// accum += "/" + sub;
        // QString p = accum;
		// connect(btnSub, &QPushButton::clicked, [this, p](){ targetSubPath = p; refreshDirectory(); });
        // breadcrumbLayout->addWidget(btnSub);
    // }
	
	QStringList parts = currentSubPath.split('/', QString::SkipEmptyParts);
	// parts.prepend(currentLabel); // 加入根标签
	for (int i = -1; i < parts.size(); ++i) {
		QString btnText = (i == -1) ? currentLabel : parts[i];
        QPushButton *btn = new QPushButton(btnText);
        
		if (i == -1) {
            // 根目录按钮图标
			btn->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));
        }
		
        // 按钮样式
        QString style= "QPushButton {"
                        "  background-color: #f8f9fa;"
                        "  border: 1px solid #dee2e6;"
                        "  border-right: none;" // 隐藏右边框，由下一个按钮的左边框填补
                        "  border-radius: 0px;"
                        "  padding: 4px 12px;"
						"  font-size: 14px;"
                        "  min-height: 24px;"
                        "}"
                        "QPushButton:hover { background-color: #e9ecef; }"
                        "QPushButton:pressed { background-color: #dee2e6; }";
						
        if (i == -1) {
            // 根目录按钮样式
            style += "QPushButton { border-top-left-radius: 4px; border-bottom-left-radius: 4px; }";
        } 

        // 如果是最后一个按钮
        if (i == parts.size() - 1) {
            style += "QPushButton { font-weight: bold; border-right: 1px solid #dee2e6; border-top-right-radius: 4px; border-bottom-right-radius: 4px; }";
        }
        btn->setStyleSheet(style);

        // 绑定点击事件等...
        // i = -1 时 target 为 "" (根目录)
        // i = 0 时 target 为 "/" + parts[0]
        // i = 1 时 target 为 "/" + parts[0] + "/" + parts[1]
        // QString tPath = (i == -1) ? "" : parts.mid(0, i + 1).join("/");
		QString targetPath = "";
        if (i >= 0) {
            targetPath = "/" + parts.mid(0, i + 1).join("/");
        }

        connect(btn, &QPushButton::clicked, [this, targetPath]() {
            this->targetSubPath = targetPath;
            this->refreshDirectory();
        });
		
        breadcrumbLayout->addWidget(btn);
    }
    breadcrumbLayout->addStretch(); // 将按钮推向左侧
	
}

// 调用rsync获取目录内容
void MainWindow::refreshDirectory() {
    if (currentHost.isEmpty()) return;
	
	// 如果进程正在运行，直接拒绝处理
	if (rsyncProcess->state() == QProcess::Running) {
        return;
    }
	
	setUiBusy(true);
    activityIndicator->setText(tr("Loading..."));
	activityIndicator->setToolTip(""); // 每次刷新前清空上次的提示
	
	// if (rsyncProcess->state() == QProcess::Running) {
        // rsyncProcess->kill();
        // rsyncProcess->waitForFinished();
    // }
	
	
    // QString fullPath = currentBasePath + currentSubPath + "/";
	QString fullPath = currentBasePath + targetSubPath + "/";
    QStringList args;
    args << "--timeout=5" << "-e" << "ssh -o ConnectTimeout=5"
	     // << "-lptgoD" << "--out-format=%n|%l|%M" << QString("%1:%2").arg(currentHost).arg(fullPath);
		 // << "-av" << QString("%1:%2").arg(currentHost).arg(fullPath);
		 << "--list-only" << QString("%1:%2").arg(currentHost).arg(fullPath);
    rsyncProcess->start(m_rsyncCmd, args);
}

// 获取后处理，错误/正常
void MainWindow::onRsyncFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // Q_UNUSED(exitCode);
	setUiBusy(false);
	if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        QString errOutput = rsyncProcess->readAllStandardError();
        QString firstLine = errOutput.split('\n', QString::SkipEmptyParts).value(0).trimmed();
        
        activityIndicator->setText(tr("Failed"));
        activityIndicator->setToolTip(firstLine); // 设置 Hover 显示错误第一行
        
        // fileModel->removeRows(0, fileModel->rowCount()); // 清空表格，防止显示旧数据
		targetSubPath = currentSubPath;
        return;
    }
	
	currentSubPath = targetSubPath;
	updateBreadcrumbs();
	
	activityIndicator->setText(tr("Idle"));
	activityIndicator->setToolTip("");
    
    // 清空现有数据
    fileModel->removeRows(0, fileModel->rowCount());
    
    QString output = rsyncProcess->readAllStandardOutput();
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
    
    if (!currentSubPath.isEmpty()) {
        QList<QStandardItem*> rowItems;
        QStandardItem *nameItem = new QStandardItem("..");
		QStandardItem *typeItem = new QStandardItem();
		nameItem->setCheckable(false);
		nameItem->setIcon(style()->standardIcon(QStyle::SP_FileDialogToParent));
		typeItem->setText(tr("Up"));
		typeItem->setData("Up", Qt::UserRole);
        rowItems << nameItem << new QStandardItem("") << typeItem << new QStandardItem("");
        fileModel->appendRow(rowItems);
    }

    for (const QString& line : lines) {
		// rsync --list-only 输出
		// drwxr-xr-x          4,096 2026/03/11 12:37:41 .
		// lrwxrwxrwx             26 2026/03/11 12:37:41 ln_a_log
		// -rw-r--r--              0 2026/02/15 20:44:20 logfileB.log
		// drwxr-xr-x          4,096 2026/02/15 20:44:29 c_log
		
        // QStringList parts = line.split(QRegExp("\\s+"), QString::SkipEmptyParts);
        QStringList parts = line.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
        if (parts.size() < 5) continue;
        
        QString perms = parts[0];
        QString sizeStr = parts[1];
		
		bool ok;
        qint64 rawSize = QLocale::system().toLongLong(sizeStr, &ok);
        // 容错处理：如果 QLocale 解析失败，直接暴力移除逗号后再转换
		if (!ok) rawSize = sizeStr.remove(',').toLongLong();
		
        QString modTime = parts[2] + " " + parts[3];
        QString name = parts.mid(4).join(" ");
		
        if (name == "." || name == "..") continue; // 始终忽略rsync返回的 "." 和 ".."
        
        QList<QStandardItem*> rowItems;
        QStandardItem *nameItem = new QStandardItem(name);
		QStandardItem *typeItem = new QStandardItem();
        
		nameItem->setCheckable(true);
		nameItem->setCheckState(Qt::Unchecked);
		
		// 根据权限首字符判断文件类型并赋予图标
		if (perms.startsWith('d')) {
			nameItem->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
			typeItem->setText(tr("Dir"));
			typeItem->setData("Dir", Qt::UserRole);
		} else if (perms.startsWith('l')) {
			nameItem->setIcon(style()->standardIcon(QStyle::SP_FileLinkIcon));
			typeItem->setText(tr("Link"));
			typeItem->setData("Link", Qt::UserRole);
		} else {
			nameItem->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
			typeItem->setText(tr("File"));
			typeItem->setData("File", Qt::UserRole);
		}
        
        QStandardItem *sizeItem = new QStandardItem(formatSize(rawSize)); // 显示为可读字符串
        sizeItem->setData(rawSize, Qt::UserRole); // 在 Qt::UserRole 存入纯数字，供 ProxyModel 排序比较
		
        rowItems << nameItem 
                 << sizeItem 
				 << typeItem
                 << new QStandardItem(modTime);
        
        fileModel->appendRow(rowItems);
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // 捕获视图状态并转换为 Base64 保存
    m_config.fileTreeState = fileTreeView->header()->saveState().toBase64();
    m_config.taskTableState = taskView->horizontalHeader()->saveState().toBase64();
	m_config.browseSplitterState = browseSplitter->saveState().toBase64();
    
    // 保存配置
    ConfigManager::saveConfig(m_config);
	
    QMainWindow::closeEvent(event);
}

// 双击文件树条目
void MainWindow::onDirDoubleClicked(const QModelIndex &proxyIndex) {
	// 如果进程正在运行，直接拒绝处理
	if (rsyncProcess->state() == QProcess::Running) {
        return;
    }
		
	// 由于使用了 ProxyModel，需要将视图的 index 转换回原始 sourceModel 的 index
	QModelIndex index = fileSortModel->mapToSource(proxyIndex);
    int row = index.row();
    // QString type = fileModel->item(row, 2)->text(); // 第2列为Type
	QString typeMark = fileModel->item(row, 2)->data(Qt::UserRole).toString();
    QString name = fileModel->item(row, 0)->text(); // 第0列为Name
	
	targetSubPath = currentSubPath;
	
	// 处理返回上一级逻辑
    // if (name == "..") {
	if (typeMark == "Up") {
        int lastSlash = targetSubPath.lastIndexOf('/');
        if (lastSlash > 0) {
            // 如果路径是 /folder/subfolder，截取后变成 /folder
            targetSubPath = targetSubPath.left(lastSlash);
        } else {
            // 如果路径是 /folder，截取后回退到根路径
            targetSubPath = "";
        }
    }

    // 处理进入子目录逻辑（包括可能指向目录的链接）
    // else if (type == "Dir" || type == "Link") {
	else if (typeMark == "Dir" || typeMark == "Link") {
        // targetSubPath += "/" + name;
		targetSubPath = QDir::cleanPath(targetSubPath + "/" + name);
        // refreshDirectory();
    } else {
		return;
	}
	
	refreshDirectory();
}

void MainWindow::addToDownloadTasks() {
    auto existingTasks = m_transferMgr->getTasks(); // 获取当前任务列表用于查重
    int addedCount = 0;    // 记录成功添加的数量
    int skippedCount = 0;  // 记录跳过的重复文件数量
	
    for (int i = 0; i < fileModel->rowCount(); ++i) {
        QStandardItem *item = fileModel->item(i, 0);
        if (item && item->isCheckable() && item->checkState() == Qt::Checked) {
            QString fileName = item->text();
            QString remotePath = currentBasePath + currentSubPath;
            
            // 检查是否已经存在于任务列表中
            bool isDuplicate = false;
            for (const auto& et : existingTasks) {
                if (et.host == currentHost && et.label == currentLabel && 
                    et.remotePath == remotePath && et.fileName == fileName) {
                    isDuplicate = true;
                    break;
                }
            }
            
            // 只有不重复时才添加
            if (!isDuplicate) {
                TransferTask t;
                t.host = currentHost;
                t.label = currentLabel;
                t.remotePath = remotePath;
                t.localTempPath = currentSubPath;
                t.fileName = fileName;
                m_transferMgr->addTask(t);
				addedCount++;
            } else {
                skippedCount++;
            }
            
            item->setCheckState(Qt::Unchecked); // 无论是否重复，操作后都取消勾选
        }
    }
	
	// 弹窗提示结果
    if (addedCount > 0 || skippedCount > 0) {
        QString msg = QString(tr("Added %1 files to the task list")).arg(addedCount);
        if (skippedCount > 0) {
            msg += QString(tr("\nSkipped %1 duplicate files")).arg(skippedCount);
        }
        QMessageBox::information(this, tr("Add task"), msg);
    }
}

void MainWindow::refreshTaskUi() {
    auto tasks = m_transferMgr->getTasks();
    
    // 如果任务数量变化，调整模型行数
    if (taskModel->rowCount() != tasks.size()) {
        taskModel->setRowCount(tasks.size());
    }

    for (int i = 0; i < tasks.size(); ++i) {
        QString stateStr = tasks[i].state == Pending ? tr("Pending") :
                           tasks[i].state == Downloading ? tr("Downloading") :
                           tasks[i].state == Converting ? tr("Converting") :
                           tasks[i].state == Failed ? tr("Failed") : tr("Finished");
                           
        if (!taskModel->item(i, 0)) taskModel->setItem(i, 0, new QStandardItem());
        taskModel->item(i, 0)->setText(stateStr);
		
		if (tasks[i].state == Failed && !tasks[i].errorMsg.isEmpty()) {
            taskModel->item(i, 0)->setToolTip(tasks[i].errorMsg);
        } else {
            taskModel->item(i, 0)->setToolTip(""); // 非失败状态清空 Tooltip
        }
        
        if (!taskModel->item(i, 1)) taskModel->setItem(i, 1, new QStandardItem());
        // taskModel->item(i, 1)->setText(QString("%1:%2/%3").arg(tasks[i].host).arg(tasks[i].label).arg(tasks[i].fileName));
		taskModel->item(i, 1)->setText(QString("%1:%2%3/%4").arg(tasks[i].host).arg(tasks[i].label).arg(tasks[i].localTempPath).arg(tasks[i].fileName));
        
        // --- 插入或更新进度条 ---
        QModelIndex progIndex = taskModel->index(i, 2);
        QProgressBar *pb = qobject_cast<QProgressBar*>(taskView->indexWidget(progIndex));
        if (!pb) {
            pb = new QProgressBar();
            taskView->setIndexWidget(progIndex, pb);
        }
        // 进度伪逻辑（实际开发中可根据 rsync 输出更新详细进度）
        // if (tasks[i].state == Finished) pb->setValue(100);
        // else if (tasks[i].state == Downloading) pb->setValue(50); 
        // else pb->setValue(0);
        
        // 真实的进度逻辑
        if (tasks[i].state == Finished) {
			pb->setMaximum(100);
            pb->setMinimum(0);
            pb->setValue(100);
        } else if (tasks[i].state == Downloading) {
            // 直接读取解析出来的实时进度
            pb->setValue(tasks[i].progress); 
        } else if (tasks[i].state == Converting) {
            // 转换阶段进度未知，可以设置成“忙碌/跑马灯”状态 (设 max, min 为 0)
            pb->setMaximum(0);
            pb->setMinimum(0);
        } else {
            pb->setMaximum(100); // 恢复正常进度条模式
            pb->setValue(0);
        }
        
        // --- 插入删除按钮 ---
        QModelIndex btnIndex = taskModel->index(i, 3);
        QPushButton *btnDel = qobject_cast<QPushButton*>(taskView->indexWidget(btnIndex));
        if (!btnDel) {
            btnDel = new QPushButton();
			btnDel->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
			btnDel->setToolTip(tr("Delete"));
            // 动态捕获按钮所在的行号以确保安全删除
            connect(btnDel, &QPushButton::clicked, [this, btnDel]() {
                QModelIndex idx = taskView->indexAt(btnDel->pos());
                if (idx.isValid()) {
                    m_transferMgr->removeTask(idx.row());
                }
            });
            taskView->setIndexWidget(btnIndex, btnDel);
        }
    }
}