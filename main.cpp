#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);
	
	a.setWindowIcon(QIcon(":/images/icon.png"));
	
	// 1. 读取配置
    AppConfig config = ConfigManager::loadConfig();

    // 2. 加载翻译
    QTranslator translator;
    // 翻译文件编译到了资源文件 (qrc) 的 /i18n/ 目录下
    if (config.language == "zh_CN") {
        if (translator.load(":/i18n/app_zh_CN.qm")) {
            a.installTranslator(&translator);
        }
    } else if (config.language == "en") {
        if (translator.load(":/i18n/app_en.qm")) {
            a.installTranslator(&translator);
        }
    }
	

    MainWindow w;
    w.show();
    return a.exec();
}