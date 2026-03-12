#!/bin/bash

# 1. 获取脚本所在的绝对路径，确保在任何目录下执行脚本都能找到 libs
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 2. 设置二进制程序名
APP_NAME="Pigeonpiper"

# 3. 设置库文件路径
# 注意：我们将 ${SCRIPT_DIR}/libs 放在了 $LD_LIBRARY_PATH 的后面
# 这样链接器会先搜索系统默认路径（/lib64, /usr/lib64等），找不到时才看 libs 目录
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${SCRIPT_DIR}/libs

# 4. 如果你拷贝了 Qt 的 plugins 文件夹，需要指定插件路径，否则界面可能无法启动（如 xcb 错误）
if [ -d "${SCRIPT_DIR}/libs/plugins" ]; then
    export QT_PLUGIN_PATH=${SCRIPT_DIR}/libs/plugins
fi

# 5. 为了确保程序能找到同目录下的自带 rsync，将当前目录临时加入 PATH
# export PATH=${SCRIPT_DIR}:$PATH

# 6. 赋予二进制程序执行权限（以防万一）
# chmod +x "${SCRIPT_DIR}/${APP_NAME}"
# if [ -f "${SCRIPT_DIR}/rsync" ]; then
    # chmod +x "${SCRIPT_DIR}/rsync"
# fi

# 7. 启动程序，并传递所有输入参数 "$@"
echo "Starting ${APP_NAME}..."
exec "${SCRIPT_DIR}/${APP_NAME}" "$@"