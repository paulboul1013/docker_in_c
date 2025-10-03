# Simple Container Implementation in C

一個用 C 語言實現的簡單容器程式，可以運行隔離的 bash 環境。

## 功能特性

- ✅ **進程隔離**: 使用 PID 命名空間隔離進程
- ✅ **文件系統隔離**: 使用 chroot 和掛載命名空間
- ✅ **主機名隔離**: 使用 UTS 命名空間
- ✅ **IPC 隔離**: 使用 IPC 命名空間
- ✅ **完整的基本指令集**: ls, cat, ps, top, htop, grep, find, vim, nano, man 等
- ✅ **終端支援**: 完整的 terminfo 和 devpts 支援

## 編譯

```bash
make
```

## 運行

```bash
sudo ./main
```

注意：需要 root 權限來創建命名空間和執行 chroot。

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
2. 在子進程中設置容器根目錄和基本文件系統結構
3. 使用 `chroot()` 改變根目錄實現文件系統隔離
4. 掛載必要的文件系統（proc, sys, devpts）
5. 執行 /bin/bash

## 技術細節

- **命名空間**: CLONE_NEWPID, CLONE_NEWNS, CLONE_NEWUTS, CLONE_NEWIPC
- **文件系統隔離**: chroot + mount
- **終端設備**: /dev/pts, /dev/tty, /dev/console
- **依賴複製**: 自動複製二進制文件及其依賴庫

## 限制

這是一個簡化的實現，不包含以下功能：
- 網路隔離（CLONE_NEWNET）
- 資源限制（cgroups）
- 用戶命名空間（CLONE_NEWUSER）
- 安全性加強（seccomp, AppArmor）
- 鏡像管理

## 作者

paulboul1013

## 許可證

MIT License

