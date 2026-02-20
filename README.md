# pigeonpiper
A Professional Log Transport Tool 一个高效日志下载打包工具

Pigeon Piper 是一款轻量级的日志自动化采集与预处理工具。它专为解决“多服务器日志分散、格式不统一、手动搜集繁琐”的痛点而设计。通过简单的 GUI 配置，你可以一键完成从远程拉取、本地转换、打包压缩到归档推送的全流程。
## ✨ 主要功能

- 多源同步：支持配置多个源服务器及对应的多个日志路径，基于 rsync 高效增量同步。
- 智能预处理：支持根据文件扩展名配置自定义工具（如将 .bin 转换为 .txt）。
- 灵活归档：
    - 自动使用 tar 进行高压缩比打包。
    - 支持基于 date 命令格式的自定义文件名（例如 logs_%Y%m%d_%H%M.tar.gz）。
- 双模式推送：
    - 远程归档：通过 rsync 推送到指定的备份服务器。
    - 本地存储：自动移动至本地指定目录（默认 ./downloads），支持自动创建路径。
- 环境自检：内置依赖版本（rsync/tar）检测工具，确保运行环境稳定。

## 🛠 系统要求

为确保程序正常运行，您的系统中需要预装以下组件：

- 操作系统：Linux 或 macOS (Windows 环境需安装 WSL 或相关命令集)
- rsync：建议版本 ≥ 3.0.9
- tar：建议版本 ≥ 1.26
- SSH：需配置免密登录（SSH Key）以便自动化同步

## 🚀 快速开始
1. 编译安装

- 确保你已安装 Go 环境，然后运行：
`Bash`

- 克隆仓库
```
git clone https://github.com/carlnerv/pigeonpiper
cd pigeonpiper
```

- 整理依赖并编译
```
go mod tidy
go build -o pigeonpiper main.go
```

2. 初始化配置

运行程序后，点击 “设置” 按钮：
- 全局配置：设置目标存储路径。若为空，默认保存在程序目录下的 downloads 文件夹。
- 转换工具：如果日志需要特殊处理，在此添加后缀名映射和处理命令。
- 源服务器：添加远程服务器地址及日志所在的绝对路径。

## 📂 目录说明

`./temp`: 运行时的临时工作目录，用于存放下载和处理中的原始日志。程序退出或任务完成时会保持清洁。

`./downloads`: 默认的本地归档存放点。

`config.json`: 所有的服务器和工具配置均保存在此文件中。

## 📜 开源协议

本项目采用 MIT 协议开源。

- 作者: XeonM

- 项目主页: [https://github.com/carlnerv/pigeonpiper](https://github.com/carlnerv/pigeonpiper)

本项目使用AI辅助
