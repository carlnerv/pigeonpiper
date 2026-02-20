package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	// "net"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/container"
	// "fyne.io/fyne/v2/data/binding"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

// --- 1. 配置与数据结构 ---

type ServerPath struct {
	Label string `json:"label"`
	Path  string `json:"path"`
}

type ServerConfig struct {
	Address string       `json:"address"` // user@host
	Paths   []ServerPath `json:"paths"`
}

type ToolRule struct {
	Ext     string `json:"ext"`
	ToolCmd string `json:"tool_cmd"`
}

type Config struct {
	LocalTempDir      string         `json:"local_temp_dir"`
	DestServer        string         `json:"dest_server"`
	ArchiveNameFormat string         `json:"archive_name_format"`
	Servers           []ServerConfig `json:"servers"`
	ToolRules         []ToolRule     `json:"tool_rules"`
}

// 修改状态枚举
const (
	StatusPending     = "待下载"
	StatusDownloading = "下载中"
	StatusConverting  = "转换中"
	StatusCompleted   = "已完成"
)

type FileItem struct {
	ID             string
	ServerAddress  string
	HostName       string
	PathLabel      string
	RemoteBasePath string
	SubPath        string
	FileName       string
	DisplayPath    string // UI显示用

	Status   string
	Progress float64

	StatusLabel *widget.Label
	ProgressBar *widget.ProgressBar
}

// --- 全局变量 ---
var (
	appConfig     Config
	downloadQueue []*FileItem
	queueLock     sync.Mutex

	currentFileList []RemoteFile
	tableData       []RemoteFile

	mainCtx      context.Context
	mainCancel   context.CancelFunc
	isProcessing bool

	downloadDir string
	archiveDir  string
)

type RemoteFile struct {
	Name    string
	Size    string
	RawSize int64
	Date    string
	IsDir   bool
	Checked bool
	IsBack  bool // 标记是否为返回上级
}

// --- 2. 安全与工具函数 ---

func isSafePath(baseDir, targetPath string) bool {
	absBase, err := filepath.Abs(baseDir)
	if err != nil {
		return false
	}
	absTarget, err := filepath.Abs(targetPath)
	if err != nil {
		return false
	}
	prefix := absBase + string(filepath.Separator)
	return strings.HasPrefix(absTarget, prefix) || absTarget == absBase
}

func loadConfig(path string) error {
	f, err := os.Open(path)
	if err != nil {
		appConfig = Config{
			LocalTempDir:      "./temp",
			DestServer:        "./downloads",
			ArchiveNameFormat: "logs_%Y%m%d_%H%M%S",
			// ToolRules: []ToolRule{
				// {Ext: ".lzo", ToolCmd: "lzop -d -c"},
			// },
		}
		return nil
	}
	defer f.Close()
	return json.NewDecoder(f).Decode(&appConfig)
}

func saveConfig(path string) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	return enc.Encode(appConfig)
}

func initDirs() {
	if appConfig.LocalTempDir == "" {
		appConfig.LocalTempDir = "./temp"
	}
	absTemp, _ := filepath.Abs(appConfig.LocalTempDir)
	appConfig.LocalTempDir = absTemp
	
	// if appConfig.LocalTempDir == "" {
		// appConfig.LocalTempDir = "./temp"
	// }
	// absTemp, _ := filepath.Abs(appConfig.LocalTempDir)
	// appConfig.LocalTempDir = absTemp

	downloadDir = filepath.Join(appConfig.LocalTempDir, "downloads")
	archiveDir = filepath.Join(appConfig.LocalTempDir, "archives")
}

func cleanTempDirs() {
	if appConfig.LocalTempDir == "/" || appConfig.LocalTempDir == "\\" || strings.HasSuffix(appConfig.LocalTempDir, ":\\") {
		return
	}
	os.RemoveAll(downloadDir)
	os.RemoveAll(archiveDir)
	os.MkdirAll(downloadDir, 0755)
	os.MkdirAll(archiveDir, 0755)
	// os.MkdirAll("downloads", 0755)
}

func cleanEmptyDirs(targetDir string) {
	if !isSafePath(appConfig.LocalTempDir, targetDir) {
		return
	}
	cmd := exec.Command("find", targetDir, "-type", "d", "-empty", "-delete")
	_ = cmd.Run()
}

func removeLocalFile(filePath string) {
	if !isSafePath(downloadDir, filePath) {
		return
	}
	os.Remove(filePath)
	txtPath := filePath + ".txt"
	if isSafePath(downloadDir, txtPath) {
		if !strings.HasSuffix(filePath, ".txt") {
			os.Remove(txtPath)
		}
	}
}

func formatFileName(format string) string {
	if format == "" {
		format = "logs_%Y%m%d_%H%M%S"
	}
	cmd := exec.Command("date", "+"+format)
	out, err := cmd.Output()
	if err != nil {
		return "LogPack_" + time.Now().Format("20060102")
	}
	return strings.TrimSpace(string(out))
}

func getHostName(address string) string {
	parts := strings.Split(address, "@")
	if len(parts) > 1 {
		return parts[1]
	}
	return parts[0]
}

func parseRsyncLine(line string) *RemoteFile {
	fields := strings.Fields(line)
	if len(fields) < 5 {
		return nil
	}
	perms := fields[0]
	isDir := strings.HasPrefix(perms, "d")
	dateIdx := -1
	for i, f := range fields {
		if (strings.Contains(f, "/") || strings.Contains(f, "-")) && i > 1 {
			if i+1 < len(fields) && strings.Contains(fields[i+1], ":") {
				dateIdx = i
				break
			}
		}
	}
	if dateIdx == -1 || dateIdx+2 >= len(fields) {
		return nil
	}
	sizeStr := fields[dateIdx-1]
	sizeStr = strings.ReplaceAll(sizeStr, ",", "")
	size, _ := strconv.ParseInt(sizeStr, 10, 64)
	dateStr := fields[dateIdx] + " " + fields[dateIdx+1]
	name := strings.Join(fields[dateIdx+2:], " ")
	if name == "." || name == ".." {
		return nil
	}
	return &RemoteFile{Name: name, RawSize: size, Size: byteCountBinary(size), Date: dateStr, IsDir: isDir}
}

func byteCountBinary(b int64) string {
	const unit = 1024
	if b < unit {
		return fmt.Sprintf("%d B", b)
	}
	div, exp := int64(unit), 0
	for n := b / unit; n >= unit && exp < 5; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(b)/float64(div), "KMGTPE"[exp])
}

// // 测试 TCP 连接 (user@host:path -> host:22)
// func testConnection(addressStr string) error {
	// if addressStr == "" {
		// return errors.New("地址为空")
	// }
	// // 移除路径部分 /path
	// hostPart := strings.Split(addressStr, ":")[0]
	// // 移除用户部分 user@
	// if strings.Contains(hostPart, "@") {
		// hostPart = strings.Split(hostPart, "@")[1]
	// }

	// conn, err := net.DialTimeout("tcp", hostPart+":22", 3*time.Second)
	// if err != nil {
		// return err
	// }
	// conn.Close()
	// return nil
// }

// 测试 rsync 连接及路径可达性
func testConnection(addressStr string) error {
	if strings.TrimSpace(addressStr) == "" {
		return errors.New("地址为空")
	}

	target := addressStr
	// rsync 远端地址要求至少包含 ":"，如果没有则默认探测目标家目录
	if !strings.Contains(target, ":") {
		target += ":"
	}

	// 设定 5 秒超时，防止网络不通一直卡死
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	// --list-only 只列出目录内容，不进行实质性的数据传输
	cmd := exec.CommandContext(ctx, "rsync", "--list-only", target)
	
	// 捕获综合输出，以便更直观地向用户展示失败原因（如密码错误、目录不存在等）
	outBytes, err := cmd.CombinedOutput()
	if err != nil {
		if ctx.Err() == context.DeadlineExceeded {
			return errors.New("连接超时，请检查网络或防火墙配置")
		}
		
		errMsg := strings.TrimSpace(string(outBytes))
		if errMsg == "" {
			errMsg = err.Error()
		}
		return fmt.Errorf("连接或路径不可达: %s", errMsg)
	}

	return nil
}

// 测试路径是否存在 (使用 rsync --list-only)
func testPath(address, pathStr string) error {
	remote := fmt.Sprintf("%s:%s", address, pathStr)
	// 使用 -d 仅查看目录本身，不递归列出内容
	cmd := exec.Command("rsync", "--list-only", "-d", remote)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("失败: %v\n%s", err, string(out))
	}
	return nil
}

// 检查版本号
func checkVersion(cmdName, minVer string) (bool, string) {
	cmd := exec.Command(cmdName, "--version")
	out, err := cmd.Output()
	if err != nil {
		return false, "未安装"
	}

	re := regexp.MustCompile(`(\d+)\.(\d+)(?:\.(\d+))?`)
	matches := re.FindStringSubmatch(string(out))
	if len(matches) < 3 {
		return false, "未知版本"
	}

	verStr := matches[0]
	parseVer := func(s string) (int, int, int) {
		parts := strings.Split(s, ".")
		maj, _ := strconv.Atoi(parts[0])
		min, _ := strconv.Atoi(parts[1])
		pat := 0
		if len(parts) > 2 {
			pat, _ = strconv.Atoi(parts[2])
		}
		return maj, min, pat
	}

	cMaj, cMin, cPat := parseVer(verStr)
	mMaj, mMin, mPat := parseVer(minVer)

	if cMaj > mMaj {
		return true, verStr
	}
	if cMaj < mMaj {
		return false, verStr
	}
	if cMin > mMin {
		return true, verStr
	}
	if cMin < mMin {
		return false, verStr
	}
	if cPat >= mPat {
		return true, verStr
	}

	return false, verStr
}


// --- 3. 配置窗口 ---

func showConfigWindow(onSaved func()) {
	w := fyne.CurrentApp().NewWindow("设置")
	w.Resize(fyne.NewSize(900, 700))

	// 1. 全局配置 (移除本地临时目录，固定使用程序内目录)
	destServerEntry := widget.NewEntry()
	destServerEntry.SetText(appConfig.DestServer)
	destServerEntry.SetPlaceHolder("远程(user@host:/path) 或 本地绝对路径(/local/path 默认: ./downloads)")

	testDestBtn := widget.NewButton("测试连接/路径", func() {
		target := strings.TrimSpace(destServerEntry.Text)
		// 如果为空，自动使用默认的 ./downloads
		if target == "" {
			// dialog.ShowInformation("提示", "目标地址不可为空", w)
			// return
			target = "./downloads"
			destServerEntry.SetText(target) // 同步更新 UI 显示
		}

		// 智能判断：包含 '@' 和 ':' 视为远程服务器 rsync 格式，否则视为本地路径
		isRemote := strings.Contains(target, "@") && strings.Contains(target, ":")

		if isRemote {
			// 远程服务器：使用 ssh / rsync 测试
			if err := testConnection(target); err != nil {
				dialog.ShowError(err, w)
			} else {
				dialog.ShowInformation("成功", "目标服务器连接正常", w)
			}
		} else {
			// 本地路径：检查本地文件系统
			info, err := os.Stat(target)
			if err != nil {
				if os.IsNotExist(err) {
					dialog.ShowInformation("提示", fmt.Sprintf("本地路径 [%s] 目前不存在，上传时将自动尝试创建", target), w)
				} else {
					dialog.ShowError(fmt.Errorf("访问本地路径异常: %v", err), w)
				}
			} else {
				if info.IsDir() {
					dialog.ShowInformation("成功", fmt.Sprintf("本地路径 [%s] 已存在并可访问", target), w)
				} else {
					dialog.ShowError(errors.New("目标路径已存在，但它是一个文件而不是文件夹"), w)
				}
			}
		}
	})

	archiveFmtEntry := widget.NewEntry()
	archiveFmtEntry.SetText(appConfig.ArchiveNameFormat)
	archiveFmtEntry.SetPlaceHolder("logs_%Y%m%d")

	fmtHelpBtn := widget.NewButtonWithIcon("", theme.HelpIcon(), func() {
		msg := "支持 date 命令格式，例如:\n" +
			"%Y - 年份 (2023)\n%m - 月份 (01-12)\n%d - 日期 (01-31)\n" +
			"%H - 小时 (00-23)\n%M - 分钟 (00-59)\n" +
			"示例: logs_%Y%m%d_%H%M"
		dialog.ShowInformation("格式说明", msg, w)
	})

	// 版本检查
	verCheckLabel := widget.NewLabel("点击按钮检查依赖版本")
	checkVerBtn := widget.NewButton("检查环境依赖", func() {
		okRsync, vRsync := checkVersion("rsync", "3.0.9")
		okTar, vTar := checkVersion("tar", "1.26")
		res := "rsync>=3.0.9 "
		if okRsync {
			res += "✅ (" + vRsync + ")  "
		} else {
			res += "❌ (" + vRsync + ")  "
		}
		res += "tar>=1.26 "
		if okTar {
			res += "✅ (" + vTar + ")"
		} else {
			res += "❌ (" + vTar + ")"
		}
		verCheckLabel.SetText(res)
	})

	globalBox := container.NewVBox(
		widget.NewLabelWithStyle("全局配置", fyne.TextAlignLeading, fyne.TextStyle{Bold: true}),
		// 移除临时目录 UI
		container.NewBorder(nil, nil, widget.NewLabel("目标服务器/路径:"), testDestBtn, destServerEntry),
		container.NewBorder(nil, nil, widget.NewLabel("打包文件名格式:"), fmtHelpBtn, archiveFmtEntry),
		container.NewHBox(checkVerBtn, verCheckLabel),
		widget.NewSeparator(),
	)

	// 2. 工具配置
	toolsContainer := container.NewVBox()
	addToolRow := func(ext, cmd string) {
		extEnt := widget.NewEntry()
		extEnt.SetText(ext)
		extEnt.SetPlaceHolder(".ext")
		cmdEnt := widget.NewEntry()
		cmdEnt.SetText(cmd)
		cmdEnt.SetPlaceHolder("/path/to/cmd args")
		testBtn := widget.NewButton("测试", func() {
			args := strings.Fields(cmdEnt.Text)
			if len(args) == 0 {
				dialog.ShowError(errors.New("❌ 命令为空"), w)
				return
			}
			p, err := exec.LookPath(args[0])
			if err != nil {
				dialog.ShowError(errors.New("❌ 未找到: "+args[0]), w)
			} else {
				dialog.ShowInformation("✅ 成功", "路径: "+p, w)
			}
		})
		var row *fyne.Container
		delBtn := widget.NewButtonWithIcon("", theme.DeleteIcon(), func() { toolsContainer.Remove(row) })
		row = container.NewGridWithColumns(4, extEnt, cmdEnt, testBtn, delBtn)
		toolsContainer.Add(row)
	}
	for _, t := range appConfig.ToolRules {
		addToolRow(t.Ext, t.ToolCmd)
	}

	toolsBox := container.NewVBox(
		widget.NewLabelWithStyle("转换工具配置", fyne.TextAlignLeading, fyne.TextStyle{Bold: true}),
		container.NewGridWithColumns(4, widget.NewLabel("扩展名"), widget.NewLabel("命令"), widget.NewLabel("检查"), widget.NewLabel("操作")),
		toolsContainer,
		widget.NewButtonWithIcon("添加规则", theme.ContentAddIcon(), func() { addToolRow("", "") }),
		widget.NewSeparator(),
	)

	// 3. 服务器配置
	serversContainer := container.NewVBox()
	addServerRow := func(srv ServerConfig) {
		addrEnt := widget.NewEntry()
		addrEnt.SetPlaceHolder("user@host")
		addrEnt.SetText(srv.Address)

		testAddrBtn := widget.NewButton("测试", func() {
			if err := testConnection(addrEnt.Text); err != nil {
				dialog.ShowError(err, w)
			} else {
				dialog.ShowInformation("成功", "连接正常", w)
			}
		})

		pathsBox := container.NewVBox()
		addPathRow := func(label, pVal string) {
			lEnt := widget.NewEntry()
			lEnt.SetPlaceHolder("标签")
			lEnt.SetText(label)
			pEnt := widget.NewEntry()
			pEnt.SetPlaceHolder("/path")
			pEnt.SetText(pVal)

			testPathBtn := widget.NewButton("检查路径", func() {
				if addrEnt.Text == "" || pEnt.Text == "" {
					dialog.ShowError(errors.New("❌ 路径为空"), w)
					return
				}
				if err := testPath(addrEnt.Text, pEnt.Text); err != nil {
					dialog.ShowError(err, w)
				} else {
					dialog.ShowInformation("成功", "路径可达", w)
				}
			})

			var pRow *fyne.Container
			delPBtn := widget.NewButtonWithIcon("", theme.DeleteIcon(), func() { pathsBox.Remove(pRow) })
			pRow = container.NewGridWithColumns(4, lEnt, pEnt, testPathBtn, delPBtn)
			pathsBox.Add(pRow)
		}
		for _, p := range srv.Paths {
			addPathRow(p.Label, p.Path)
		}

		var srvCard *widget.Card
		delSrvBtn := widget.NewButtonWithIcon("删除服务器", theme.DeleteIcon(), func() { serversContainer.Remove(srvCard) })
		addPBtn := widget.NewButton("添加路径", func() { addPathRow("", "") })

		content := container.NewVBox(
			container.NewBorder(nil, nil, widget.NewLabel("地址:"), testAddrBtn, addrEnt),
			widget.NewLabel("路径列表:"),
			pathsBox,
			container.NewHBox(addPBtn, delSrvBtn),
		)
		srvCard = widget.NewCard("", "", content)
		serversContainer.Add(srvCard)
	}
	for _, s := range appConfig.Servers {
		addServerRow(s)
	}

	serversBox := container.NewVBox(
		widget.NewLabelWithStyle("源服务器配置", fyne.TextAlignLeading, fyne.TextStyle{Bold: true}),
		serversContainer,
		widget.NewButtonWithIcon("添加服务器", theme.ContentAddIcon(), func() { addServerRow(ServerConfig{}) }),
	)

	// 底部按钮
	saveBtn := widget.NewButtonWithIcon("保存", theme.DocumentSaveIcon(), func() {
		// 如果为空，自动使用默认的 ./downloads
		dest := strings.TrimSpace(destServerEntry.Text)
		if dest == "" {
			dest = "./downloads"
		}

		newCfg := Config{
			LocalTempDir:      "./temp", // 固定使用当前运行目录下的 temp 文件夹
			DestServer:        dest,
			ArchiveNameFormat: archiveFmtEntry.Text,
		}
		// 收集工具
		for _, obj := range toolsContainer.Objects {
			row := obj.(*fyne.Container)
			e := row.Objects[0].(*widget.Entry).Text
			c := row.Objects[1].(*widget.Entry).Text
			if e != "" && c != "" {
				newCfg.ToolRules = append(newCfg.ToolRules, ToolRule{Ext: e, ToolCmd: c})
			}
		}
		// 收集服务器
		for _, obj := range serversContainer.Objects {
			card := obj.(*widget.Card)
			content := card.Content.(*fyne.Container)
			addr := content.Objects[0].(*fyne.Container).Objects[0].(*widget.Entry).Text
			if addr == "" {
				continue
			}
			pathsContainer := content.Objects[2].(*fyne.Container)
			var paths []ServerPath
			for _, pObj := range pathsContainer.Objects {
				pRow := pObj.(*fyne.Container)
				l := pRow.Objects[0].(*widget.Entry).Text
				p := pRow.Objects[1].(*widget.Entry).Text
				if l == "" {
					l = path.Base(p)
				}
				if l != "" && p != "" {
					paths = append(paths, ServerPath{Label: l, Path: p})
				}
			}
			newCfg.Servers = append(newCfg.Servers, ServerConfig{Address: addr, Paths: paths})
		}
		appConfig = newCfg
		saveConfig("config.json")
		initDirs()
		if onSaved != nil {
			onSaved()
		}
		w.Close()
	})

	cancelBtn := widget.NewButtonWithIcon("取消", theme.CancelIcon(), func() { w.Close() })

	w.SetContent(container.NewBorder(nil, container.NewHBox(saveBtn, cancelBtn), nil, nil,
		container.NewScroll(container.NewVBox(globalBox, toolsBox, serversBox))))
	w.Show()
}

// --- 4. UI 主逻辑 ---

func main() {
	myApp := app.New()
	myApp.SetIcon(resourceIconPng)
	myWindow := myApp.NewWindow("Pigeon Piper")
	myWindow.SetMaster()
	myWindow.Resize(fyne.NewSize(1100, 750))

	loadConfig("config.json")
	initDirs()
	cleanTempDirs()

	var (
		currentServerIdx int
		currentBasePath  string
		currentSubPath   string
		currentPathLabel string
		
		fetchFiles     func(string)
		refreshUI      func()
		refreshQueueUI func()
	)

	menuFile := fyne.NewMenu("文件",
		fyne.NewMenuItem("配置...", func() { showConfigWindow(refreshUI) }),
		fyne.NewMenuItem("退出", func() { myApp.Quit() }),
	)
	menuAbout := fyne.NewMenu("帮助",
		fyne.NewMenuItem("关于", func() {
			dialog.ShowInformation("关于", "Pigeon Piper\nA Professional Log Transport Tool\n版本: 1.0\n\n高效的日志下载与打包工具\n需要rsync>=3.0.9 tar>=1.26（支持xz）", myWindow)
		}),
	)

	myWindow.SetMainMenu(fyne.NewMainMenu(menuFile,menuAbout))

	tree := widget.NewTree(
		func(id widget.TreeNodeID) []widget.TreeNodeID {
			if id == "" {
				ids := make([]string, len(appConfig.Servers))
				for i := range appConfig.Servers {
					ids[i] = fmt.Sprintf("s|%d", i)
				}
				return ids
			}
			if strings.HasPrefix(id, "s|") {
				var idx int
				fmt.Sscanf(id, "s|%d", &idx)
				if idx >= len(appConfig.Servers) {
					return nil
				}
				ids := make([]string, len(appConfig.Servers[idx].Paths))
				for i := range appConfig.Servers[idx].Paths {
					ids[i] = fmt.Sprintf("p|%d|%d", idx, i)
				}
				return ids
			}
			return nil
		},
		func(id widget.TreeNodeID) bool { return id == "" || strings.HasPrefix(id, "s|") },
		func(branch bool) fyne.CanvasObject { return widget.NewLabel("Template") },
		func(id widget.TreeNodeID, branch bool, o fyne.CanvasObject) {
			l := o.(*widget.Label)
			if strings.HasPrefix(id, "s|") {
				var idx int
				fmt.Sscanf(id, "s|%d", &idx)
				if idx < len(appConfig.Servers) {
					l.SetText(getHostName(appConfig.Servers[idx].Address))
				}
			} else if strings.HasPrefix(id, "p|") {
				var sIdx, pIdx int
				fmt.Sscanf(id, "p|%d|%d", &sIdx, &pIdx)
				if sIdx < len(appConfig.Servers) && pIdx < len(appConfig.Servers[sIdx].Paths) {
					l.SetText(appConfig.Servers[sIdx].Paths[pIdx].Label)
				}
			}
		},
	)

	refreshUI = func() { tree.Refresh() }

	breadcrumbBox := container.NewHBox()
	breadcrumbScroll := container.NewScroll(breadcrumbBox)
	breadcrumbScroll.SetMinSize(fyne.NewSize(100, 36))
	breadcrumbScroll.Direction = container.ScrollHorizontalOnly

	loadingActivity := widget.NewActivity()
	var fileTable *widget.Table

	updateBreadcrumbs := func(srvAddr, label, subPath string) {
		breadcrumbBox.Objects = nil

		hostName := getHostName(srvAddr)
		hostLabel := widget.NewLabel(hostName + ":")
		hostLabel.TextStyle = fyne.TextStyle{Bold: true}
		breadcrumbBox.Add(hostLabel)

		rootBtn := widget.NewButton("["+label+"]", func() { fetchFiles("") })
		// rootBtn.Importance = widget.LowImportance
		breadcrumbBox.Add(rootBtn)

		if subPath != "" && subPath != "." {
			parts := strings.Split(subPath, "/")
			accumulatedPath := ""
			for _, part := range parts {
				if part == "" { continue }
				
				if accumulatedPath == "" {
					accumulatedPath = part
				} else {
					accumulatedPath = path.Join(accumulatedPath, part)
				}
				
				target := accumulatedPath
				partBtn := widget.NewButton(part, func() { fetchFiles(target) })
				// partBtn.Importance = widget.LowImportance
				breadcrumbBox.Add(partBtn)
			}
		}
		breadcrumbBox.Refresh()
		breadcrumbScroll.ScrollToBottom()
	}

	fetchFiles = func(targetSubPath string) {
		if currentServerIdx < 0 || currentServerIdx >= len(appConfig.Servers) {
			return
		}

		loadingActivity.Start()

		go func() {
			defer fyne.Do(func() { loadingActivity.Stop() })

			srv := appConfig.Servers[currentServerIdx]
			fullPath := path.Join(currentBasePath, targetSubPath)
			remoteAddr := fmt.Sprintf("%s:%s/", srv.Address, fullPath)

			cmd := exec.Command("rsync", "--list-only", remoteAddr)
			outBytes, err := cmd.Output()

			fyne.Do(func() {
				if err != nil {
					dialog.ShowError(errors.New("读取目录失败: "+err.Error()), myWindow)
					return
				}
				
				tree.UnselectAll()
				currentSubPath = targetSubPath
				updateBreadcrumbs(srv.Address, currentPathLabel, currentSubPath)

				var newList []RemoteFile
				
				if currentSubPath != "" && currentSubPath != "." {
					newList = append(newList, RemoteFile{
						Name: "..", 
						IsDir: true, 
						IsBack: true,
						Date: "-",
						Size: "-",
					})
				}

				scanner := bufio.NewScanner(strings.NewReader(string(outBytes)))
				for scanner.Scan() {
					if f := parseRsyncLine(scanner.Text()); f != nil {
						newList = append(newList, *f)
					}
				}
				currentFileList = newList
				tableData = make([]RemoteFile, len(currentFileList))
				copy(tableData, currentFileList)

				sort.Slice(tableData, func(i, j int) bool {
					if tableData[i].IsBack { return true }
					if tableData[j].IsBack { return false }
					if tableData[i].IsDir != tableData[j].IsDir {
						return tableData[i].IsDir
					}
					return tableData[i].Name < tableData[j].Name
				})

				fileTable.Refresh()
				fileTable.ScrollToTop()
			})
		}()
	}

	tree.OnSelected = func(id widget.TreeNodeID) {
		if strings.HasPrefix(id, "p|") {
			var sIdx, pIdx int
			fmt.Sscanf(id, "p|%d|%d", &sIdx, &pIdx)
			currentServerIdx = sIdx
			currentBasePath = appConfig.Servers[sIdx].Paths[pIdx].Path
			currentPathLabel = appConfig.Servers[sIdx].Paths[pIdx].Label

			fetchFiles("")
		}
	}

	fileTable = widget.NewTableWithHeaders(
		func() (int, int) { return len(tableData), 4 },
		func() fyne.CanvasObject { return container.NewStack(widget.NewLabel("Template")) },
		func(id widget.TableCellID, o fyne.CanvasObject) {
			if id.Row >= len(tableData) || id.Row < 0 {
				return
			}
			item := tableData[id.Row]
			cell := o.(*fyne.Container)
			cell.Objects = nil
			switch id.Col {
			case 0:
				icon := theme.FileIcon()
				if item.IsDir { icon = theme.FolderIcon() }
				if item.IsBack { icon = theme.NavigateBackIcon() }
				
				var leftWidget fyne.CanvasObject
				if !item.IsDir && !item.IsBack {
					check := widget.NewCheck("", func(b bool) { tableData[id.Row].Checked = b })
					check.Checked = item.Checked
					leftWidget = check
				} else {
					leftWidget = layout.NewSpacer()
				}
				l := widget.NewLabel(item.Name)
				l.TextStyle = fyne.TextStyle{Bold: item.IsDir}
				cell.Add(container.NewBorder(nil, nil, container.NewHBox(leftWidget, widget.NewIcon(icon)), nil, l))
			case 1: cell.Add(widget.NewLabel(item.Size))
			case 2:
				t := "文件"
				if item.IsBack { t = "上级目录" } else if item.IsDir { t = "文件夹" }
				cell.Add(widget.NewLabel(t))
			case 3: cell.Add(widget.NewLabel(item.Date))
			}
		},
	)

	// 隐藏左侧行头
	fileTable.ShowHeaderColumn = false
	
	fileTable.CreateHeader = func() fyne.CanvasObject { return widget.NewLabel("Header") }
	
	fileTable.UpdateHeader = func(id widget.TableCellID, o fyne.CanvasObject) {
		l := o.(*widget.Label)
		l.TextStyle = fyne.TextStyle{Bold: true}
		headers := []string{"名称", "大小", "类型", "修改时间"}
		if id.Col >= 0 && id.Col < len(headers) {
			l.SetText(headers[id.Col])
		} else {
			l.SetText("") 
		}
	}
	fileTable.SetColumnWidth(0, 450)

	fileTable.OnSelected = func(id widget.TableCellID) {
		fileTable.Unselect(id)
		if id.Row < 0 || id.Row >= len(tableData) { return }
		
		item := tableData[id.Row]
		if item.IsBack {
			parent := path.Dir(currentSubPath)
			if parent == "." { parent = "" }
			fetchFiles(parent)
		} else if item.IsDir {
			newSub := path.Join(currentSubPath, item.Name)
			fetchFiles(newSub)
		} else {
			tableData[id.Row].Checked = !tableData[id.Row].Checked
			fileTable.Refresh()
		}
	}

	refreshBtn := widget.NewButtonWithIcon("", theme.ViewRefreshIcon(), func() { fetchFiles(currentSubPath) })

	addQueueBtn := widget.NewButtonWithIcon("添加到任务列表", theme.ContentAddIcon(), func() {
		queueLock.Lock()
		defer queueLock.Unlock()
		
		count := 0
		dupCount := 0
		srv := appConfig.Servers[currentServerIdx]
		host := getHostName(srv.Address)

		for _, f := range tableData {
			if f.Checked {
				displayP := fmt.Sprintf("[%s] %s", host, path.Join(currentPathLabel, currentSubPath, f.Name))

				isDuplicate := false
				for _, q := range downloadQueue {
					if q.ServerAddress == srv.Address && 
					   q.RemoteBasePath == currentBasePath &&
					   q.SubPath == currentSubPath &&
					   q.FileName == f.Name {
						   isDuplicate = true
						   break
					   }
				}

				if isDuplicate {
					dupCount++
					continue
				}

				item := &FileItem{
					ID:             fmt.Sprintf("%d-%s", time.Now().UnixNano(), f.Name),
					ServerAddress:  srv.Address,
					HostName:       host,
					PathLabel:      currentPathLabel,
					RemoteBasePath: currentBasePath,
					SubPath:        currentSubPath,
					FileName:       f.Name,
					DisplayPath:    displayP,
					Status:         StatusPending,
					Progress:       0,
				}
				downloadQueue = append(downloadQueue, item)
				count++
			}
		}
		
		if count > 0 || dupCount > 0 {
			for i := range tableData { tableData[i].Checked = false }
			fileTable.Refresh()
			refreshQueueUI()
			
			msg := fmt.Sprintf("已添加 %d 个文件", count)
			if dupCount > 0 {
				msg += fmt.Sprintf("\n(跳过 %d 个重复文件)", dupCount)
			}
			dialog.ShowInformation("操作完成", msg, myWindow)
		}
	})

	topToolbar := container.NewBorder(nil, nil,
		container.NewHBox(addQueueBtn, refreshBtn, loadingActivity),
		nil,
		breadcrumbScroll)

	fileTabContent := container.NewHSplit(
		container.NewBorder(widget.NewLabel("服务器列表"), nil, nil, nil, tree),
		container.NewBorder(topToolbar, nil, nil, nil, fileTable),
	)
	fileTabContent.SetOffset(0.25)

	// --- 传输任务页 ---

	queueContainer := container.NewVBox()
	queueScroll := container.NewScroll(queueContainer)

	statusLabel := widget.NewLabel("就绪")
	statusLabel.TextStyle = fyne.TextStyle{Bold: true}
	totalProg := widget.NewProgressBar()
	archiveType := widget.NewSelect([]string{".tar.gz", ".tar.xz"}, nil)
	archiveType.SetSelected(".tar.gz")

	refreshQueueUI = func() {
		fyne.Do(func() {
			queueContainer.Objects = nil
			queueLock.Lock()
			itemsToDraw := make([]*FileItem, len(downloadQueue))
			copy(itemsToDraw, downloadQueue)
			queueLock.Unlock()

			if len(itemsToDraw) == 0 {
				queueContainer.Add(widget.NewLabel("任务列表为空"))
			} else {
				for _, item := range itemsToDraw {
					it := item
					if it.StatusLabel == nil {
						it.StatusLabel = widget.NewLabel(it.Status)
					}
					it.StatusLabel.SetText(it.Status)

					if it.ProgressBar == nil {
						it.ProgressBar = widget.NewProgressBar()
						it.ProgressBar.Max = 1.0
					}
					it.ProgressBar.SetValue(it.Progress)

					delBtn := widget.NewButtonWithIcon("", theme.DeleteIcon(), func() {
						if isProcessing { return }
						destPath := filepath.Join(downloadDir, it.HostName, it.PathLabel, it.SubPath, it.FileName)
						removeLocalFile(destPath)

						queueLock.Lock()
						var newQ []*FileItem
						for _, q := range downloadQueue {
							if q.ID != it.ID {
								newQ = append(newQ, q)
							}
						}
						downloadQueue = newQ
						queueLock.Unlock()
						refreshQueueUI()
					})

					infoLine := container.NewBorder(nil, nil, it.StatusLabel, delBtn,
						widget.NewLabelWithStyle(it.DisplayPath, fyne.TextAlignLeading, fyne.TextStyle{Monospace: true}))

					queueContainer.Add(container.NewVBox(infoLine, it.ProgressBar, widget.NewSeparator()))
				}
			}
			queueContainer.Refresh()
		})
	}

	startTask := func() {
		queueLock.Lock()
		if len(downloadQueue) == 0 {
			queueLock.Unlock()
			return
		}
		queueLock.Unlock()

		if isProcessing { return }
		isProcessing = true
		mainCtx, mainCancel = context.WithCancel(context.Background())
		fyne.Do(func() { statusLabel.SetText("开始任务...") })

		go func() {
			defer func() {
				isProcessing = false
				if mainCtx.Err() != nil {
					fyne.Do(func() { statusLabel.SetText("任务已停止") })
				}
			}()

			totalFiles := len(downloadQueue)
			for i, item := range downloadQueue {
				if mainCtx.Err() != nil { return }
				
				currentItem := item
				if currentItem.Status == StatusCompleted {
					continue
				}

				destDir := filepath.Join(downloadDir, currentItem.HostName, currentItem.PathLabel, currentItem.SubPath)
				localFilePath := filepath.Join(destDir, currentItem.FileName)

				if !isSafePath(downloadDir, localFilePath) {
					fyne.Do(func() { currentItem.StatusLabel.SetText("非法路径") })
					continue
				}
				os.MkdirAll(destDir, 0755)
				remotePath := path.Join(currentItem.RemoteBasePath, currentItem.SubPath, currentItem.FileName)
				remoteSrc := fmt.Sprintf("%s:%s", currentItem.ServerAddress, remotePath)

				// 1. 下载阶段
				if currentItem.Status == StatusPending {
					fyne.Do(func() {
						currentItem.Status = StatusDownloading
						currentItem.StatusLabel.SetText(StatusDownloading)
						statusLabel.SetText(fmt.Sprintf("正在下载 (%d/%d): %s", i+1, totalFiles, currentItem.FileName))
					})

					cmd := exec.CommandContext(mainCtx, "rsync", "-az", "--append-verify", remoteSrc, localFilePath)
					if err := cmd.Run(); err != nil {
						fmt.Println("下载失败:", err)
						continue
					}
					
					// 下载完成后将进度置为一半，准备进入转换检查
					fyne.Do(func() {
						currentItem.Progress = 0.5
						currentItem.ProgressBar.SetValue(0.5)
					})
				}

				// 2. 转换阶段与完成判定
				if currentItem.Status == StatusDownloading || currentItem.Status == StatusConverting {
					// 寻找匹配的转换规则
					match := false
					var matchedRule ToolRule
					for _, rule := range appConfig.ToolRules {
						if strings.HasSuffix(currentItem.FileName, rule.Ext) || strings.Contains(currentItem.FileName, rule.Ext+".") {
							match = true
							matchedRule = rule
							break
						}
					}

					if match {
						fyne.Do(func() {
							currentItem.Status = StatusConverting
							currentItem.StatusLabel.SetText(StatusConverting)
						})

						outFile := localFilePath + ".txt"
						cmdParts := strings.Fields(matchedRule.ToolCmd)
						if len(cmdParts) > 0 {
							c := exec.CommandContext(mainCtx, cmdParts[0], cmdParts[1:]...)
							if fIn, err := os.Open(localFilePath); err == nil {
								if fOut, err := os.Create(outFile); err == nil {
									c.Stdin = fIn
									c.Stdout = fOut
									c.Run()
									fOut.Close()
								}
								fIn.Close()
							}
						}
					}

					// 无论是否匹配转换规则，此时文件处理已结束
					fyne.Do(func() {
						currentItem.Status = StatusCompleted
						currentItem.StatusLabel.SetText(StatusCompleted)
						currentItem.Progress = 1.0
						currentItem.ProgressBar.SetValue(1.0)
						totalProg.SetValue(float64(i+1) / float64(totalFiles) * 0.6)
					})
				}
			}

			if mainCtx.Err() != nil { return }

			fyne.Do(func() {
				statusLabel.SetText("正在打包...")
				totalProg.SetValue(0.65)
			})
			cleanEmptyDirs(downloadDir)

			baseName := formatFileName(appConfig.ArchiveNameFormat)
			finalName := baseName + archiveType.Selected
			archivePath := filepath.Join(archiveDir, finalName)
			os.Remove(archivePath)

			tarArgs := []string{"-cf", archivePath, "-C", downloadDir, "."}
			if archiveType.Selected == ".tar.gz" {
				tarArgs = append([]string{"-z"}, tarArgs...)
			}
			if archiveType.Selected == ".tar.xz" {
				tarArgs = append([]string{"-J"}, tarArgs...)
			}

			tCmd := exec.CommandContext(mainCtx, "tar", tarArgs...)
			if err := tCmd.Run(); err != nil {
				fyne.Do(func() {
					statusLabel.SetText("打包失败")
					dialog.ShowError(err, myWindow)
				})
				return
			}
			fyne.Do(func() { totalProg.SetValue(0.8) })

			// if appConfig.DestServer != "" {
				// fyne.Do(func() { statusLabel.SetText("正在上传...") })
				// uCmd := exec.CommandContext(mainCtx, "rsync", "--partial", "--append-verify", archivePath, appConfig.DestServer)
				// if err := uCmd.Run(); err != nil {
					// fyne.Do(func() {
						// statusLabel.SetText("上传失败")
						// dialog.ShowError(err, myWindow)
					// })
					// return
				// }
				// fyne.Do(func() {
					// statusLabel.SetText("已发送: " + finalName)
					// totalProg.SetValue(1.0)
					// dialog.ShowInformation("成功", "文件已发送", myWindow)
				// })
			// } else {
				// fyne.Do(func() {
					// statusLabel.SetText("已打包 (未上传)" + finalName)
					// totalProg.SetValue(1.0)
					// dialog.ShowInformation("成功", "文件已打包", myWindow)
				// })
			// }
			dest := strings.TrimSpace(appConfig.DestServer)
			if dest == "" {
				dest = "./downloads"
			}

			isRemote := strings.Contains(dest, "@") && strings.Contains(dest, ":")
			
			if isRemote {
				// 远程地址：使用 rsync 上传
				fyne.Do(func() { statusLabel.SetText("正在上传...") })
				uCmd := exec.CommandContext(mainCtx, "rsync", "--partial", "--append-verify", archivePath, dest)
				if err := uCmd.Run(); err != nil {
					fyne.Do(func() {
						statusLabel.SetText("上传失败")
						dialog.ShowError(err, myWindow)
					})
					return
				}
				fyne.Do(func() {
					statusLabel.SetText("已发送: " + finalName)
					totalProg.SetValue(1.0)
					dialog.ShowInformation("成功", "文件已发送", myWindow)
				})
			} else {
				// 本地路径：创建目录并移动文件
				fyne.Do(func() { statusLabel.SetText("正在保存到本地...") })
				
				// 1. 确保本地目录存在
				if err := os.MkdirAll(dest, 0755); err != nil {
					fyne.Do(func() {
						statusLabel.SetText("创建目录失败")
						dialog.ShowError(fmt.Errorf("无法创建本地目录: %v", err), myWindow)
					})
					return
				}

				// 2. 计算目标文件的完整路径
				targetFilePath := filepath.Join(dest, filepath.Base(archivePath))

				// 3. 执行 mv 命令
				mvCmd := exec.CommandContext(mainCtx, "mv", archivePath, targetFilePath)
				if err := mvCmd.Run(); err != nil {
					// 备用方案：如果系统 mv 命令由于某些原因失败，尝试使用 Go 原生方法
					if errOs := os.Rename(archivePath, targetFilePath); errOs != nil {
						fyne.Do(func() {
							statusLabel.SetText("移动失败")
							dialog.ShowError(fmt.Errorf("移动文件失败: %v", errOs), myWindow)
						})
						return
					}
				}
				
				fyne.Do(func() {
					statusLabel.SetText("已保存" + finalName)
					totalProg.SetValue(1.0)
					dialog.ShowInformation("成功", fmt.Sprintf("文件已保存至本地:\n%s", targetFilePath), myWindow)
				})
			}
		}()
	}

	taskCtrlBox := container.NewVBox(
		widget.NewSeparator(),
		container.NewBorder(nil, nil, widget.NewLabel("压缩格式:"), nil, archiveType),
		statusLabel,
		totalProg,
		container.NewHBox(
			widget.NewButtonWithIcon("开始", theme.MediaPlayIcon(), func() { go startTask() }),
			widget.NewButtonWithIcon("停止", theme.MediaStopIcon(), func() {
				if mainCancel != nil {
					mainCancel()
				}
			}),
			layout.NewSpacer(),
			widget.NewButtonWithIcon("清除已完成", theme.ContentClearIcon(), func() {
				if isProcessing { return }

				queueLock.Lock()
				var activeQ []*FileItem
				for _, item := range downloadQueue {
					// 修改清除条件为 StatusCompleted
					if item.Status != StatusCompleted {
						activeQ = append(activeQ, item)
					} else {
						p := filepath.Join(downloadDir, item.HostName, item.PathLabel, item.SubPath, item.FileName)
						removeLocalFile(p)
					}
				}
				downloadQueue = activeQ
				queueLock.Unlock()

				cleanEmptyDirs(downloadDir)

				refreshQueueUI()
				statusLabel.SetText("就绪")
				totalProg.SetValue(0)
			}),
		),
	)

	dlSplit := container.NewVSplit(
		queueScroll,
		taskCtrlBox,
	)
	dlSplit.SetOffset(0.7)

	tabs := container.NewAppTabs(
		container.NewTabItemWithIcon("浏览文件", theme.FolderIcon(), fileTabContent),
		container.NewTabItemWithIcon("传输任务", theme.DownloadIcon(), dlSplit),
	)

	myWindow.SetContent(tabs)
	myWindow.ShowAndRun()
}