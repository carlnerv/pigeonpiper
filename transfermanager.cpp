#include "transfermanager.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
// #include <QRegExp>
#include <QRegularExpression>

// TransferManager::TransferManager(QObject *parent) : QObject(parent), currentTaskIndex(0), currentProcess(new QProcess(this)), isRunning(false) {}
TransferManager::TransferManager(QObject *parent) : QObject(parent), isRunning(false) {
    // 设置为程序同目录下的 temp 和 archives
    tempDirPath = QCoreApplication::applicationDirPath() + "/temp";
    archivesDirPath = QCoreApplication::applicationDirPath() + "/archives";
    
    // 确保目录存在
    QDir().mkpath(tempDirPath);
    QDir().mkpath(archivesDirPath);

    currentProcess = new QProcess(this);
}

// TransferManager::~TransferManager() { stop(); }
TransferManager::~TransferManager() {
	stop();
	QDir dir(tempDirPath);
    if (dir.exists()) {
        dir.removeRecursively(); // 删除目录及其下所有内容
    }
}

void TransferManager::addTask(const TransferTask& task) {
    tasks.append(task);
    emit taskUpdated();
}

void TransferManager::start(const QString& targetRemotePath, const QString& archiveFmt, bool useXz, const QList<RuleConfig>& rules) {
	if (tasks.isEmpty()) return;
    if (isRunning) return;
    currentTargetRemote = targetRemotePath;
    currentArchiveFormat = archiveFmt;
    m_useXz = useXz;
    m_rules = rules;       // 保存转换规则
    currentRuleIndex = 0;  // 规则执行索引初始化
    isRunning = true;
    currentTaskIndex = 0;
    currentStage = Stage_Download;
    emit globalProgress(0, tr("Starting downloads..."));
    processNext();
}

void TransferManager::stop() {
    isRunning = false;
    if (currentProcess->state() == QProcess::Running) {
        currentProcess->kill();
        currentProcess->waitForFinished();
    }
    emit globalProgress(0, tr("Stopped"));
}

void TransferManager::clearFinishedTasks() {
	bool removedAny = false;
    QMutableListIterator<TransferTask> i(tasks);
    while (i.hasNext()) {
        auto& t = i.next();
        if (t.state == Finished) {
            // 删除源文件及可能转换生成的 .txt 文件
            // QString baseFilePath = tempDirPath + "/" + t.host + "/" + t.label + "/" + t.localTempPath+ "/" + t.fileName;
			QString baseFilePath = tempDirPath + "/" + t.host + "/" + t.label + t.localTempPath+ "/" + t.fileName;
            QFile::remove(baseFilePath);
            QFile::remove(baseFilePath + ".txt");
            i.remove(); 
			removedAny = true;
        }
    }
	
	if (removedAny) removeEmptyDirs(tempDirPath);
    
    // 修正索引防止越界
    if (currentTaskIndex > tasks.size()) currentTaskIndex = tasks.size();
    emit taskUpdated();
	emit globalProgress(0, tr("Ready"));
}

// 新增：递归清理空文件夹实现
bool TransferManager::removeEmptyDirs(const QString& dirPath) {
    QDir dir(dirPath);
    if (!dir.exists()) return true;

    bool isEmpty = true;
    // 获取目录下所有条目，排除 . 和 ..
    QFileInfoList entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    
    for (const QFileInfo& info : entries) {
        if (info.isDir()) {
            // 递归检查并清理子目录
            if (!removeEmptyDirs(info.absoluteFilePath())) {
                isEmpty = false; 
            }
        } else {
            isEmpty = false; // 存在文件，说明目录不为空
        }
    }

    // 如果目录为空，且当前目录不是我们要保留的 temp 根目录，则删除之
    if (isEmpty && dirPath != tempDirPath) {
        dir.rmdir(dirPath); 
    }
    
    return isEmpty;
}

void TransferManager::removeTask(int index) {
    if (index >= 0 && index < tasks.size()) {
        if (isRunning && index == currentTaskIndex) return; // 运行中的安全阻断
        
        // 核心逻辑：仅当任务已完成时，才删除对应的文件
        if (tasks[index].state == Finished) {
            // 注意：一定要加上 /t.fileName，之前遗漏了这层
            QString fileFullPath = tempDirPath + "/" + tasks[index].host + "/" + tasks[index].label + tasks[index].localTempPath + "/" + tasks[index].fileName;
            QFile::remove(fileFullPath);
            QFile::remove(fileFullPath + ".txt");
            
            tasks.removeAt(index);

			// 清理空文件夹
            removeEmptyDirs(tempDirPath);
        } else {
            // 未下载完（待下载/下载中），仅删除任务条目，保留断点续传文件
            tasks.removeAt(index);
        }
        
        if (index < currentTaskIndex) currentTaskIndex--;
        emit taskUpdated();
    }
}

QString TransferManager::formatArchiveName(const QString &pattern) {
    QDateTime now = QDateTime::currentDateTime();
    QString result = pattern;

    // 常用日期占位符
    result.replace("%Y", now.toString("yyyy")); // 4位年份 (如 2026)
    // result.replace("%y", now.toString("yy"));   // 2位年份 (如 26)
    result.replace("%m", now.toString("MM"));   // 月份 01-12
    result.replace("%d", now.toString("dd"));   // 日期 01-31
    
    // 常用时间占位符
    result.replace("%H", now.toString("hh"));   // 小时 00-23
    result.replace("%M", now.toString("mm"));   // 分钟 00-59
    result.replace("%S", now.toString("ss"));   // 秒 00-59
    
    // 组合占位符支持 (可选，为了兼容常见的快捷写法)
    // result.replace("%F", now.toString("yyyy-MM-dd")); // 等同于 %Y-%m-%d
    // result.replace("%T", now.toString("hh:mm:ss"));   // 等同于 %H:%M:%S

    return result;
}

void TransferManager::processNext() {
    if (!isRunning) return;

	// 下载阶段
    if (currentStage == Stage_Download) {
		// 如果所有任务都已处理完，直接跳到打包阶段（去除了原来的 Convert 阶段）
        if (currentTaskIndex >= tasks.size()) {
            currentStage = Stage_Pack;
            emit globalProgress(50, tr("Packing...")); // 更新全局进度提示
            processNext();
            return;
        }

        // 开始下载
        TransferTask& t = tasks[currentTaskIndex];
        t.state = Downloading;
        emit taskUpdated();

        // QString localPath = tempDir.path() + "/" + t.host + "/" + t.label + "/" + t.localTempPath;
		QString localPath = tempDirPath + "/" + t.host + "/" + t.label + "/" + t.localTempPath;
        QDir().mkpath(localPath);

        // disconnect(currentProcess, SIGNAL(finished(int)), this, 0);
        // connect(currentProcess, SIGNAL(finished(int)), this, SLOT(onDownloadFinished(int)));
        
		// 每次开始新下载前，确保进度归零
        tasks[currentTaskIndex].progress = 0;

        disconnect(currentProcess, SIGNAL(finished(int)), this, 0);
        // 断开可能遗留的旧输出读取连接
        disconnect(currentProcess, &QProcess::readyReadStandardOutput, this, 0); 

        connect(currentProcess, SIGNAL(finished(int)), this, SLOT(onDownloadFinished(int)));
        // 将 rsync 的标准输出连接到我们刚写的解析函数上
        connect(currentProcess, &QProcess::readyReadStandardOutput, this, &TransferManager::onRsyncProgress);
		
        QStringList args;
        args << "--timeout=5" << "-e" << "ssh -o ConnectTimeout=5"
		     << "-av" << "--info=progress2" << "--no-inc-recursive"
			 << QString("%1:%2/%3").arg(t.host).arg(t.remotePath).arg(t.fileName) << localPath;
        // 确保 process 模式能够独立捕获 standard output
        currentProcess->setProcessChannelMode(QProcess::SeparateChannels);
		currentProcess->start(m_rsyncCmd, args);
    } 
	// 打包阶段
    else if (currentStage == Stage_Pack) {
		QString archName = formatArchiveName(currentArchiveFormat);
        if (m_useXz) archName += ".tar.xz"; else archName += ".tar.gz";
		m_currentArchiveFilePath = archivesDirPath + "/" + archName;

        disconnect(currentProcess, SIGNAL(finished(int)), this, 0);
        connect(currentProcess, SIGNAL(finished(int)), this, SLOT(onTarFinished(int)));
        
        QStringList args;
		if (m_useXz) args << "-cJf" << m_currentArchiveFilePath << "-C" << tempDirPath << ".";
        else args << "-czf" << m_currentArchiveFilePath	<< "-C" << tempDirPath << ".";
        currentProcess->start("tar", args);
    }
	// 上传阶段
	else if (currentStage == Stage_Upload) {
        QString target = currentTargetRemote.trimmed();
        
        // 如果目标路径为空，直接结束流程
        if (target.isEmpty()) {
            emit globalProgress(100, tr("Finished (Archive saved locally, no upload target)."));
            isRunning = false;
            return;
        }

        emit globalProgress(90, tr("Uploading archive..."));
        disconnect(currentProcess, SIGNAL(finished(int)), this, 0);
        connect(currentProcess, SIGNAL(finished(int)), this, SLOT(onUploadFinished(int)));
        
        // 使用 rsync 进行上传，支持本地复制或远程传输
        currentProcess->start(m_rsyncCmd, {"--timeout=5", "-e", "ssh -o ConnectTimeout=5", "-av", m_currentArchiveFilePath, target});
    }
}

void TransferManager::onDownloadFinished(int exitCode) {
    if (!isRunning) return;
	
	if (exitCode == 0) {
        // 下载成功，检查是否需要转换
        QString fileName = tasks[currentTaskIndex].fileName;
        QString matchedCmd = "";
        // tasks[currentTaskIndex].progress = 100;
        // 遍历配置中的规则（假设可以通过 ConfigManager::loadConfig() 或类成员 m_config 获取）
        auto config = ConfigManager::loadConfig(); 
		// auto config = m_Config; 
        for (const auto& rule : config.rules) {
            // 确保后缀匹配逻辑正确（防呆设计，处理用户可能没填 '.' 的情况）
            QString ext = rule.ext;
            if (!ext.startsWith(".")) ext = "." + ext; 
			
			QString pattern = QRegularExpression::escape(ext) + "(\\.\\d+)?$";
			QRegularExpression re(pattern);
            
            // if (fileName.endsWith(ext, Qt::CaseInsensitive)) {
			if (re.match(fileName).hasMatch()) {
                matchedCmd = rule.cmd;
                break;
            }
        }
        
        // 如果匹配到了转换规则
        if (!matchedCmd.isEmpty()) {
            tasks[currentTaskIndex].state = Converting; // 状态更新为：转换中
            emit taskUpdated();
            
            // 构造文件的绝对路径
            QString filePath = QDir::cleanPath(tempDirPath + "/" + 
                               tasks[currentTaskIndex].host + "/" + 
                               tasks[currentTaskIndex].label + "/" + 
                               tasks[currentTaskIndex].localTempPath + "/" + 
                               fileName);
            
            // 断开旧连接，接入转换完成的槽函数
            disconnect(currentProcess, SIGNAL(finished(int)), this, 0);
            connect(currentProcess, SIGNAL(finished(int)), this, SLOT(onSingleConversionFinished(int)));
            
            // 【核心】：使用 Qt 原生文件重定向，杜绝 Shell 注入
            currentProcess->setStandardOutputFile(filePath + ".txt");
            currentProcess->start(matchedCmd, QStringList() << filePath);
            
            return; // ！！重要：直接返回，等待转换进程触发 onSingleConversionFinished
        } else {
            // 不需要转换，直接标记完成
            tasks[currentTaskIndex].state = Finished;
            tasks[currentTaskIndex].errorMsg = "";
        }
    } else {
        // 下载失败处理
        tasks[currentTaskIndex].state = Failed;
        QString errOutput = currentProcess->readAllStandardError();
        tasks[currentTaskIndex].errorMsg = errOutput.split('\n', QString::SkipEmptyParts).value(0).trimmed();
    }
	
	emit taskUpdated();
    currentTaskIndex++;
    processNext();
}

void TransferManager::onSingleConversionFinished(int exitCode) {
    if (!isRunning) return;
    
    // ！！极其重要：恢复标准输出，否则后续的 tar 和 rsync 命令的输出也会被写进 txt 文件里
    // currentProcess->setProcessChannelMode(QProcess::NormalChannels);
	currentProcess->setProcessChannelMode(QProcess::SeparateChannels);
	currentProcess->setStandardOutputFile("");
    
    if (exitCode == 0) {
        tasks[currentTaskIndex].state = Finished;
        tasks[currentTaskIndex].errorMsg = "";
    } else {
        tasks[currentTaskIndex].state = Failed;
        QString errOutput = currentProcess->readAllStandardError();
        QString firstLine = errOutput.split('\n', QString::SkipEmptyParts).value(0).trimmed();
        // 增加前缀以区分是下载失败还是转换失败
        tasks[currentTaskIndex].errorMsg = tr("Convert Failed: ") + firstLine; 
    }
    
    emit taskUpdated();
    currentTaskIndex++;
    processNext(); // 继续处理下一个任务
}

void TransferManager::onTarFinished(int exitCode) {
	Q_UNUSED(exitCode);
    if (!isRunning) return;
	// 打包完成后，进入上传阶段
    currentStage = Stage_Upload;
    processNext();
}

void TransferManager::onUploadFinished(int exitCode) {
    if (!isRunning) return;
    
    if (exitCode == 0) {
        emit globalProgress(100, tr("All Finished! Upload successful."));
    } else {
		// 上传失败时，提取错误信息显示在全局 Label 上
        QString errOutput = currentProcess->readAllStandardError();
        QString firstLine = errOutput.split('\n', QString::SkipEmptyParts).value(0).trimmed();
        emit globalProgress(100, tr("Upload Failed: %1").arg(firstLine));
    }
    
    isRunning = false;
}

void TransferManager::onRsyncProgress() {
    if (!isRunning || currentStage != Stage_Download) return;
    if (currentTaskIndex >= tasks.size()) return;

    // 读取当前缓冲区所有的标准输出
    QString output = currentProcess->readAllStandardOutput();
    
    // 使用正则匹配类似于 " 45%" 或 "100%" 的文本
    // QRegExp rx("\\b(\\d+)%");
    QRegularExpression re("\\b(\\d+)%");
	QRegularExpressionMatchIterator i = re.globalMatch(output);
    // int pos = 0;
    int lastProgress = -1;
    
    // 遍历当前读取到的所有块，找到最新的一个百分比
    // while ((pos = rx.indexIn(output, pos)) != -1) {
        // lastProgress = rx.cap(1).toInt();
        // pos += rx.matchedLength();
    // }
	while (i.hasNext()) {
		QRegularExpressionMatch match = i.next();
		lastProgress = match.captured(1).toInt();
	}

    if (lastProgress >= 0 && lastProgress <= 100) {
        // 只有当进度真正发生变化时才触发 UI 刷新，防止频繁重绘导致界面卡顿
        if (tasks[currentTaskIndex].progress != lastProgress) {
            tasks[currentTaskIndex].progress = lastProgress;
            emit taskUpdated(); 
        }
    }
}