#ifndef TRANSFERMANAGER_H
#define TRANSFERMANAGER_H

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QCoreApplication>
#include "configmanager.h"

enum TaskState { Pending, Downloading, Converting, Finished, Failed };

struct TransferTask {
    QString host;
    QString label;
    QString remotePath;
    QString localTempPath;
    QString fileName;
    TaskState state = Pending;
	QString errorMsg;
	int progress = 0; // 记录当前任务的进度百分比
};

class TransferManager : public QObject {
    Q_OBJECT
public:
    explicit TransferManager(QObject *parent = nullptr);
    ~TransferManager();
    void addTask(const TransferTask& task);
    QList<TransferTask>& getTasks() { return tasks; }
    void start(const QString& targetRemotePath, const QString& archiveFmt, bool useXz, const QList<RuleConfig>& rules);
    void stop();
	void removeTask(int index);
	void clearFinishedTasks();
	QString formatArchiveName(const QString &pattern);
	void setRsyncCommand(const QString& cmd) { m_rsyncCmd = cmd; }

signals:
    void taskUpdated();
    void globalProgress(int percent, const QString& status);

private slots:
    void processNext();
    void onDownloadFinished(int exitCode);
	void onSingleConversionFinished(int exitCode);
    void onTarFinished(int exitCode);
    void onUploadFinished(int exitCode);
	void onRsyncProgress();

private:
    QList<TransferTask> tasks;
    int currentTaskIndex;
    QProcess* currentProcess;
	QString tempDirPath;
    QString archivesDirPath;
    QString currentTargetRemote;
    QString currentArchiveFormat;
	QString m_currentArchiveFilePath; // 用于在打包和上传阶段传递归档文件路径
	QList<RuleConfig> m_rules;
    int currentRuleIndex;
    bool m_useXz;
    bool isRunning;
	bool removeEmptyDirs(const QString& dirPath); // 递归清理空文件夹，但保留根目录
    enum Stage { Stage_Download, Stage_Pack, Stage_Upload } currentStage;
	QString m_rsyncCmd = "rsync"; // 默认使用系统环境变量中的 rsync
};

#endif // TRANSFERMANAGER_H