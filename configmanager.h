#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QByteArray>

struct RuleConfig {
    QString ext;
    QString cmd;
};

struct PathConfig {
    QString label;
    QString path;
};

struct ServerConfig {
    QString host; // user@host
    QList<PathConfig> paths;
};

struct AppConfig {
    QString targetPath = "";
    QString archiveFormat = "Log_%Y%m%d_%H%M%S";
    QList<RuleConfig> rules;
    QList<ServerConfig> servers;
	QByteArray fileTreeState;
    QByteArray taskTableState;
	QByteArray browseSplitterState;
	QString language = "en";
	QString customRsyncPath; // 自定义 rsync 路径
};

class ConfigManager {
public:
    static AppConfig loadConfig() {
        AppConfig config;
        QFile file(getConfigPath());
        if (!file.exists()) {
            saveConfig(config); // 若不存在则创建默认
            return config;
        }
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject root = doc.object();
            
            QJsonObject global = root["global"].toObject();
            if (global.contains("target_path")) config.targetPath = global["target_path"].toString();
            if (global.contains("archive_format")) config.archiveFormat = global["archive_format"].toString();
			if (global.contains("language")) config.language = global["language"].toString();
            
            QJsonArray rulesArr = root["converters"].toArray();
            for (int i = 0; i < rulesArr.size(); ++i) {
                QJsonObject ro = rulesArr[i].toObject();
                config.rules.append({ro["ext"].toString(), ro["cmd"].toString()});
            }
            
            QJsonArray srvArr = root["servers"].toArray();
            for (int i = 0; i < srvArr.size(); ++i) {
                QJsonObject so = srvArr[i].toObject();
                ServerConfig sc;
                sc.host = so["host"].toString();
                QJsonArray pathArr = so["paths"].toArray();
                for (int j = 0; j < pathArr.size(); ++j) {
                    QJsonObject po = pathArr[j].toObject();
                    sc.paths.append({po["label"].toString(), po["path"].toString()});
                }
                config.servers.append(sc);
            }
			QJsonObject state = root["state"].toObject();
			if (state.contains("fileTreeState")) config.fileTreeState = state["fileTreeState"].toString().toLatin1();
			if (state.contains("taskTableState")) config.taskTableState = state["taskTableState"].toString().toLatin1();
			if (state.contains("browseSplitterState")) config.browseSplitterState = state["browseSplitterState"].toString().toLatin1();
            file.close();
        }
        return config;
    }

    static void saveConfig(const AppConfig& config) {
        QJsonObject root;
        QJsonObject global;
		QJsonObject state;
        global["target_path"] = config.targetPath;
        global["archive_format"] = config.archiveFormat;
		global["language"] = config.language;
        root["global"] = global;

        QJsonArray rulesArr;
        for (const auto& r : config.rules) {
            QJsonObject ro; ro["ext"] = r.ext; ro["cmd"] = r.cmd;
            rulesArr.append(ro);
        }
        root["converters"] = rulesArr;

        QJsonArray srvArr;
        for (const auto& s : config.servers) {
            QJsonObject so; so["host"] = s.host;
            QJsonArray pathArr;
            for (const auto& p : s.paths) {
                QJsonObject po; po["label"] = p.label; po["path"] = p.path;
                pathArr.append(po);
            }
            so["paths"] = pathArr;
            srvArr.append(so);
        }
        root["servers"] = srvArr;
		
		state["fileTreeState"] = QString::fromLatin1(config.fileTreeState);
        state["taskTableState"] = QString::fromLatin1(config.taskTableState);
		state["browseSplitterState"] = QString::fromLatin1(config.browseSplitterState);
		root["state"] = state;

        QFile file(getConfigPath());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson());
            file.close();
        }
    }
	
private:
    static QString getConfigPath() {
        // return QDir::homePath() + "/.syncapp_config.json";
        // 修改为程序同目录下的 pigeonpiper_config.json
        return QCoreApplication::applicationDirPath() + "/pigeonpiper_config.json";
    }
};

#endif // CONFIGMANAGER_H