#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTreeWidget>
#include <QTreeView>
#include <QTableView>
#include <QStandardItemModel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QRadioButton>
#include <QProcess>
#include <QSortFilterProxyModel>
#include <QCloseEvent>
#include "configmanager.h"
#include "transfermanager.h"

class FileSortProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit FileSortProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override {
        // 调整排序规则
		// // 获取两行的类型（第2列）用于判断是否为文件夹
        // QString leftType = sourceModel()->data(sourceModel()->index(left.row(), 2)).toString();
        // QString rightType = sourceModel()->data(sourceModel()->index(right.row(), 2)).toString();
        // bool leftIsDir = (leftType == "Dir");
        // bool rightIsDir = (rightType == "Dir");

        // // 无论按哪列排序，始终让文件夹排在前面（适配升降序切换）
        // if (leftIsDir != rightIsDir) {
            // return sortOrder() == Qt::AscendingOrder ? leftIsDir : rightIsDir;
        // }

        // // 获取主排序列的数据
        // QVariant leftData = sourceModel()->data(left);
        // QVariant rightData = sourceModel()->data(right);

        // // 如果主条件相等，或者不是按名称排序，则使用名称（第0列）作为第二排序条件
        // if (leftData == rightData || sortColumn() != 0) {
            // if (sortColumn() == 1) { // 如果是按大小排序，使用 UserRole 中存的纯数字进行对比
                // qint64 lSize = sourceModel()->data(left, Qt::UserRole).toLongLong();
                // qint64 rSize = sourceModel()->data(right, Qt::UserRole).toLongLong();
                // if (lSize != rSize) return lSize < rSize;
            // } else if (leftData != rightData) {
                // // 如果是其他列且不相等，直接走默认字符串比较
                // return QSortFilterProxyModel::lessThan(left, right);
            // }
            
            // // 第二条件：按文件名排序
            // QString leftName = sourceModel()->data(sourceModel()->index(left.row(), 0)).toString();
            // QString rightName = sourceModel()->data(sourceModel()->index(right.row(), 0)).toString();
            // return leftName.compare(rightName, Qt::CaseInsensitive) < 0;
        // }

        // return QSortFilterProxyModel::lessThan(left, right);
		
		// 1. 始终获取第 2 列（Type列）的原始标记进行类型比较
        // 注意：使用 sourceModel()->index(...) 确保无论当前排哪一列，都能拿到类型列的数据
        QVariant leftData = sourceModel()->index(left.row(), 2, left.parent()).data(Qt::UserRole);
        QVariant rightData = sourceModel()->index(right.row(), 2, right.parent()).data(Qt::UserRole);
        
        QString leftType = leftData.toString();
        QString rightType = rightData.toString();

        // 2. 定义权重：Up=0, Dir=1, 其他(File/Link)=2
        auto getWeight = [](const QString& t) {
            if (t == "Up") return 0;
            if (t == "Dir") return 1;
            return 2;
        };

        int wL = getWeight(leftType);
        int wR = getWeight(rightType);

        // 3. 如果类型权重不同，强制让权重小的排在前面
        if (wL != wR) {
            // 关键：为了抵消 Qt 在 DescendingOrder 时对 lessThan 结果的自动反转，
            // 我们需要根据当前的 sortOrder 来决定返回结果，确保文件夹始终在上方。
            if (sortOrder() == Qt::AscendingOrder) {
                return wL < wR;
            } else {
                return wL > wR;
            }
        }

        // 4. 类型相同时，执行具体的列比较
        int column = sortColumn();
        if (column == 1) { // Size 列
            qint64 sL = sourceModel()->index(left.row(), 1, left.parent()).data(Qt::UserRole).toLongLong();
            qint64 sR = sourceModel()->index(right.row(), 1, right.parent()).data(Qt::UserRole).toLongLong();
            // return sL < sR;
			if (sL != sR) return sL < sR;
        }

        // 默认比较（名称、时间等）
        // return QSortFilterProxyModel::lessThan(left, right);
		
		// 其他所有情况（或大小相等时），按“文件名” (第 0 列) 排序
        QString nameL = sourceModel()->index(left.row(), 0, left.parent()).data(Qt::DisplayRole).toString();
        QString nameR = sourceModel()->index(right.row(), 0, right.parent()).data(Qt::DisplayRole).toString();
        
        // 使用本地化习惯排序（处理数字如 1, 2, 10 的自然排序）
        return nameL.localeAwareCompare(nameR) < 0;
    }
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void openSettings();
	void showAboutDialog();
    void loadServerTree();
    void onTreeItemClicked(QTreeWidgetItem *item, int column);
    void refreshDirectory();
    void onRsyncFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onDirDoubleClicked(const QModelIndex &index);
    void updateBreadcrumbs();
    void addToDownloadTasks();
    void refreshTaskUi();

protected:
    void closeEvent(QCloseEvent *event) override; // 重写关闭事件用于保存状态

private:
    void setupUi();
    void createMenus();
	void setUiBusy(bool busy);
	void checkRsyncVersion();

    AppConfig m_config;
    TransferManager* m_transferMgr;
	QString m_rsyncCmd = "rsync";
	
	QString formatSize(qint64 bytes); // 增加人类可读大小转换函数
    FileSortProxyModel *fileSortModel; // 新增排序模型

    // UI Elements
	QSplitter *browseSplitter;
    QTreeWidget *serverTree;
    QTreeView *fileTreeView;
    QStandardItemModel *fileModel;
    QWidget *breadcrumbWidget;
    QHBoxLayout *breadcrumbLayout;
    QLabel *activityIndicator;
    
    QTableView *taskView;
    QStandardItemModel *taskModel;
    QRadioButton *radioGz;
    QRadioButton *radioXz;
    QProgressBar *overallProgress;
    QLabel *overallStatus;

    // State
    QString currentHost;
    QString currentLabel;
    QString currentBasePath;
    QString currentSubPath;
	QString targetSubPath;
    QProcess *rsyncProcess;
};

#endif // MAINWINDOW_H