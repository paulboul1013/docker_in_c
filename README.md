# Simple Container Implementation in C

一個用 C 語言實現的簡單容器程式，可以運行隔離的 bash 環境。

## 功能特性

### 核心功能
- ✅ **進程隔離**: 使用 PID 命名空間隔離進程
- ✅ **文件系統隔離**: 使用 chroot 和掛載命名空間
- ✅ **主機名隔離**: 使用 UTS 命名空間
- ✅ **IPC 隔離**: 使用 IPC 命名空間
- ✅ **用戶隔離**: 使用用戶命名空間（USER namespace）進行權限隔離
- ✅ **資源限制**: 使用 cgroups 限制 CPU、記憶體和進程數
- ✅ **多容器支持**: 可同時運行多個獨立容器

### 環境和工具
- ✅ **完整的基本指令集**: ls, cat, ps, top, htop, grep, find, vim, nano, man 等
- ✅ **終端支援**: 完整的 terminfo 和 devpts 支援
- ✅ **設備文件**: /dev/null, /dev/zero, /dev/random 等
- ✅ **系統文件**: /etc/passwd, /etc/group, /etc/hostname
- ✅ **Bash 配置**: 別名、環境變量、清屏腳本

## 編譯

```bash
make
```

## 運行

### 單容器模式
```bash
sudo ./main
```

### 多容器模式
可以在多個終端同時啟動多個容器：
```bash
# 終端 1
sudo ./main

# 終端 2
sudo ./main

# 終端 3
sudo ./main
```

每個容器都有：
- 唯一的容器 ID
- 獨立的根目錄 `/tmp/container_root_<ID>`
- 獨立的 cgroup `docker_in_c_container_<ID>`
- 完全隔離的命名空間
## 已安裝的指令

### 基本文件操作
- ls, cat, cp, mv, rm, mkdir, find, grep, pwd

### 系統監控
- top, htop, ps, free, uptime, df, du

### 文本編輯
- vim, nano, less, more

### 文本處理
- head, tail, sort, uniq, wc, grep

### 網路工具
- curl, wget

### 壓縮工具
- tar, gzip, gunzip, zcat

### 其他工具
- man, which, whoami, id, env, clear, sleep

## 實現原理

1. 使用 `clone()` 系統調用創建帶有新命名空間的子進程
2. 設置用戶命名空間 UID/GID 映射，實現權限隔離
3. 設置 cgroups 資源限制（CPU、記憶體、進程數）
4. 在子進程中設置容器根目錄和基本文件系統結構
5. 使用 `chroot()` 改變根目錄實現文件系統隔離
6. 掛載必要的文件系統（proc, sys, devpts）
7. 執行 /bin/bash

## 技術細節

- **命名空間**: CLONE_NEWPID, CLONE_NEWNS, CLONE_NEWUTS, CLONE_NEWIPC, CLONE_NEWUSER
- **用戶映射**: 將容器內的 root (UID 0) 映射到主機當前用戶，提供安全隔離
- **資源限制**: cgroups v1/v2 自動檢測和配置
  - 記憶體限制: 512 MB
  - CPU 配額: 50% (可配置)
  - 最大進程數: 100
- **文件系統隔離**: chroot + mount
- **終端設備**: /dev/pts, /dev/tty, /dev/console
- **依賴複製**: 自動複製二進制文件及其依賴庫

## 資源限制配置

程式會自動檢測系統使用的 cgroup 版本（v1 或 v2），並相應配置資源限制。

### 預設限制

```c
cgroup_limits_t limits = {
    .memory_limit_mb = 512,    // 記憶體限制 512 MB
    .cpu_shares = 512,         // CPU 份額（預設 1024 的一半）
    .cpu_quota_us = 50000,     // CPU 配額 50%
    .pids_max = 100            // 最多 100 個進程
};
```

### 自訂限制

您可以在 `main.c` 的 `main()` 函數中修改 `cgroup_limits_t` 結構來調整資源限制：

- **memory_limit_mb**: 記憶體限制（MB），設為 0 表示不限制
- **cpu_shares**: CPU 份額（範圍 2-262144，預設 1024）
- **cpu_quota_us**: CPU 配額（微秒/100ms），50000 = 50%
- **pids_max**: 最大進程數，設為 0 表示不限制

### 驗證資源限制

進入容器後，可以使用以下指令查看資源使用情況：

```bash
# 查看記憶體使用
free -h

# 查看進程列表
ps aux

# 查看系統資源
top
```

## 測試功能

### 快速測試
```bash
# 在容器內執行
id                          # 查看用戶身份
whoami                      # 應顯示 root
cat /proc/self/uid_map      # 查看 UID 映射
ls -la /etc/passwd          # 查看系統文件
echo test > /tmp/test.txt   # 測試文件操作
clear                       # 測試清屏
```



### 測試用戶命名空間映射
```bash
# 在容器內創建文件
touch /tmp/usertest
ls -la /tmp/usertest        # 顯示所有者為 root

# 在主機查看（另一個終端）
ls -la /tmp/container_root_*/tmp/usertest
# 顯示所有者為 paulboul1013 (1000) ✓
```

## 限制

這是一個簡化的實現，不包含以下功能：
- 網路隔離（CLONE_NEWNET）
- 安全性加強（seccomp, AppArmor）
- 鏡像管理
- 持久化存儲（volumes）


## 專案結構

```
docker_in_c/
├── main.c                      # 主程式源代碼
├── Makefile                    # 編譯配置
├── README.md                   # 本文件
```

## 作者

paulboul1013



