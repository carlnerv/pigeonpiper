#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QComboBox>
#include "configmanager.h"

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const AppConfig& config, QWidget *parent = nullptr);
    AppConfig getConfig() const;

private slots:
    void onSave();
    void checkEnvDependencies();
    void testTargetReachability();
    
    // 动态增删UI槽函数
    void addRuleRow(const QString& ext = "", const QString& cmd = "");
    void addServerBox(const ServerConfig& srv = ServerConfig());
    void addPathRow(QVBoxLayout* pathLayout, const QString& hostEdit, const QString& label = "", const QString& path = "");

private:
    AppConfig m_config;
	
	QRegularExpressionValidator *m_safeValidator;

    // 全局配置控件
    QLineEdit *targetPathEdit;
    QLineEdit *archiveFmtEdit;
	QComboBox *comboLanguage;
	QLineEdit *customRsyncEdit;

    // 动态区域布局
    QVBoxLayout *rulesLayout;
    QVBoxLayout *serversLayout;
};

#endif // SETTINGSDIALOG_H