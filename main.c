#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ROOT_PREFIX "/tmp/container_root_"
#define CGROUP_ROOT "/sys/fs/cgroup"
#define CGROUP_NAME_PREFIX "docker_in_c_container_"

static char child_stack[STACK_SIZE];

// 資源限制配置結構
typedef struct {
    long memory_limit_mb;      // 記憶體限制 (MB)
    int cpu_shares;            // CPU 份額 (預設 1024)
    int cpu_quota_us;          // CPU 配額 (微秒/100ms週期)
    int pids_max;              // 最大進程數
} cgroup_limits_t;

// 容器初始化參數結構
typedef struct {
    cgroup_limits_t* limits;
    int sync_pipe[2];          // 用於父子進程同步的管道
    char container_root[256];  // 容器根目錄路徑
    char cgroup_name[128];     // cgroup 名稱
    int container_id;          // 容器 ID
} container_init_args_t;

// 寫入 cgroup 檔案的輔助函數
int write_cgroup_file(const char* cgroup_path, const char* filename, const char* value) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", cgroup_path, filename);
    
    FILE* file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "警告: 無法打開 %s: %s\n", path, strerror(errno));
        return -1;
    }
    
    if (fprintf(file, "%s", value) < 0) {
        fprintf(stderr, "警告: 無法寫入 %s: %s\n", path, strerror(errno));
        fclose(file);
        return -1;
    }
    
    fclose(file);
    return 0;
}

// 檢測 cgroup 版本 (v1 或 v2)
int detect_cgroup_version() {
    struct stat st;
    // 如果存在 cgroup.controllers 檔案，則為 cgroup v2
    if (stat("/sys/fs/cgroup/cgroup.controllers", &st) == 0) {
        return 2;
    }
    // 如果存在 memory 子系統目錄，則為 cgroup v1
    if (stat("/sys/fs/cgroup/memory", &st) == 0) {
        return 1;
    }
    return 0;
}

// 創建並配置 cgroup v2
int setup_cgroup_v2(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name) {
    char cgroup_path[512];
    char buffer[128];
    
    printf("正在設置資源限制 (cgroup v2)...\n");
    
    // 創建 cgroup 目錄
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_ROOT, cgroup_name);
    if (mkdir(cgroup_path, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "警告: 無法創建 cgroup 目錄: %s\n", strerror(errno));
        return -1;
    }
    
    // 先將進程移入 cgroup（在設置限制之前）
    // 這樣可以避免某些系統上的權限問題
    snprintf(buffer, sizeof(buffer), "%d", pid);
    write_cgroup_file(cgroup_path, "cgroup.procs", buffer);
    
    // 嘗試在根 cgroup 中啟用控制器
    // 注意：這可能會失敗，但不影響基本功能
    write_cgroup_file(CGROUP_ROOT, "cgroup.subtree_control", "+cpu +memory +pids");
    
    // 設置記憶體限制
    if (limits->memory_limit_mb > 0) {
        snprintf(buffer, sizeof(buffer), "%ld", limits->memory_limit_mb * 1024 * 1024);
        if (write_cgroup_file(cgroup_path, "memory.max", buffer) == 0) {
            printf("  記憶體限制: %ld MB\n", limits->memory_limit_mb);
        }
    }
    
    // 設置 CPU 權重 (cgroup v2 使用 weight 代替 shares)
    if (limits->cpu_shares > 0) {
        // 將 shares (範圍 2-262144) 轉換為 weight (範圍 1-10000)
        int weight = (limits->cpu_shares * 10000) / 1024;
        if (weight < 1) weight = 1;
        if (weight > 10000) weight = 10000;
        
        snprintf(buffer, sizeof(buffer), "%d", weight);
        if (write_cgroup_file(cgroup_path, "cpu.weight", buffer) == 0) {
            printf("  CPU 權重: %d (shares: %d)\n", weight, limits->cpu_shares);
        }
    }
    
    // 設置 CPU 配額
    if (limits->cpu_quota_us > 0) {
        snprintf(buffer, sizeof(buffer), "%d 100000", limits->cpu_quota_us);
        if (write_cgroup_file(cgroup_path, "cpu.max", buffer) == 0) {
            printf("  CPU 配額: %d us / 100000 us (%.1f%%)\n", 
                   limits->cpu_quota_us, (limits->cpu_quota_us / 1000.0));
        }
    }
    
    // 設置進程數限制
    if (limits->pids_max > 0) {
        snprintf(buffer, sizeof(buffer), "%d", limits->pids_max);
        if (write_cgroup_file(cgroup_path, "pids.max", buffer) == 0) {
            printf("  最大進程數: %d\n", limits->pids_max);
        }
    }
    
    printf("  已將進程 %d 加入 cgroup\n", pid);
    
    return 0;
}

// 創建並配置 cgroup v1
int setup_cgroup_v1(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name) {
    char cgroup_path[512];
    char buffer[128];
    
    printf("正在設置資源限制 (cgroup v1)...\n");
    
    // 設置記憶體限制
    if (limits->memory_limit_mb > 0) {
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/memory/%s", cgroup_name);
        if (mkdir(cgroup_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "警告: 無法創建 memory cgroup: %s\n", strerror(errno));
        } else {
            snprintf(buffer, sizeof(buffer), "%ld", limits->memory_limit_mb * 1024 * 1024);
            write_cgroup_file(cgroup_path, "memory.limit_in_bytes", buffer);
            
            snprintf(buffer, sizeof(buffer), "%d", pid);
            write_cgroup_file(cgroup_path, "tasks", buffer);
            printf("  記憶體限制: %ld MB\n", limits->memory_limit_mb);
        }
    }
    
    // 設置 CPU 限制
    if (limits->cpu_shares > 0 || limits->cpu_quota_us > 0) {
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/cpu/%s", cgroup_name);
        if (mkdir(cgroup_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "警告: 無法創建 cpu cgroup: %s\n", strerror(errno));
        } else {
            if (limits->cpu_shares > 0) {
                snprintf(buffer, sizeof(buffer), "%d", limits->cpu_shares);
                write_cgroup_file(cgroup_path, "cpu.shares", buffer);
                printf("  CPU 份額: %d\n", limits->cpu_shares);
            }
            
            if (limits->cpu_quota_us > 0) {
                snprintf(buffer, sizeof(buffer), "%d", limits->cpu_quota_us);
                write_cgroup_file(cgroup_path, "cpu.cfs_quota_us", buffer);
                printf("  CPU 配額: %d us / 100000 us (%.1f%%)\n", 
                       limits->cpu_quota_us, (limits->cpu_quota_us / 1000.0));
            }
            
            snprintf(buffer, sizeof(buffer), "%d", pid);
            write_cgroup_file(cgroup_path, "tasks", buffer);
        }
    }
    
    // 設置進程數限制
    if (limits->pids_max > 0) {
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/pids/%s", cgroup_name);
        if (mkdir(cgroup_path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "警告: 無法創建 pids cgroup: %s\n", strerror(errno));
        } else {
            snprintf(buffer, sizeof(buffer), "%d", limits->pids_max);
            write_cgroup_file(cgroup_path, "pids.max", buffer);
            
            snprintf(buffer, sizeof(buffer), "%d", pid);
            write_cgroup_file(cgroup_path, "tasks", buffer);
            printf("  最大進程數: %d\n", limits->pids_max);
        }
    }
    
    return 0;
}

// 設置 cgroup 資源限制
int setup_cgroup_limits(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name) {
    int cgroup_version = detect_cgroup_version();
    
    if (cgroup_version == 0) {
        fprintf(stderr, "警告: 未檢測到 cgroup 支援，資源限制將不生效\n");
        return -1;
    }
    
    if (cgroup_version == 2) {
        return setup_cgroup_v2(pid, limits, cgroup_name);
    } else {
        return setup_cgroup_v1(pid, limits, cgroup_name);
    }
}

// 清理 cgroup
void cleanup_cgroup(const char* cgroup_name) {
    int cgroup_version = detect_cgroup_version();
    char cgroup_path[512];
    
    if (cgroup_version == 2) {
        snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_ROOT, cgroup_name);
        rmdir(cgroup_path);
    } else if (cgroup_version == 1) {
        // 清理各個子系統的 cgroup
        char* subsystems[] = {"memory", "cpu", "pids", NULL};
        for (int i = 0; subsystems[i]; i++) {
            snprintf(cgroup_path, sizeof(cgroup_path), 
                     "/sys/fs/cgroup/%s/%s", subsystems[i], cgroup_name);
            rmdir(cgroup_path);
        }
    }
}

// 獲取真實用戶 UID（即使在 sudo 下運行）
uid_t get_real_uid() {
    char *sudo_uid = getenv("SUDO_UID");
    if (sudo_uid) {
        return (uid_t)atoi(sudo_uid);
    }
    return getuid();
}

// 獲取真實用戶 GID（即使在 sudo 下運行）
gid_t get_real_gid() {
    char *sudo_gid = getenv("SUDO_GID");
    if (sudo_gid) {
        return (gid_t)atoi(sudo_gid);
    }
    return getgid();
}

// 設置用戶命名空間的 UID/GID 映射
int setup_user_namespace(pid_t pid) {
    char path[256];
    char mapping[256];
    FILE *file;
    uid_t real_uid = get_real_uid();
    gid_t real_gid = get_real_gid();
    
    printf("正在設置用戶命名空間映射...\n");
    
    // 禁用 setgroups（安全要求）
    snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "deny");
        fclose(file);
    } else {
        fprintf(stderr, "警告: 無法禁用 setgroups: %s\n", strerror(errno));
    }
    
    // 設置 UID 映射：將容器內的 root (UID 0) 映射到主機真實用戶
    snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
    snprintf(mapping, sizeof(mapping), "0 %d 1", real_uid);
    
    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "錯誤: 無法打開 uid_map: %s\n", strerror(errno));
        return -1;
    }
    
    if (fprintf(file, "%s", mapping) < 0) {
        fprintf(stderr, "錯誤: 無法寫入 uid_map: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }
    fclose(file);
    printf("  UID 映射: 容器 0 -> 主機 %d\n", real_uid);
    
    // 設置 GID 映射
    snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
    snprintf(mapping, sizeof(mapping), "0 %d 1", real_gid);
    
    file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "錯誤: 無法打開 gid_map: %s\n", strerror(errno));
        return -1;
    }
    
    if (fprintf(file, "%s", mapping) < 0) {
        fprintf(stderr, "錯誤: 無法寫入 gid_map: %s\n", strerror(errno));
        fclose(file);
        return -1;
    }
    fclose(file);
    printf("  GID 映射: 容器 0 -> 主機 %d\n", real_gid);
    
    return 0;
}


void terminfo_copy(const char* container_root){
     // 複製 terminfo 資料庫
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


// 創建基本的系統文件（/etc/passwd, /etc/group, /etc/hostname）
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

// 創建清屏腳本
void clear_copy(const char* container_root){
    char path[512];
    FILE *file;
    
    // 創建簡單的清屏腳本（直接使用 ANSI 轉義序列）
    snprintf(path, sizeof(path), "%s/bin/clear_alt", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "#!/bin/bash\n");
        fprintf(file, "printf '\\033[2J\\033[H'\n");
        fclose(file);
        chmod(path, 0755);
    }
    
    // 創建 cls 別名（符號連結）
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ln -sf /bin/clear_alt %s/bin/cls 2>/dev/null || true", container_root);
    system(cmd);
    
    // 覆蓋原有的 clear 指令
    snprintf(path, sizeof(path), "%s/bin/clear", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "#!/bin/bash\n");
        fprintf(file, "printf '\\033[2J\\033[H'\n");
        fclose(file);
        chmod(path, 0755);
    }
}

// 創建設備文件
void device_copy(const char* container_root){
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
void vim_copy(const char* container_root){
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

// 複製指令和其依賴庫的函數
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
void man_command_copy(const char* container_root){
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
void set_alias(const char* container_root){
    char path[512];
    FILE *file;
    
    // 創建簡單的 bashrc 來設置別名
    snprintf(path, sizeof(path), "%s/etc/bash.bashrc", container_root);
    file = fopen(path, "w");
    if (file) {
        fprintf(file, "alias cls=clear_alt\n");
        fprintf(file, "alias ll=\"ls -la\"\n");
        fprintf(file, "export TERM=xterm\n");
        fprintf(file, "export TERMINFO=/usr/share/terminfo:/lib/terminfo:/etc/terminfo\n");
        fclose(file);
    }
}

// 創建虛擬的 meminfo 文件以反映 cgroup 限制（在 chroot 後調用）
void create_virtual_meminfo_after_chroot(long memory_limit_mb) {
    // 在 /tmp 中創建虛擬 meminfo（tmpfs，可寫）
    FILE* meminfo = fopen("/tmp/meminfo.custom", "w");
    if (!meminfo) {
        fprintf(stderr, "警告: 無法創建虛擬 meminfo\n");
        return;
    }
    
    // 計算以 KB 為單位的記憶體值
    long mem_total_kb = memory_limit_mb * 1024;
    long mem_free_kb = mem_total_kb * 80 / 100;  // 假設 80% 可用
    long mem_available_kb = mem_total_kb * 75 / 100;
    
    // 創建簡化的 meminfo，包含 free 命令需要的關鍵字段
    fprintf(meminfo, "MemTotal:       %ld kB\n", mem_total_kb);
    fprintf(meminfo, "MemFree:        %ld kB\n", mem_free_kb);
    fprintf(meminfo, "MemAvailable:   %ld kB\n", mem_available_kb);
    fprintf(meminfo, "Buffers:             0 kB\n");
    fprintf(meminfo, "Cached:              0 kB\n");
    fprintf(meminfo, "SwapCached:          0 kB\n");
    fprintf(meminfo, "Active:              0 kB\n");
    fprintf(meminfo, "Inactive:            0 kB\n");
    fprintf(meminfo, "SwapTotal:           0 kB\n");
    fprintf(meminfo, "SwapFree:            0 kB\n");
    fprintf(meminfo, "Dirty:               0 kB\n");
    fprintf(meminfo, "Writeback:           0 kB\n");
    fprintf(meminfo, "Shmem:               0 kB\n");
    fprintf(meminfo, "Slab:                0 kB\n");
    fprintf(meminfo, "SReclaimable:        0 kB\n");
    fprintf(meminfo, "SUnreclaim:          0 kB\n");
    
    fclose(meminfo);
}

// 容器初始化函數
int container_init(void* arg) {
    container_init_args_t* args = (container_init_args_t*)arg;
    cgroup_limits_t* limits = args->limits;
    const char* container_root = args->container_root;
    
    // 使用 -i 參數強制 bash 進入交互模式
    char *argv[] = {"/bin/bash", "-i", NULL};
    char *envp[] = {
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin", 
        "HOME=/", 
        "PS1=[容器] \\w # ", 
        "TERM=xterm",  // 使用更通用的 xterm 終端類型
        "TERMINFO=/usr/share/terminfo:/lib/terminfo:/etc/terminfo",  // 多個搜尋路徑
        NULL
    };
    
    // 關閉管道的寫入端（父進程使用）
    close(args->sync_pipe[1]);
    
    // 等待父進程完成 uid_map 和 gid_map 的設置
    char ch;
    if (read(args->sync_pipe[0], &ch, 1) != 1) {
        fprintf(stderr, "等待父進程設置映射時失敗\n");
    }
    close(args->sync_pipe[0]);
    
    // 在用戶命名空間中設置 UID/GID 為 0（必須在 uid_map 設置後立即執行）
    // 這樣後續創建的所有文件和目錄都會有正確的權限
    if (setgid(0) == -1) {
        perror("setgid");
        return -1;
    }
    if (setuid(0) == -1) {
        perror("setuid");
        return -1;
    }
    
    printf("=== 進入容器環境 (ID: %d) ===\n", args->container_id);
    printf("PID: %d\n", getpid());
    printf("PPID: %d\n", getppid());
    printf("UID: %d, GID: %d\n", getuid(), getgid());
    printf("容器根目錄: %s\n", container_root);
    
    // 創建容器根目錄
    if (mkdir(container_root, 0755) == -1 && errno != EEXIST) {
        perror("mkdir container_root");
        return -1;
    }
    
    // 創建完整的目錄結構
    char *dirs[] = {
        "bin", "sbin", "usr", "usr/bin", "usr/sbin", "usr/local", "usr/local/bin",
        "tmp", "dev", "dev/pts", "proc", "sys", "etc", "var", "var/tmp", "var/log",
        "lib", "lib64", "lib/x86_64-linux-gnu", "usr/lib", "usr/lib/x86_64-linux-gnu",
        "usr/share", "usr/share/terminfo", "usr/share/terminfo/l", "usr/share/terminfo/x", "usr/share/terminfo/v",
        "lib/terminfo", "lib/terminfo/l", "lib/terminfo/x", "lib/terminfo/v",
        "etc/terminfo",
        NULL
    };
    
    for (int i = 0; dirs[i]; i++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", container_root, dirs[i]);
        if (mkdir(path, 0755) == -1 && errno != EEXIST) {
            // 忽略錯誤
        }
    }
    
    printf("正在安裝基本指令...\n");
    
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
        {"/usr/bin/man", "man"},
        {"/usr/bin/groff", "groff"},
        {"/usr/bin/grotty", "grotty"},
        {"/usr/bin/nroff", "nroff"},
        {"/usr/bin/troff", "troff"},
        {"/usr/bin/tbl", "tbl"},
        {"/usr/bin/col", "col"},
        {"/usr/bin/gunzip", "gunzip"},
        {"/usr/bin/zcat", "zcat"},
        {"/usr/bin/preconv", "preconv"},
        {"/usr/bin/eqn", "eqn"},
        {"/usr/bin/pic", "pic"},
        {"/usr/bin/refer", "refer"},
        {"/usr/bin/soelim", "soelim"},
        {"/usr/bin/curl", "curl"},
        {"/usr/bin/wget", "wget"},
        {"/bin/tar", "tar"},
        {"/bin/gzip", "gzip"},
        {NULL, NULL}
    };
    
    for (int i = 0; basic_commands[i].source_path; i++) {
        if (access(basic_commands[i].source_path, F_OK) == 0) {
            copy_command_with_libs(basic_commands[i].source_path, basic_commands[i].dest_name, container_root);
            // printf("  已安裝: %s\n", basic_commands[i].dest_name);
        }
    }

    // 創建基本系統文件（必須在 chroot 之前）
    printf("正在創建基本系統文件...\n");
    create_basic_system_files(container_root);
    
    printf("正在安裝終端支援...\n");
    terminfo_copy(container_root);
    
    printf("正在創建設備文件...\n");
    device_copy(container_root);
    
    printf("正在設置別名...\n");
    set_alias(container_root);
    
    printf("正在安裝清屏腳本...\n");
    clear_copy(container_root);
    
    // 可選：安裝 vim 和 man（較慢，可根據需要啟用）
    // printf("正在安裝 vim 支援...\n");
    // vim_copy(container_root);
    // printf("正在安裝 man 手冊...\n");
    // man_command_copy(container_root);
    
    // 文件系統隔離：改變根目錄並切換工作目錄
    // chroot: 將進程的根目錄改為容器目錄，實現文件系統隔離
    // chdir: 切換到新的根目錄，避免工作目錄錯誤
    if (chroot(container_root) == -1 || chdir("/") == -1) {
        perror("文件系統隔離失敗");
        return -1;
    }
    
    // 掛載 proc 和 sys 文件系統
    if (mount("proc", "/proc", "proc", 0, NULL) == -1 || mount("sysfs", "/sys", "sysfs", 0, NULL) == -1) {
        printf("警告: 無法掛載 /proc (某些指令如 top 可能無法正常工作)\n");
    }
    
    // 創建並掛載虛擬 meminfo（在 chroot 和 mount proc 之後）
    if (limits && limits->memory_limit_mb > 0) {
        // 創建虛擬 meminfo 文件
        create_virtual_meminfo_after_chroot(limits->memory_limit_mb);
        
        // 使用 bind mount 將虛擬 meminfo 掛載到 /proc/meminfo
        if (mount("/tmp/meminfo.custom", "/proc/meminfo", NULL, MS_BIND, NULL) == -1) {
            printf("警告: 無法掛載虛擬 meminfo: %s\n", strerror(errno));
            printf("     (free 命令將顯示主機記憶體，但 cgroup 限制仍然生效)\n");
        } else {
            printf("✓ 已設置虛擬記憶體視圖 (%ld MB)\n", limits->memory_limit_mb);
        }
    }
    
    
    // 掛載 devpts (偽終端設備，對 top/htop 等交互式程式很重要)
    mkdir("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0, "newinstance,ptmxmode=0666") == -1) {
        // 如果新實例失敗，嘗試不帶參數掛載
        if (mount("devpts", "/dev/pts", "devpts", 0, NULL) == -1) {
            printf("警告: 無法掛載 /dev/pts (終端交互可能受影響)\n");
        }
    }
    
    // 創建 /dev/ptmx 的符號連結
    symlink("/dev/pts/ptmx", "/dev/ptmx");
    
    printf("\n容器環境已準備就緒！\n");
    printf("輸入 'exit' 離開容器\n\n");
    
    // 執行 bash
    if (execve("/bin/bash", argv, envp) == -1) {
        perror("execve");
        return -1;
    }
    
    return 0;
}


int main() {
    printf("=== 簡單容器實現 ===\n");
    printf("正在創建容器...\n");
    
    // 生成唯一的容器 ID（使用當前時間戳和進程ID）
    int container_id = (int)time(NULL) % 100000 + getpid() % 1000;
    
    printf("容器 ID: %d\n", container_id);
    
    // 配置資源限制
    static cgroup_limits_t limits = {
        .memory_limit_mb = 512,    // 限制記憶體為 512 MB
        .cpu_shares = 512,         // CPU 份額為 512 (預設的一半)
        .cpu_quota_us = 50000,     // CPU 配額為 50% (50000/100000)
        .pids_max = 100            // 最多 100 個進程
    };
    
    // 創建用於同步的管道
    static container_init_args_t args;
    args.limits = &limits;
    args.container_id = container_id;
    
    // 生成唯一的容器根目錄和 cgroup 名稱
    snprintf(args.container_root, sizeof(args.container_root), "%s%d", CONTAINER_ROOT_PREFIX, container_id);
    snprintf(args.cgroup_name, sizeof(args.cgroup_name), "%s%d", CGROUP_NAME_PREFIX, container_id);
    
    printf("容器根目錄: %s\n", args.container_root);
    printf("Cgroup 名稱: %s\n\n", args.cgroup_name);
    
    if (pipe(args.sync_pipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
    
    // 創建子進程，使用新的命名空間
    pid_t pid = clone(container_init, 
                      child_stack + STACK_SIZE,
                      CLONE_NEWPID |    // 新的 PID 命名空間
                      CLONE_NEWNS |     // 新的掛載命名空間
                      CLONE_NEWUTS |    // 新的主機名命名空間
                      CLONE_NEWIPC |    // 新的 IPC 命名空間
                      CLONE_NEWUSER |   // 新的用戶命名空間
                      SIGCHLD,          // 子進程結束時發送 SIGCHLD
                      &args);           // 傳遞參數結構
    
    if (pid == -1) {
        perror("clone");
        exit(EXIT_FAILURE);
    }
    
    // 關閉管道的讀取端（子進程使用）
    close(args.sync_pipe[0]);
    
    printf("容器已創建，PID: %d\n\n", pid);
    
    // 設置用戶命名空間映射
    if (setup_user_namespace(pid) == -1) {
        fprintf(stderr, "警告: 用戶命名空間設置失敗，但容器將繼續運行\n");
    }
    printf("\n");
    
    // 通知子進程映射已完成，可以繼續執行
    close(args.sync_pipe[1]);
    
    // 設置資源限制
    setup_cgroup_limits(pid, &limits, args.cgroup_name);
    printf("\n");
    
    // 等待子進程結束
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }
    
    printf("容器已退出\n");
    
    // 清理 cgroup
    cleanup_cgroup(args.cgroup_name);
    
    // 清理容器目錄
    char cleanup_cmd[512];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", args.container_root);
    printf("正在清理容器目錄: %s\n", args.container_root);
    if (system(cleanup_cmd) == -1) {
        perror("清理容器目錄");
    }
    
    printf("容器 %d 清理完成\n", container_id);
    return 0;
}