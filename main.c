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

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ROOT "/tmp/container_root"
#define CGROUP_ROOT "/sys/fs/cgroup"
#define CGROUP_NAME "docker_in_c_container"

static char child_stack[STACK_SIZE];

// 資源限制配置結構
typedef struct {
    long memory_limit_mb;      // 記憶體限制 (MB)
    int cpu_shares;            // CPU 份額 (預設 1024)
    int cpu_quota_us;          // CPU 配額 (微秒/100ms週期)
    int pids_max;              // 最大進程數
} cgroup_limits_t;

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
int setup_cgroup_v2(pid_t pid, const cgroup_limits_t* limits) {
    char cgroup_path[512];
    char buffer[128];
    
    printf("正在設置資源限制 (cgroup v2)...\n");
    
    // 創建 cgroup 目錄
    snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_ROOT, CGROUP_NAME);
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
int setup_cgroup_v1(pid_t pid, const cgroup_limits_t* limits) {
    char cgroup_path[512];
    char buffer[128];
    
    printf("正在設置資源限制 (cgroup v1)...\n");
    
    // 設置記憶體限制
    if (limits->memory_limit_mb > 0) {
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/memory/%s", CGROUP_NAME);
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
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/cpu/%s", CGROUP_NAME);
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
        snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/pids/%s", CGROUP_NAME);
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
int setup_cgroup_limits(pid_t pid, const cgroup_limits_t* limits) {
    int cgroup_version = detect_cgroup_version();
    
    if (cgroup_version == 0) {
        fprintf(stderr, "警告: 未檢測到 cgroup 支援，資源限制將不生效\n");
        return -1;
    }
    
    if (cgroup_version == 2) {
        return setup_cgroup_v2(pid, limits);
    } else {
        return setup_cgroup_v1(pid, limits);
    }
}

// 清理 cgroup
void cleanup_cgroup() {
    int cgroup_version = detect_cgroup_version();
    char cgroup_path[512];
    
    if (cgroup_version == 2) {
        snprintf(cgroup_path, sizeof(cgroup_path), "%s/%s", CGROUP_ROOT, CGROUP_NAME);
        rmdir(cgroup_path);
    } else if (cgroup_version == 1) {
        // 清理各個子系統的 cgroup
        char* subsystems[] = {"memory", "cpu", "pids", NULL};
        for (int i = 0; subsystems[i]; i++) {
            snprintf(cgroup_path, sizeof(cgroup_path), 
                     "/sys/fs/cgroup/%s/%s", subsystems[i], CGROUP_NAME);
            rmdir(cgroup_path);
        }
    }
}


void terminfo_copy(){
     // 複製 terminfo 資料庫
    //  printf("正在安裝終端支援...\n");
     
     // 嘗試從多個可能的位置複製 terminfo
     system("cp -r /lib/terminfo " CONTAINER_ROOT "/lib/ 2>/dev/null || true");
     system("cp -r /etc/terminfo " CONTAINER_ROOT "/etc/ 2>/dev/null || true");
     
     // 從 /usr/share/terminfo 複製
     system("cp /usr/share/terminfo/l/linux " CONTAINER_ROOT "/usr/share/terminfo/l/ 2>/dev/null || true");
     system("cp /usr/share/terminfo/x/xterm " CONTAINER_ROOT "/usr/share/terminfo/x/ 2>/dev/null || true");
     system("cp /usr/share/terminfo/x/xterm-256color " CONTAINER_ROOT "/usr/share/terminfo/x/ 2>/dev/null || true");
     system("cp /usr/share/terminfo/v/vt100 " CONTAINER_ROOT "/usr/share/terminfo/v/ 2>/dev/null || true");
     
     // 如果上述都失敗，從 /lib/terminfo 複製 (某些系統的位置)
     system("cp /lib/terminfo/l/linux " CONTAINER_ROOT "/lib/terminfo/l/linux 2>/dev/null || true");
     system("cp /lib/terminfo/x/xterm " CONTAINER_ROOT "/lib/terminfo/x/xterm 2>/dev/null || true");


    // 複製 terminfo 資料庫 (讓 top, htop 等能正常顯示)
    system("mkdir -p " CONTAINER_ROOT "/usr/share/terminfo");
    system("cp -r /usr/share/terminfo/* " CONTAINER_ROOT "/usr/share/terminfo/ 2>/dev/null || true");
}


void clear_copy(){
         // 創建簡單的清屏腳本（直接使用 ANSI 轉義序列）
         FILE *clear_script = fopen(CONTAINER_ROOT "/bin/clear_alt", "w");
         if (clear_script) {
             fprintf(clear_script, "#!/bin/bash\n");
             fprintf(clear_script, "printf '\\033[2J\\033[H'\n");
             fclose(clear_script);
             chmod(CONTAINER_ROOT "/bin/clear_alt", 0755);
         }
         
         // 創建 cls 別名
         system("ln -sf /bin/clear_alt " CONTAINER_ROOT "/bin/cls 2>/dev/null || true");
         
         // 覆蓋原有的 clear 指令，使其使用簡單的 ANSI 轉義序列
         FILE *clear_main = fopen(CONTAINER_ROOT "/bin/clear", "w");
         if (clear_main) {
             fprintf(clear_main, "#!/bin/bash\n");
             fprintf(clear_main, "printf '\\033[2J\\033[H'\n");
             fclose(clear_main);
             chmod(CONTAINER_ROOT "/bin/clear", 0755);
         }
}

 void device_copy(){
      // 創建一些必要的設備文件
      system("mknod " CONTAINER_ROOT "/dev/null c 1 3 2>/dev/null || true");
      system("mknod " CONTAINER_ROOT "/dev/zero c 1 5 2>/dev/null || true");
      system("mknod " CONTAINER_ROOT "/dev/random c 1 8 2>/dev/null || true");
      system("mknod " CONTAINER_ROOT "/dev/urandom c 1 9 2>/dev/null || true");
      system("mknod " CONTAINER_ROOT "/dev/tty c 5 0 2>/dev/null || true");
      system("mknod " CONTAINER_ROOT "/dev/console c 5 1 2>/dev/null || true");
      
      // 複製當前終端設備到容器中
      system("cp -a /dev/pts " CONTAINER_ROOT "/dev/ 2>/dev/null || true");
      system("chmod 666 " CONTAINER_ROOT "/dev/null " CONTAINER_ROOT "/dev/zero " CONTAINER_ROOT "/dev/tty " CONTAINER_ROOT "/dev/console 2>/dev/null || true");
 }

void lib_copy(){
        // 複製重要的庫文件
        printf("正在安裝系統庫...\n");
        system("cp -r /lib/x86_64-linux-gnu/* " CONTAINER_ROOT "/lib/x86_64-linux-gnu/ 2>/dev/null || true");
        system("cp -r /usr/lib/x86_64-linux-gnu/* " CONTAINER_ROOT "/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true");
        system("cp /lib64/ld-linux-x86-64.so.* " CONTAINER_ROOT "/lib64/ 2>/dev/null || true");

         // 創建基本的 /etc 文件
        system("echo 'root:x:0:0:root:/root:/bin/bash' > " CONTAINER_ROOT "/etc/passwd");
        system("echo 'root:x:0:' > " CONTAINER_ROOT "/etc/group");
        system("echo 'container' > " CONTAINER_ROOT "/etc/hostname");
}

void vim_copy(){
    
    
    // 複製 vim 運行時文件
    // printf("正在安裝 vim 支援...\n");
    system("mkdir -p " CONTAINER_ROOT "/usr/share/vim");
    system("cp -r /usr/share/vim/vim* " CONTAINER_ROOT "/usr/share/vim/ 2>/dev/null || true");
    
    // 創建簡單的 vimrc 配置文件來避免錯誤
    system("mkdir -p " CONTAINER_ROOT "/etc/vim");
    system("echo 'set nocompatible' > " CONTAINER_ROOT "/etc/vim/vimrc");
    system("echo 'set backspace=indent,eol,start' >> " CONTAINER_ROOT "/etc/vim/vimrc");
    system("echo 'syntax on' >> " CONTAINER_ROOT "/etc/vim/vimrc");
    system("echo 'set background=dark' >> " CONTAINER_ROOT "/etc/vim/vimrc");
    system("echo 'set number' >> " CONTAINER_ROOT "/etc/vim/vimrc");
    
    // 創建用戶級別的 vimrc
    system("mkdir -p " CONTAINER_ROOT "/root");
    system("echo 'set nocompatible' > " CONTAINER_ROOT "/root/.vimrc");
    system("echo 'set backspace=indent,eol,start' >> " CONTAINER_ROOT "/root/.vimrc");
}

// 複製指令和其依賴庫的函數
void copy_command_with_libs(const char* cmd_path, const char* dest_name) {
    char copy_cmd[512];
    char lib_cmd[1024];
    
    // 複製指令本身
    snprintf(copy_cmd, sizeof(copy_cmd), 
             "cp %s " CONTAINER_ROOT "/bin/%s 2>/dev/null && chmod +x " CONTAINER_ROOT "/bin/%s", 
             cmd_path, dest_name, dest_name);
    system(copy_cmd);
    
    // 複製指令需要的庫文件
    snprintf(lib_cmd, sizeof(lib_cmd),
             "ldd %s 2>/dev/null | grep -o '/lib[^ ]*' | while read lib; do "
             "if [ -f \"$lib\" ]; then "
             "mkdir -p " CONTAINER_ROOT "/$(dirname \"$lib\") 2>/dev/null; "
             "cp \"$lib\" " CONTAINER_ROOT "/\"$lib\" 2>/dev/null; "
             "fi; done", cmd_path);
    system(lib_cmd);
}

//man instruction copy to container
 void man_command_copy(){
         
    // 複製 man-db 相關的庫文件
    system("cp /usr/lib/man-db/libmandb-*.so " CONTAINER_ROOT "/usr/lib/x86_64-linux-gnu/ 2>/dev/null || true");
    system("mkdir -p " CONTAINER_ROOT "/usr/lib/man-db");
    system("cp /usr/lib/man-db/* " CONTAINER_ROOT "/usr/lib/man-db/ 2>/dev/null || true");
    
    // 複製 man-db 的輔助程序及其依賴
    system("mkdir -p " CONTAINER_ROOT "/usr/libexec/man-db");
    
    // 複製並設置權限
    char *man_helpers[] = {
        "/usr/libexec/man-db/zsoelim",
        "/usr/libexec/man-db/manconv", 
        "/usr/libexec/man-db/globbing",
        "/usr/libexec/man-db/mandb",
        NULL
    };
    
    for (int i = 0; man_helpers[i]; i++) {
        char cmd[512];
        // 複製程序本身
        snprintf(cmd, sizeof(cmd), 
                "cp %s " CONTAINER_ROOT "/usr/libexec/man-db/ 2>/dev/null && chmod +x " CONTAINER_ROOT "/usr/libexec/man-db/$(basename %s)",
                man_helpers[i], man_helpers[i]);
        system(cmd);
        
        // 複製其依賴庫
        snprintf(cmd, sizeof(cmd),
                "ldd %s 2>/dev/null | grep -o '/lib[^ ]*' | while read lib; do "
                "if [ -f \"$lib\" ]; then "
                "mkdir -p " CONTAINER_ROOT "/$(dirname \"$lib\"); "
                "cp \"$lib\" " CONTAINER_ROOT "/\"$lib\" 2>/dev/null; "
                "fi; done",
                man_helpers[i]);
        system(cmd);
    }
    
    
    // 複製 man 頁面文檔（選擇性複製常用的）
    // printf("正在安裝 man 手冊...\n");
    system("mkdir -p " CONTAINER_ROOT "/usr/share/man/man1");
    system("mkdir -p " CONTAINER_ROOT "/usr/share/man/man5");
    system("mkdir -p " CONTAINER_ROOT "/usr/share/man/man8");
    
    // 複製常用指令的 man 頁面
    char *man_pages[] = {
        "ls", "cat", "grep", "find", "ps", "top", "bash", "cp", "mv", "rm",
        "chmod", "chown", "curl", "wget", "vim", "nano", "tar", "gzip","man",
        NULL
    };
    
    for (int i = 0; man_pages[i]; i++) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), 
                "cp /usr/share/man/man1/%s.1.gz " CONTAINER_ROOT "/usr/share/man/man1/ 2>/dev/null || true",
                man_pages[i]);
        system(cmd);
    }
    
    // 複製 man 的配置文件
    system("cp /etc/manpath.config " CONTAINER_ROOT "/etc/ 2>/dev/null || true");
    
    // 複製 groff 資料文件 (man 需要這些來格式化文檔)
    // printf("正在安裝 groff 支援...\n");
    system("mkdir -p " CONTAINER_ROOT "/usr/share/groff");
    system("cp -r /usr/share/groff/* " CONTAINER_ROOT "/usr/share/groff/ 2>/dev/null || true");
    
    // 複製 groff 的字體文件
    system("mkdir -p " CONTAINER_ROOT "/usr/share/groff/current");
    system("cp -r /usr/share/groff/current/* " CONTAINER_ROOT "/usr/share/groff/current/ 2>/dev/null || true");
}

void set_alias(){
        // 創建簡單的 bashrc 來設置別名
        system("echo 'alias cls=clear_alt' > " CONTAINER_ROOT "/etc/bash.bashrc");
        system("echo 'alias ll=\"ls -la\"' >> " CONTAINER_ROOT "/etc/bash.bashrc");
        system("echo 'export TERM=xterm' >> " CONTAINER_ROOT "/etc/bash.bashrc");
        system("echo 'export TERMINFO=/usr/share/terminfo:/lib/terminfo:/etc/terminfo' >> " CONTAINER_ROOT "/etc/bash.bashrc");
    
    
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
    cgroup_limits_t* limits = (cgroup_limits_t*)arg;
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
    
    (void)arg;  // 避免未使用參數的警告
    
    printf("=== 進入容器環境 ===\n");
    printf("PID: %d\n", getpid());
    printf("PPID: %d\n", getppid());
    
    // 創建容器根目錄
    if (mkdir(CONTAINER_ROOT, 0755) == -1 && errno != EEXIST) {
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
        snprintf(path, sizeof(path), "%s/%s", CONTAINER_ROOT, dirs[i]);
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
            copy_command_with_libs(basic_commands[i].source_path, basic_commands[i].dest_name);
            // printf("  已安裝: %s\n", basic_commands[i].dest_name);
        }
    }

    terminfo_copy();

    lib_copy();

    man_command_copy();

    set_alias();
     
    clear_copy();

    device_copy();

    vim_copy();
    
    // 文件系統隔離：改變根目錄並切換工作目錄
    // chroot: 將進程的根目錄改為容器目錄，實現文件系統隔離
    // chdir: 切換到新的根目錄，避免工作目錄錯誤
    if (chroot(CONTAINER_ROOT) == -1 || chdir("/") == -1) {
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
    
    // 配置資源限制
    static cgroup_limits_t limits = {
        .memory_limit_mb = 512,    // 限制記憶體為 512 MB
        .cpu_shares = 512,         // CPU 份額為 512 (預設的一半)
        .cpu_quota_us = 50000,     // CPU 配額為 50% (50000/100000)
        .pids_max = 100            // 最多 100 個進程
    };
    
    // 創建子進程，使用新的命名空間
    pid_t pid = clone(container_init, 
                      child_stack + STACK_SIZE,
                      CLONE_NEWPID |    // 新的 PID 命名空間
                      CLONE_NEWNS |     // 新的掛載命名空間
                      CLONE_NEWUTS |    // 新的主機名命名空間
                      CLONE_NEWIPC |    // 新的 IPC 命名空間
                      SIGCHLD,          // 子進程結束時發送 SIGCHLD
                      &limits);         // 傳遞限制配置
    
    if (pid == -1) {
        perror("clone");
        exit(EXIT_FAILURE);
    }
    
    printf("容器已創建，PID: %d\n\n", pid);
    
    // 設置資源限制
    setup_cgroup_limits(pid, &limits);
    printf("\n");
    
    // 等待子進程結束
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        exit(EXIT_FAILURE);
    }
    
    printf("容器已退出\n");
    
    // 清理 cgroup
    cleanup_cgroup();
    
    // 清理容器目錄
    if (system("rm -rf " CONTAINER_ROOT) == -1) {
        perror("清理容器目錄");
    }
    
    return 0;
}