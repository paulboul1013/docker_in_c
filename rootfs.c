#include "rootfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>

// 複製 terminfo 資料庫
void terminfo_copy(const char* container_root) {
    char cmd[1024];
    
    // 嘗試從多個可能的位置複製 terminfo
    snprintf(cmd, sizeof(cmd), "cp -r /lib/terminfo %s/lib/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /etc/terminfo %s/etc/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 從 /usr/share/terminfo 複製
    snprintf(cmd, sizeof(cmd), "cp /usr/share/terminfo/l/linux %s/usr/share/terminfo/l/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/terminfo/x/xterm %s/usr/share/terminfo/x/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/terminfo/x/xterm-256color %s/usr/share/terminfo/x/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/terminfo/v/vt100 %s/usr/share/terminfo/v/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 如果上述都失敗，從 /lib/terminfo 複製 (某些系統的位置)
    snprintf(cmd, sizeof(cmd), "cp /lib/terminfo/l/linux %s/lib/terminfo/l/linux 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /lib/terminfo/x/xterm %s/lib/terminfo/x/xterm 2>/dev/null || true", container_root);
    system(cmd);

    // 複製 terminfo 資料庫 (讓 top, htop 等能正常顯示)
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/terminfo", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/terminfo/* %s/usr/share/terminfo/ 2>/dev/null || true", container_root);
    system(cmd);
}

// 創建基本的系統文件
void create_basic_system_files(const char* container_root) {
    char cmd[1024];
    char path[512];
    FILE* file;
    
    // 創建 /etc/passwd
    snprintf(path, sizeof(path), "%s/etc/passwd", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "root:x:0:0:root:/root:/bin/bash\n");
        fprintf(file, "_apt:x:100:65534::/nonexistent:/usr/sbin/nologin\n");  // apt 沙箱用戶
        fprintf(file, "nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin\n");
        fclose(file);
    }
    
    // 創建 /etc/group
    snprintf(path, sizeof(path), "%s/etc/group", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "root:x:0:\n");
        fprintf(file, "nogroup:x:65534:\n");
        fclose(file);
    }
    
    // 創建 /etc/hostname
    snprintf(path, sizeof(path), "%s/etc/hostname", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "container\n");
        fclose(file);
    }
    
    // 複製基本的系統庫
    snprintf(cmd, sizeof(cmd), "cp -r /lib/x86_64-linux-gnu/* %s/lib/x86_64-linux-gnu/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/lib/x86_64-linux-gnu/* %s/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /lib64/ld-linux-x86-64.so.* %s/lib64/ 2>/dev/null || true", container_root);
    system(cmd);
}

// 創建設備文件
void device_copy(const char* container_root) {
    char cmd[1024];
    
    // 創建一些必要的設備文件
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/null c 1 3 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/zero c 1 5 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/random c 1 8 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/urandom c 1 9 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/tty c 5 0 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mknod %s/dev/console c 5 1 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製當前終端設備到容器中
    snprintf(cmd, sizeof(cmd), "cp -a /dev/pts %s/dev/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 設置設備文件權限
    snprintf(cmd, sizeof(cmd), "chmod 666 %s/dev/null %s/dev/zero %s/dev/tty %s/dev/console 2>/dev/null || true",
             container_root, container_root, container_root, container_root);
    system(cmd);
}

// 複製 vim 配置和運行時文件
void vim_copy(const char* container_root) {
    char cmd[1024];
    char path[512];
    FILE *file;
    
    // 複製 vim 運行時文件
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/vim", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/vim/vim* %s/usr/share/vim/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 創建 vim 配置目錄
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/etc/vim", container_root);
    system(cmd);
    
    // 創建簡單的 vimrc 配置文件
    snprintf(path, sizeof(path), "%s/etc/vim/vimrc", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "set nocompatible\n");
        fprintf(file, "set backspace=indent,eol,start\n");
        fprintf(file, "syntax on\n");
        fprintf(file, "set background=dark\n");
        fprintf(file, "set number\n");
        fclose(file);
    }
    
    // 創建用戶級別的 vimrc
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/root", container_root);
    system(cmd);
    
    snprintf(path, sizeof(path), "%s/root/.vimrc", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "set nocompatible\n");
        fprintf(file, "set backspace=indent,eol,start\n");
        fclose(file);
    }
}

// 複製指令和其依賴庫
void copy_command_with_libs(const char* cmd_path, const char* dest_name, const char* container_root) {
    char copy_cmd[512];
    char lib_cmd[1024];
    
    // 複製指令本身
    snprintf(copy_cmd, sizeof(copy_cmd), 
             "cp %s %s/bin/%s 2>/dev/null && chmod +x %s/bin/%s", 
             cmd_path, container_root, dest_name, container_root, dest_name);
    system(copy_cmd);
    
    // 複製指令需要的庫文件
    snprintf(lib_cmd, sizeof(lib_cmd),
             "ldd %s 2>/dev/null | grep -o '/lib[^ ]*' | while read lib; do "
             "if [ -f \"$lib\" ]; then "
             "mkdir -p %s/$(dirname \"$lib\") 2>/dev/null; "
             "cp \"$lib\" %s/\"$lib\" 2>/dev/null; "
             "fi; done", cmd_path, container_root, container_root);
    system(lib_cmd);
}

// 複製 man 指令及其相關文件
void man_command_copy(const char* container_root) {
    char cmd[1024];
    
    // 複製 man-db 相關的庫文件
    snprintf(cmd, sizeof(cmd), "cp /usr/lib/man-db/libmandb-*.so %s/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/lib/man-db", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/lib/man-db/* %s/usr/lib/man-db/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 man-db 的輔助程序及其依賴
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/libexec/man-db", container_root);
    system(cmd);
    
    // 複製並設置權限
    char *man_helpers[] = {
        "/usr/libexec/man-db/zsoelim",
        "/usr/libexec/man-db/manconv", 
        "/usr/libexec/man-db/globbing",
        "/usr/libexec/man-db/mandb",
        NULL
    };
    
    for (int i = 0; man_helpers[i]; i++) {
        char helper_cmd[1024];
        // 複製程序本身
        snprintf(helper_cmd, sizeof(helper_cmd), 
                "cp %s %s/usr/libexec/man-db/ 2>/dev/null && chmod +x %s/usr/libexec/man-db/$(basename %s)",
                man_helpers[i], container_root, container_root, man_helpers[i]);
        system(helper_cmd);
        
        // 複製其依賴庫
        snprintf(helper_cmd, sizeof(helper_cmd),
                "ldd %s 2>/dev/null | grep -o '/lib[^ ]*' | while read lib; do "
                "if [ -f \"$lib\" ]; then "
                "mkdir -p %s/$(dirname \"$lib\"); "
                "cp \"$lib\" %s/\"$lib\" 2>/dev/null; "
                "fi; done",
                man_helpers[i], container_root, container_root);
        system(helper_cmd);
    }
    
    // 複製 man 頁面文檔（選擇性複製常用的）
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/man/man1", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/man/man5", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/man/man8", container_root);
    system(cmd);
    
    // 複製常用指令的 man 頁面
    char *man_pages[] = {
        "ls", "cat", "grep", "find", "ps", "top", "bash", "cp", "mv", "rm",
        "chmod", "chown", "curl", "wget", "vim", "nano", "tar", "gzip","man",
        NULL
    };
    
    for (int i = 0; man_pages[i]; i++) {
        char page_cmd[1024];
        snprintf(page_cmd, sizeof(page_cmd), 
                "cp /usr/share/man/man1/%s.1.gz %s/usr/share/man/man1/ 2>/dev/null || true",
                man_pages[i], container_root);
        system(page_cmd);
    }
    
    // 複製 man 的配置文件
    snprintf(cmd, sizeof(cmd), "cp /etc/manpath.config %s/etc/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 groff 資料文件 (man 需要這些來格式化文檔)
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/groff", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/groff/* %s/usr/share/groff/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 groff 的字體文件
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/groff/current", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/groff/current/* %s/usr/share/groff/current/ 2>/dev/null || true", container_root);
    system(cmd);
}

// 設置 bash 別名和環境變量
void set_alias(const char* container_root) {
    char path[512];
    FILE *file;
    
    // 創建簡單的 bashrc 來設置別名
    snprintf(path, sizeof(path), "%s/etc/bash.bashrc", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "alias ll=\"ls -la\"\n");
        fprintf(file, "export TERM=xterm\n");
        fprintf(file, "export TERMINFO=/usr/share/terminfo:/lib/terminfo:/etc/terminfo\n");
        fclose(file);
    }
}

// 複製 apt-get 及其相關工具和配置
void apt_get_copy(const char* container_root) {
    char cmd[1024];
    char path[512];
    
    // 創建 apt 相關目錄結構
    char *apt_dirs[] = {
        "etc/apt",
        "etc/apt/apt.conf.d",
        "etc/apt/preferences.d",
        "etc/apt/sources.list.d",
        "etc/apt/trusted.gpg.d",
        "var/lib/apt",
        "var/lib/apt/lists",
        "var/lib/apt/lists/partial",
        "var/cache/apt",
        "var/cache/apt/archives",
        "var/cache/apt/archives/partial",
        "var/lib/dpkg",
        "var/lib/dpkg/info",
        "var/lib/dpkg/updates",
        "var/lib/dpkg/triggers",
        "usr/share/keyrings",
        NULL
    };
    
    for (int i = 0; apt_dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", container_root, apt_dirs[i]);
        mkdir(path, 0755);
    }
    
    // 複製 apt-get 相關命令
    struct {
        const char* source_path;
        const char* dest_name;
    } apt_commands[] = {
        {"/usr/bin/apt-get", "apt-get"},
        {"/usr/bin/apt", "apt"},
        {"/usr/bin/apt-cache", "apt-cache"},
        {"/usr/bin/apt-config", "apt-config"},
        // apt-key 是腳本，稍後單獨處理
        {"/usr/bin/dpkg", "dpkg"},
        {"/usr/bin/dpkg-deb", "dpkg-deb"},
        {"/usr/bin/dpkg-query", "dpkg-query"},
        {"/usr/bin/dpkg-split", "dpkg-split"},
        {"/usr/bin/dpkg-divert", "dpkg-divert"},
        {"/usr/bin/dpkg-statoverride", "dpkg-statoverride"},
        {"/usr/bin/gpg", "gpg"},
        {"/usr/bin/gpgv", "gpgv"},
        {"/usr/bin/perl", "perl"},
        {NULL, NULL}
    };
    
    for (int i = 0; apt_commands[i].source_path; i++) {
        if (access(apt_commands[i].source_path, F_OK) == 0) {
            copy_command_with_libs(apt_commands[i].source_path, apt_commands[i].dest_name, container_root);
        }
    }
    
    // apt-key 是 shell 腳本，直接複製
    snprintf(cmd, sizeof(cmd), "cp /usr/bin/apt-key %s/usr/bin/ 2>/dev/null && chmod +x %s/usr/bin/apt-key || true", 
             container_root, container_root);
    system(cmd);

    // 建立必要的符號連結，確保腳本可在 /usr/bin 下找到指令
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/bin", container_root);
    system(cmd);
    const char* link_commands[] = {
        "apt-get", "apt", "apt-cache", "apt-config",
        "dpkg", "dpkg-deb", "dpkg-query", "dpkg-split",
        "dpkg-divert", "dpkg-statoverride",
        "gpg", "gpgv", "perl", "test", "mktemp",
        NULL
    };
    for (int i = 0; link_commands[i]; i++) {
        snprintf(cmd, sizeof(cmd), "ln -sf /bin/%s %s/usr/bin/%s", link_commands[i], container_root, link_commands[i]);
        system(cmd);
    }

    // 建立 dpkg-preconfigure 的簡化實作（避免缺少此指令造成 apt 安裝失敗）
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/sbin", container_root);
    system(cmd);
    char dpkg_preconf_path[512];
    snprintf(dpkg_preconf_path, sizeof(dpkg_preconf_path), "%s/usr/sbin/dpkg-preconfigure", container_root);
    FILE* dpkg_preconf = fopen(dpkg_preconf_path, "w");
    if (dpkg_preconf) {
        fprintf(dpkg_preconf, "#!/bin/sh\n# Minimal stub for container environment\nexit 0\n");
        fclose(dpkg_preconf);
        chmod(dpkg_preconf_path, 0755);
    }
    
    // 建立必要的符號連結，確保腳本可找到對應命令
    snprintf(cmd, sizeof(cmd), "ln -sf /bin/test %s/usr/bin/test", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ln -sf /bin/gpg %s/usr/bin/gpg", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "ln -sf /bin/gpgv %s/usr/bin/gpgv", container_root);
    system(cmd);
    
    // 複製 apt 配置文件
    snprintf(cmd, sizeof(cmd), "cp -r /etc/apt/* %s/etc/apt/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 創建 apt 配置以允許無簽名倉庫並禁用沙箱（用於容器環境）
    snprintf(path, sizeof(path), "%s/etc/apt/apt.conf.d/99container-settings", container_root);
    FILE *apt_conf = fopen(path, "w");
    if (apt_conf) {
        fprintf(apt_conf, "// 容器環境配置\n");
        fprintf(apt_conf, "APT::Get::AllowUnauthenticated \"true\";\n");
        fprintf(apt_conf, "Acquire::AllowInsecureRepositories \"true\";\n");
        fprintf(apt_conf, "Acquire::AllowDowngradeToInsecureRepositories \"true\";\n");
        fprintf(apt_conf, "\n");
        fprintf(apt_conf, "// 禁用沙箱（容器內已經隔離）\n");
        fprintf(apt_conf, "APT::Sandbox::User \"root\";\n");
        fprintf(apt_conf, "Debug::NoDropPrivs \"true\";\n");
        fclose(apt_conf);
    }
    
    // 複製 dpkg 配置和狀態文件（但排除鎖文件）
    snprintf(cmd, sizeof(cmd), 
             "cp -r /var/lib/dpkg/status %s/var/lib/dpkg/ 2>/dev/null || true", 
             container_root);
    system(cmd);
    
    snprintf(cmd, sizeof(cmd), 
             "cp -r /var/lib/dpkg/available %s/var/lib/dpkg/ 2>/dev/null || true", 
             container_root);
    system(cmd);
    
    snprintf(cmd, sizeof(cmd), 
             "cp -r /var/lib/dpkg/diversions %s/var/lib/dpkg/ 2>/dev/null || true", 
             container_root);
    system(cmd);
    
    snprintf(cmd, sizeof(cmd), 
             "cp -r /var/lib/dpkg/statoverride %s/var/lib/dpkg/ 2>/dev/null || true", 
             container_root);
    system(cmd);
    
    // 確保 status 文件存在且有內容（總是創建以確保一致性）
    snprintf(path, sizeof(path), "%s/var/lib/dpkg/status", container_root);
    
    // 先確保目錄確實存在
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg", container_root);
    system(cmd);
    
    // 總是創建一個基本的 status 文件
    FILE *status_file = fopen(path, "w");
    if (status_file) {
        fprintf(status_file, "Package: dpkg\n");
        fprintf(status_file, "Status: install ok installed\n");
        fprintf(status_file, "Priority: required\n");
        fprintf(status_file, "Section: admin\n");
        fprintf(status_file, "Installed-Size: 6000\n");
        fprintf(status_file, "Maintainer: Dpkg Developers <debian-dpkg@lists.debian.org>\n");
        fprintf(status_file, "Architecture: amd64\n");
        fprintf(status_file, "Version: 1.21.0\n");
        fprintf(status_file, "Description: Debian package management system\n");
        fprintf(status_file, " This package provides the low-level infrastructure for handling the\n");
        fprintf(status_file, " installation and removal of Debian software packages.\n");
        fprintf(status_file, "\n");
        fclose(status_file);
    } else {
        fprintf(stderr, "警告: 無法創建 dpkg status 文件: %s (%s)\n", path, strerror(errno));
    }
    
    // 確保 available 文件存在（總是創建）
    snprintf(path, sizeof(path), "%s/var/lib/dpkg/available", container_root);
    FILE *available_file = fopen(path, "w");
    if (available_file) {
        fclose(available_file);
    } else {
        fprintf(stderr, "警告: 無法創建 dpkg available 文件: %s (%s)\n", path, strerror(errno));
    }
    
    // 創建 dpkg 的 arch 文件（非常重要！）- 總是覆蓋創建
    snprintf(path, sizeof(path), "%s/var/lib/dpkg/arch", container_root);
    FILE *arch_file = fopen(path, "w");
    if (arch_file) {
        fprintf(arch_file, "amd64\n");
        fclose(arch_file);
    } else {
        fprintf(stderr, "警告: 無法創建 dpkg arch 文件: %s (%s)\n", path, strerror(errno));
    }
    
    // 創建 dpkg 配置目錄和狀態目錄
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/alternatives", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/parts", container_root);
    system(cmd);
    
    // 創建 dpkg.cfg 配置文件
    snprintf(path, sizeof(path), "%s/etc/dpkg/dpkg.cfg", container_root);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/etc/dpkg", container_root);
    system(cmd);
    FILE *dpkg_cfg = fopen(path, "w");
    if (dpkg_cfg) {
        fprintf(dpkg_cfg, "# dpkg configuration file\n");
        fprintf(dpkg_cfg, "log /var/log/dpkg.log\n");
        fclose(dpkg_cfg);
    }
    
    // 創建 dpkg 日誌目錄
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/log", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "touch %s/var/log/dpkg.log", container_root);
    system(cmd);
    
    // 複製 GPG 密鑰
    snprintf(cmd, sizeof(cmd), "cp -r /etc/apt/trusted.gpg.d/* %s/etc/apt/trusted.gpg.d/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/keyrings/* %s/usr/share/keyrings/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 sources.list（如果存在）
    snprintf(cmd, sizeof(cmd), "cp /etc/apt/sources.list %s/etc/apt/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製主要 keyring 並設定 sources.list
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/keyrings", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/keyrings/ubuntu-archive-keyring.gpg %s/usr/share/keyrings/ 2>/dev/null || true", container_root);
    system(cmd);

    snprintf(path, sizeof(path), "%s/etc/apt/sources.list", container_root);
    FILE *sources_file = fopen(path, "w");
    if (sources_file) {
        fprintf(sources_file, "# Ubuntu 22.04 (Jammy) repositories\n");
        fprintf(sources_file, "deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] http://archive.ubuntu.com/ubuntu/ jammy main restricted universe multiverse\n");
        fprintf(sources_file, "deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted universe multiverse\n");
        fprintf(sources_file, "deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] http://security.ubuntu.com/ubuntu/ jammy-security main restricted universe multiverse\n");
        fprintf(sources_file, "deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] http://archive.ubuntu.com/ubuntu/ jammy-backports main restricted universe multiverse\n");
        fclose(sources_file);
    }
    
    // 複製 sources.list.d 目錄的內容
    snprintf(cmd, sizeof(cmd), "cp -r /etc/apt/sources.list.d/* %s/etc/apt/sources.list.d/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 設置 DNS 解析（複製主機的 resolv.conf）
    snprintf(cmd, sizeof(cmd), "cp /etc/resolv.conf %s/etc/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 如果 resolv.conf 不存在，創建一個基本的
    snprintf(path, sizeof(path), "%s/etc/resolv.conf", container_root);
    if (access(path, F_OK) != 0) {
        FILE *resolv_file = fopen(path, "w");
        if (resolv_file) {
            fprintf(resolv_file, "nameserver 8.8.8.8\n");
            fprintf(resolv_file, "nameserver 8.8.4.4\n");
            fclose(resolv_file);
        }
    }
    
    // 複製必要的共享庫（apt 的依賴）
    snprintf(cmd, sizeof(cmd), "cp -r /usr/lib/x86_64-linux-gnu/libapt* %s/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 apt 方法（用於不同的協議支持，如 http, https）
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/lib/apt", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/lib/apt/* %s/usr/lib/apt/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 dpkg 的架構信息（非常關鍵！）
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/dpkg", container_root);
    system(cmd);
    
    // 逐一複製關鍵的 dpkg 配置文件
    snprintf(cmd, sizeof(cmd), "cp /usr/share/dpkg/cputable %s/usr/share/dpkg/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/dpkg/tupletable %s/usr/share/dpkg/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/dpkg/ostable %s/usr/share/dpkg/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp /usr/share/dpkg/abitable %s/usr/share/dpkg/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製其他 dpkg 文件
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/dpkg/* %s/usr/share/dpkg/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 perl 模組（dpkg 需要）
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/perl", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/share/perl5", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/lib/x86_64-linux-gnu/perl-base", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/usr/lib/x86_64-linux-gnu/perl5", container_root);
    system(cmd);
    
    // 複製 Dpkg perl 模組（關鍵！）
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/perl5/Dpkg* %s/usr/share/perl5/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/perl5/Dpkg %s/usr/share/perl5/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 perl 基礎庫
    snprintf(cmd, sizeof(cmd), "cp -r /usr/lib/x86_64-linux-gnu/perl-base/* %s/usr/lib/x86_64-linux-gnu/perl-base/ 2>/dev/null || true", container_root);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "cp -r /usr/lib/x86_64-linux-gnu/perl5/* %s/usr/lib/x86_64-linux-gnu/perl5/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製 perl 核心模組
    snprintf(cmd, sizeof(cmd), "cp -r /usr/share/perl/5.* %s/usr/share/perl/ 2>/dev/null || true", container_root);
    system(cmd);
    
    // 複製其他必要的工具（apt 需要）
    char *extra_tools[] = {
        "/usr/bin/dpkg-architecture",
        "/usr/bin/apt-sortpkgs",
        "/usr/bin/apt-extracttemplates",
        "/usr/bin/apt-ftparchive",
        "/bin/gzip",
        "/bin/tar",
        "/usr/bin/xz",
        "/usr/bin/lz4",
        "/usr/bin/zstd",
        NULL
    };
    
    for (int i = 0; extra_tools[i]; i++) {
        if (access(extra_tools[i], F_OK) == 0) {
            char dest_name[128];
            snprintf(dest_name, sizeof(dest_name), "%s", strrchr(extra_tools[i], '/') + 1);
            copy_command_with_libs(extra_tools[i], dest_name, container_root);
        }
    }
    
    // 確保 dpkg/info 目錄存在並創建基本文件
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/info", container_root);
    system(cmd);

    // 強制創建 format 檔案為 2.0（覆蓋任何已存在的檔案）
    char format_path[512];
    snprintf(format_path, sizeof(format_path), "%s/var/lib/dpkg/info/format", container_root);
    // 先刪除可能存在的檔案
    unlink(format_path);
    FILE* format_file = fopen(format_path, "w");
    if (format_file) {
        fprintf(format_file, "2.0\n");
        fclose(format_file);
        chmod(format_path, 0644);
    }

    snprintf(format_path, sizeof(format_path), "%s/var/lib/dpkg/info/format-new", container_root);
    unlink(format_path);
    format_file = fopen(format_path, "w");
    if (format_file) {
        fprintf(format_file, "2.0\n");
        fclose(format_file);
        chmod(format_path, 0644);
    }
}

// 檢查基礎 rootfs 是否已存在
int check_base_rootfs_exists(void) {
    struct stat st;
    char marker_file[512];
    
    // 檢查基礎目錄是否存在
    if (stat(BASE_ROOTFS_PATH, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return 0;
    }
    
    // 檢查標記文件，確認 rootfs 已完整構建
    snprintf(marker_file, sizeof(marker_file), "%s/.rootfs_ready", BASE_ROOTFS_PATH);
    if (stat(marker_file, &st) != 0) {
        return 0;
    }
    
    return 1;
}

// 創建基礎 rootfs（只需執行一次）
int create_base_rootfs(void) {
    char cmd[1024];
    FILE *marker_file;
    
    printf("\n╔══════════════════════════════════════════════╗\n");
    printf("║  正在創建基礎容器映像（僅需執行一次）       ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");
    
    // 創建基礎目錄
    if (mkdir(BASE_ROOTFS_PATH, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "錯誤: 無法創建基礎 rootfs 目錄: %s\n", strerror(errno));
        return -1;
    }
    
    // 創建完整的目錄結構
    char *dirs[] = {
        "bin", "sbin", "usr", "usr/bin", "usr/sbin", "usr/local", "usr/local/bin",
        "tmp", "dev", "dev/pts", "proc", "sys", "etc", "var", "var/tmp", "var/log",
        "lib", "lib64", "lib/x86_64-linux-gnu", "usr/lib", "usr/lib/x86_64-linux-gnu",
        "usr/share", "usr/share/terminfo", "usr/share/terminfo/l", "usr/share/terminfo/x", "usr/share/terminfo/v",
        "lib/terminfo", "lib/terminfo/l", "lib/terminfo/x", "lib/terminfo/v",
        "etc/terminfo", "root",
        NULL
    };
    
    printf("[1/7] 創建目錄結構...\n");
    for (int i = 0; dirs[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", BASE_ROOTFS_PATH, dirs[i]);
        mkdir(path, 0755);
    }
    
    printf("[2/7] 安裝基本指令...\n");
    
    // 複製基本的系統指令
    struct {
        const char* source_path;
        const char* dest_name;
    } basic_commands[] = {
        {"/bin/bash", "bash"},
        {"/bin/sh", "sh"},
        {"/bin/ls", "ls"},
        {"/bin/cat", "cat"},
        {"/bin/echo", "echo"},
        {"/bin/pwd", "pwd"},
        {"/bin/ps", "ps"},
        {"/bin/grep", "grep"},
        {"/bin/find", "find"},
        {"/bin/mkdir", "mkdir"},
        {"/bin/rmdir", "rmdir"},
        {"/bin/rm", "rm"},
        {"/bin/cp", "cp"},
        {"/bin/mv", "mv"},
        {"/bin/chmod", "chmod"},
        {"/bin/chown", "chown"},
        {"/bin/df", "df"},
        {"/bin/du", "du"},
        {"/bin/wc", "wc"},
        {"/bin/head", "head"},
        {"/bin/tail", "tail"},
        {"/bin/sort", "sort"},
        {"/bin/uniq", "uniq"},
        {"/bin/dd", "dd"},
        {"/bin/touch", "touch"},
        {"/bin/mount", "mount"},
        {"/bin/umount", "umount"},
        {"/usr/bin/yes", "yes"},
        {"/usr/bin/seq", "seq"},
        {"/usr/bin/bc", "bc"},
        {"/usr/bin/tr", "tr"},
        {"/usr/bin/awk", "awk"},
        {"/usr/bin/sed", "sed"},
        {"/usr/bin/id", "id"},
        {"/usr/bin/whoami", "whoami"},
        {"/usr/bin/which", "which"},
        {"/usr/bin/test", "test"},
        {"/usr/bin/mktemp", "mktemp"},
        {"/usr/bin/top", "top"},
        {"/usr/bin/htop", "htop"},
        {"/usr/bin/free", "free"},
        {"/usr/bin/uptime", "uptime"},
        {"/usr/bin/uname", "uname"},
        {"/usr/bin/sleep", "sleep"},
        {"/usr/bin/env", "env"},
        {"/usr/bin/less", "less"},
        {"/usr/bin/clear", "clear"},
        {"/usr/bin/more", "more"},
        {"/usr/bin/vim", "vim"},
        {"/usr/bin/nano", "nano"},
        {"/usr/bin/curl", "curl"},
        {"/usr/bin/wget", "wget"},
        {"/bin/tar", "tar"},
        {"/bin/gzip", "gzip"},
        {NULL, NULL}
    };
    
    int installed_count = 0;
    for (int i = 0; basic_commands[i].source_path; i++) {
        if (access(basic_commands[i].source_path, F_OK) == 0) {
            copy_command_with_libs(basic_commands[i].source_path, basic_commands[i].dest_name, BASE_ROOTFS_PATH);
            installed_count++;
        }
    }
    printf("  ✓ 已安裝 %d 個命令\n", installed_count);
    
    printf("[3/7] 創建系統文件...\n");
    create_basic_system_files(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[4/7] 安裝終端支援...\n");
    terminfo_copy(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[5/7] 創建設備文件...\n");
    device_copy(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[6/7] 設置環境配置...\n");
    set_alias(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[7/7] 安裝套件管理工具 (apt-get)...\n");
    apt_get_copy(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    // 創建標記文件表示 rootfs 已完整構建
    snprintf(cmd, sizeof(cmd), "%s/.rootfs_ready", BASE_ROOTFS_PATH);
    marker_file = fopen(cmd, "w");
    if (marker_file) {
        fprintf(marker_file, "Base rootfs created successfully\n");
        fclose(marker_file);
    }
    
    // printf("\n✅ 基礎容器映像創建完成！\n");
    // printf("   位置: %s\n", BASE_ROOTFS_PATH);
    // printf("   提示: 後續容器啟動將直接使用此映像，速度將大幅提升\n\n");
    
    return 0;
}

// 為容器準備 rootfs（使用基礎 rootfs）
int setup_container_rootfs(const char* container_root, int use_copy) {
    char cmd[1024];
    
    // 創建容器根目錄
    if (mkdir(container_root, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "錯誤: 無法創建容器根目錄: %s\n", strerror(errno));
        return -1;
    }
    
    if (use_copy == 2) {
        // 方案 3: 使用 OverlayFS（推薦，類似 Docker）⭐
        // printf("正在設置容器文件系統 (OverlayFS)...\n");
        
        // 創建 overlay 需要的目錄
        char upper_dir[512], work_dir[512];
        snprintf(upper_dir, sizeof(upper_dir), "%s_upper", container_root);
        snprintf(work_dir, sizeof(work_dir), "%s_work", container_root);
        
        snprintf(cmd, sizeof(cmd), "mkdir -p %s %s", upper_dir, work_dir);
        system(cmd);
        
        // 在 upper layer 中預先創建需要寫入權限的目錄
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/tmp", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "chmod 1777 %s/tmp", upper_dir);
        system(cmd);
        
        // 創建 apt 需要的可寫目錄
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/apt/lists/partial", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/cache/apt/archives/partial", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/info", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/alternatives", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/updates", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/triggers", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/lib/dpkg/parts", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/var/log/apt", upper_dir);
        system(cmd);
        
        // 設置權限確保這些目錄可寫
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/var/lib/dpkg", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/var/lib/dpkg/info", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/var/lib/dpkg/alternatives", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/var/lib/dpkg/updates", upper_dir);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "chmod 755 %s/var/lib/dpkg/triggers", upper_dir);
        system(cmd);
        
        // 在 upper layer 中創建 format 檔案（確保可寫）
        char format_upper_path[512];
        snprintf(format_upper_path, sizeof(format_upper_path), "%s/var/lib/dpkg/info/format", upper_dir);
        FILE* format_file = fopen(format_upper_path, "w");
        if (format_file) {
            fprintf(format_file, "2.0\n");
            fclose(format_file);
        }
        snprintf(format_upper_path, sizeof(format_upper_path), "%s/var/lib/dpkg/info/format-new", upper_dir);
        format_file = fopen(format_upper_path, "w");
        if (format_file) {
            fprintf(format_file, "2.0\n");
            fclose(format_file);
        }
        
        // 不在 upper layer 創建 /dev 目錄，讓基礎層的設備文件直接透過
        
        // 直接掛載 overlayfs 到 container_root
        // lowerdir: 只讀的基礎層（共享）
        // upperdir: 可寫層（每個容器獨立）
        // workdir: overlay 工作目錄
        snprintf(cmd, sizeof(cmd),
                 "mount -t overlay overlay "
                 "-o lowerdir=%s,upperdir=%s,workdir=%s %s",
                 BASE_ROOTFS_PATH, upper_dir, work_dir, container_root);
        
        if (system(cmd) != 0) {
            fprintf(stderr, "⚠️  警告: OverlayFS 掛載失敗\n");
            fprintf(stderr, "    原因: 可能是內核不支援或權限不足\n");
            fprintf(stderr, "    改用複製模式...\n");
            // 回退到複製模式
            snprintf(cmd, sizeof(cmd), "cp -a %s/* %s/ 2>/dev/null", BASE_ROOTFS_PATH, container_root);
            system(cmd);
            // 確保 format 檔案是 2.0
            char format_container_path[512];
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format", container_root);
            unlink(format_container_path);
            FILE* format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format-new", container_root);
            unlink(format_container_path);
            format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
        } else {
            // OverlayFS 掛載成功
            // 確保 /tmp 目錄是可寫的（觸發 Copy-on-Write）
            char tmp_path[512];
            snprintf(tmp_path, sizeof(tmp_path), "%s/tmp", container_root);
            
            // 創建一個測試文件來觸發 CoW，然後刪除
            snprintf(cmd, sizeof(cmd), "touch %s/.overlay_test 2>/dev/null && rm -f %s/.overlay_test", 
                     tmp_path, tmp_path);
            system(cmd);
            
            // 確保 /tmp 有正確的權限
            chmod(tmp_path, 01777);  // rwxrwxrwt (sticky bit)
            
            // 確保 /var/lib/dpkg 目錄在 upper layer 中可寫（觸發 CoW）
            char dpkg_path[512];
            snprintf(dpkg_path, sizeof(dpkg_path), "%s/var/lib/dpkg", container_root);
            mkdir(dpkg_path, 0755); // 確保目錄存在
            chmod(dpkg_path, 0755); // 確保權限正確
            
            // 確保 /var/lib/dpkg/info 目錄在 upper layer 中可寫
            snprintf(dpkg_path, sizeof(dpkg_path), "%s/var/lib/dpkg/info", container_root);
            mkdir(dpkg_path, 0755);
            chmod(dpkg_path, 0755);
            
            // 確保 /var/lib/dpkg/alternatives 目錄在 upper layer 中可寫
            snprintf(dpkg_path, sizeof(dpkg_path), "%s/var/lib/dpkg/alternatives", container_root);
            mkdir(dpkg_path, 0755);
            chmod(dpkg_path, 0755);
            
            // 確保 /var/lib/dpkg/updates 目錄在 upper layer 中可寫
            snprintf(dpkg_path, sizeof(dpkg_path), "%s/var/lib/dpkg/updates", container_root);
            mkdir(dpkg_path, 0755);
            chmod(dpkg_path, 0755);
            
            // 確保 /var/lib/dpkg/triggers 目錄在 upper layer 中可寫
            snprintf(dpkg_path, sizeof(dpkg_path), "%s/var/lib/dpkg/triggers", container_root);
            mkdir(dpkg_path, 0755);
            chmod(dpkg_path, 0755);
            
            // 在掛載後的容器內再次創建 format 檔案（確保合併視圖中是 2.0）
            char format_container_path[512];
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format", container_root);
            unlink(format_container_path); // 刪除可能存在的舊檔案
            FILE* format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format-new", container_root);
            unlink(format_container_path); // 刪除可能存在的舊檔案
            format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            
            // printf("  ✅ 使用 OverlayFS (寫時複製)\n");
        }
    } else if (use_copy == 1) {
        // 方案 1: 複製整個 rootfs（較慢但簡單）
        printf("正在複製容器文件系統...\n");
        snprintf(cmd, sizeof(cmd), "cp -a %s/* %s/ 2>/dev/null", BASE_ROOTFS_PATH, container_root);
        system(cmd);
        // 確保 format 檔案是 2.0
        char format_container_path[512];
        snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format", container_root);
        unlink(format_container_path);
        FILE* format_file = fopen(format_container_path, "w");
        if (format_file) {
            fprintf(format_file, "2.0\n");
            fclose(format_file);
            chmod(format_container_path, 0644);
        }
        snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format-new", container_root);
        unlink(format_container_path);
        format_file = fopen(format_container_path, "w");
        if (format_file) {
            fprintf(format_file, "2.0\n");
            fclose(format_file);
            chmod(format_container_path, 0644);
        }
    } else {
        // 方案 2: 使用 bind mount（快速但需要在 chroot 前執行）
        // printf("正在掛載容器文件系統 (Bind Mount)...\n");
        snprintf(cmd, sizeof(cmd), "mount --bind %s %s", BASE_ROOTFS_PATH, container_root);
        if (system(cmd) != 0) {
            fprintf(stderr, "警告: bind mount 失敗，嘗試複製文件...\n");
            snprintf(cmd, sizeof(cmd), "cp -a %s/* %s/ 2>/dev/null", BASE_ROOTFS_PATH, container_root);
            system(cmd);
            // 確保 format 檔案是 2.0
            char format_container_path[512];
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format", container_root);
            unlink(format_container_path);
            FILE* format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format-new", container_root);
            unlink(format_container_path);
            format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
        } else {
            // Bind mount 成功，但在容器內仍需確保 format 檔案是 2.0
            char format_container_path[512];
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format", container_root);
            unlink(format_container_path);
            FILE* format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            snprintf(format_container_path, sizeof(format_container_path), "%s/var/lib/dpkg/info/format-new", container_root);
            unlink(format_container_path);
            format_file = fopen(format_container_path, "w");
            if (format_file) {
                fprintf(format_file, "2.0\n");
                fclose(format_file);
                chmod(format_container_path, 0644);
            }
            // printf("  ⚡ 瞬間完成！\n");
        }
    }
    
    // 創建容器特定的目錄（這些不應該共享）
    char *private_dirs[] = {"proc", "sys", "dev/pts", "tmp", NULL};
    for (int i = 0; private_dirs[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", container_root, private_dirs[i]);
        mkdir(path, 0755);
    }
    
    return 0;
}

