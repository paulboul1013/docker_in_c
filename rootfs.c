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
    
    printf("[1/6] 創建目錄結構...\n");
    for (int i = 0; dirs[i]; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", BASE_ROOTFS_PATH, dirs[i]);
        mkdir(path, 0755);
    }
    
    printf("[2/6] 安裝基本指令...\n");
    
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
        {"/usr/bin/yes", "yes"},
        {"/usr/bin/seq", "seq"},
        {"/usr/bin/bc", "bc"},
        {"/usr/bin/tr", "tr"},
        {"/usr/bin/awk", "awk"},
        {"/usr/bin/sed", "sed"},
        {"/usr/bin/id", "id"},
        {"/usr/bin/whoami", "whoami"},
        {"/usr/bin/which", "which"},
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
    
    printf("[3/6] 創建系統文件...\n");
    create_basic_system_files(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[4/6] 安裝終端支援...\n");
    terminfo_copy(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[5/6] 創建設備文件...\n");
    device_copy(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    printf("[6/6] 設置環境配置...\n");
    set_alias(BASE_ROOTFS_PATH);
    printf("  ✓ 完成\n");
    
    // 創建標記文件表示 rootfs 已完整構建
    snprintf(cmd, sizeof(cmd), "%s/.rootfs_ready", BASE_ROOTFS_PATH);
    marker_file = fopen(cmd, "w");
    if (marker_file) {
        fprintf(marker_file, "Base rootfs created successfully\n");
        fclose(marker_file);
    }
    
    printf("\n✅ 基礎容器映像創建完成！\n");
    printf("   位置: %s\n", BASE_ROOTFS_PATH);
    printf("   提示: 後續容器啟動將直接使用此映像，速度將大幅提升\n\n");
    
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
        printf("正在設置容器文件系統 (OverlayFS)...\n");
        
        // 創建 overlay 需要的目錄
        char upper_dir[512], work_dir[512];
        snprintf(upper_dir, sizeof(upper_dir), "%s_upper", container_root);
        snprintf(work_dir, sizeof(work_dir), "%s_work", container_root);
        
        snprintf(cmd, sizeof(cmd), "mkdir -p %s %s", upper_dir, work_dir);
        system(cmd);
        
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
            
            printf("  ✅ 使用 OverlayFS (寫時複製)\n");
        }
    } else if (use_copy == 1) {
        // 方案 1: 複製整個 rootfs（較慢但簡單）
        printf("正在複製容器文件系統...\n");
        snprintf(cmd, sizeof(cmd), "cp -a %s/* %s/ 2>/dev/null", BASE_ROOTFS_PATH, container_root);
        system(cmd);
    } else {
        // 方案 2: 使用 bind mount（快速但需要在 chroot 前執行）
        // printf("正在掛載容器文件系統 (Bind Mount)...\n");
        snprintf(cmd, sizeof(cmd), "mount --bind %s %s", BASE_ROOTFS_PATH, container_root);
        if (system(cmd) != 0) {
            fprintf(stderr, "警告: bind mount 失敗，嘗試複製文件...\n");
            snprintf(cmd, sizeof(cmd), "cp -a %s/* %s/ 2>/dev/null", BASE_ROOTFS_PATH, container_root);
            system(cmd);
        } else {
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

