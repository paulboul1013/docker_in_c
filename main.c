#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include "cgroup.h"
#include "namespace.h"
#include "rootfs.h"

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ROOT_PREFIX "/tmp/container_root_"
#define CGROUP_NAME_PREFIX "docker_in_c_container_"

static char child_stack[STACK_SIZE];

// 容器初始化參數結構
typedef struct {
    cgroup_limits_t* limits;
    int sync_pipe[2];          // 用於父子進程同步的管道
    char container_root[256];  // 容器根目錄路徑
    char cgroup_name[128];     // cgroup 名稱
    int container_id;          // 容器 ID
} container_init_args_t;

// 創建基本的設備文件（當 devtmpfs 掛載失敗時的備用方案）
static void create_basic_devices(const char* container_root) {
    char dev_path[512];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", container_root);
    mkdir(dev_path, 0755);
    
    // 創建基本設備文件
    struct {
        const char* name;
        mode_t mode;
        dev_t dev;
    } devices[] = {
        {"/dev/null", S_IFCHR | 0666, makedev(1, 3)},
        {"/dev/zero", S_IFCHR | 0666, makedev(1, 5)},
        {"/dev/random", S_IFCHR | 0666, makedev(1, 8)},
        {"/dev/urandom", S_IFCHR | 0666, makedev(1, 9)},
        {"/dev/tty", S_IFCHR | 0666, makedev(5, 0)},
        {"/dev/console", S_IFCHR | 0600, makedev(5, 1)},
        {"/dev/full", S_IFCHR | 0666, makedev(1, 7)},
    };
    
    for (size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); ++i) {
        char device_path[512];
        snprintf(device_path, sizeof(device_path), "%s%s", container_root, devices[i].name);
        
        // 如果文件已存在，先刪除
        unlink(device_path);
        
        // 創建設備文件
        if (mknod(device_path, devices[i].mode, devices[i].dev) == -1) {
            // 靜默失敗，因為這只是備用方案
            // fprintf(stderr, "警告: 無法創建設備 %s: %s\n", device_path, strerror(errno));
        } else {
            chmod(device_path, devices[i].mode & 0777);
        }
    }
}

// 創建虛擬的 meminfo 文件以反映 cgroup 限制
void create_virtual_meminfo(const char* path, long memory_limit_mb) {
    FILE* meminfo = fopen(path, "w");
    if (!meminfo) {
        // fprintf(stderr, "警告: 無法創建虛擬 meminfo: %s (錯誤: %s)\n", path, strerror(errno));
        return;
    }
    
    // 計算以 KB 為單位的記憶體值
    long mem_total_kb = memory_limit_mb * 1024;
    long mem_free_kb = mem_total_kb * 80 / 100;      // 假設 80% 可用
    long mem_available_kb = mem_total_kb * 75 / 100; // 真正可分配的
    long cached_kb = mem_total_kb * 15 / 100;        // 模擬 15% 被快取使用
    long buffers_kb = mem_total_kb * 5 / 100;        // 模擬 5% 用於緩衝區
    
    // 創建簡化的 meminfo，包含 free 命令需要的關鍵字段
    fprintf(meminfo, "MemTotal:       %ld kB\n", mem_total_kb);
    fprintf(meminfo, "MemFree:        %ld kB\n", mem_free_kb);
    fprintf(meminfo, "MemAvailable:   %ld kB\n", mem_available_kb);
    fprintf(meminfo, "Buffers:        %ld kB\n", buffers_kb);        // 設置合理值
    fprintf(meminfo, "Cached:         %ld kB\n", cached_kb);         // 設置合理值
    fprintf(meminfo, "SwapCached:          0 kB\n");                  // 容器通常無 swap
    fprintf(meminfo, "Active:         %ld kB\n", mem_total_kb - mem_free_kb);
    fprintf(meminfo, "Inactive:            0 kB\n");
    fprintf(meminfo, "SwapTotal:           0 kB\n");                  // 容器不提供 swap
    fprintf(meminfo, "SwapFree:            0 kB\n");
    fprintf(meminfo, "Dirty:               0 kB\n");                  // 簡化：沒有髒頁
    fprintf(meminfo, "Writeback:           0 kB\n");
    fprintf(meminfo, "Shmem:               0 kB\n");
    fprintf(meminfo, "Slab:                0 kB\n");                  // 內核數據，設 0 合理
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
        // fprintf(stderr, "等待父進程設置映射時失敗\n");
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
    
    // printf("=== 進入容器環境 (ID: %d) ===\n", args->container_id);
    // printf("PID: %d\n", getpid());
    // printf("PPID: %d\n", getppid());
    // printf("UID: %d, GID: %d\n", getuid(), getgid());
    // printf("容器根目錄: %s\n\n", container_root);
    
    // 使用基礎 rootfs 設置容器文件系統（快速！）
    // 模式選擇：
    //   0 = Bind Mount (最快，但容器間共享文件系統，/tmp 只讀)
    //   1 = 複製模式 (較慢但完全隔離，/tmp 可寫) ✅
    //   2 = OverlayFS (推薦：快速 + 隔離，但需要內核支援)
    if (setup_container_rootfs(container_root, 2) != 0) {  // 使用 OverlayFS
        fprintf(stderr, "錯誤: 無法設置容器文件系統\n");
        return -1;
    }
    
    // 在 chroot 之前，將主機的關鍵設備文件 bind mount 到容器路徑
    const char* device_paths[] = {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom", "/dev/tty", "/dev/console"};
    for (size_t i = 0; i < sizeof(device_paths) / sizeof(device_paths[0]); ++i) {
        char target_path[512];
        snprintf(target_path, sizeof(target_path), "%s%s", container_root, device_paths[i]);
        // 確保目標文件存在
        int fd = open(target_path, O_CREAT | O_WRONLY, 0666);
        if (fd != -1) close(fd);
        if (mount(device_paths[i], target_path, NULL, MS_BIND, NULL) == -1) {
            fprintf(stderr, "警告: 無法綁定設備 %s -> %s: %s\n", device_paths[i], target_path, strerror(errno));
        } else {
            chmod(target_path, 0666);
        }
    }

    // 在 chroot 之前創建虛擬 meminfo（在主機文件系統）
    if (limits && limits->memory_limit_mb > 0) {
        char meminfo_path[512];
        char tmp_dir[512];
        
        // 確保 /tmp 目錄存在
        snprintf(tmp_dir, sizeof(tmp_dir), "%s/tmp", container_root);
        mkdir(tmp_dir, 0777);
        chmod(tmp_dir, 01777);  // 設置 sticky bit
        
        snprintf(meminfo_path, sizeof(meminfo_path), "%s/tmp/meminfo.custom", container_root);
        create_virtual_meminfo(meminfo_path, limits->memory_limit_mb);
    }
    
    // 在 chroot 之前掛載 devtmpfs 到容器的 /dev 目錄
    char dev_path[512];
    snprintf(dev_path, sizeof(dev_path), "%s/dev", container_root);
    mkdir(dev_path, 0755);
    if (mount("devtmpfs", dev_path, "devtmpfs", 0, NULL) == -1) {
        // 在用戶命名空間中，devtmpfs 掛載可能失敗，使用備用方案：手動創建基本設備文件
        // fprintf(stderr, "警告: 無法掛載 devtmpfs 到 %s: %s，使用備用方案創建設備文件\n", dev_path, strerror(errno));
        create_basic_devices(container_root);
    }
    
    // 在 chroot 之前掛載 devpts 到容器的 /dev/pts 目錄
    char devpts_path[512];
    snprintf(devpts_path, sizeof(devpts_path), "%s/dev/pts", container_root);
    mkdir(devpts_path, 0755);
    if (mount("devpts", devpts_path, "devpts", MS_NOSUID | MS_NOEXEC, "ptmxmode=0666,newinstance") == -1) {
        if (mount("devpts", devpts_path, "devpts", MS_NOSUID | MS_NOEXEC, NULL) == -1) {
            fprintf(stderr, "警告: 無法掛載 devpts 到 %s: %s\n", devpts_path, strerror(errno));
        }
    }
    
    // 創建 /dev/ptmx 的符號連結（在 chroot 之前）
    char ptmx_link[512];
    snprintf(ptmx_link, sizeof(ptmx_link), "%s/dev/ptmx", container_root);
    unlink(ptmx_link); // 如果已存在則刪除
    symlink("/dev/pts/ptmx", ptmx_link);
    
    // 文件系統隔離：改變根目錄並切換工作目錄
    // chroot: 將進程的根目錄改為容器目錄，實現文件系統隔離
    // chdir: 切換到新的根目錄，避免工作目錄錯誤
    if (chroot(container_root) == -1 || chdir("/") == -1) {
        perror("文件系統隔離失敗");
        return -1;
    }
    
    // 掛載 proc 和 sys 文件系統
    if (mount("proc", "/proc", "proc", 0, NULL) == -1 || mount("sysfs", "/sys", "sysfs", 0, NULL) == -1) {
        // printf("警告: 無法掛載 /proc (某些指令如 top 可能無法正常工作)\n");
    }
    
    // 確保 /dev/pts 正確掛載（在 chroot 後驗證並重新掛載）
    // 先嘗試卸載可能存在的舊掛載
    umount("/dev/pts");
    mkdir("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "ptmxmode=0666,newinstance") == -1) {
        if (mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, NULL) == -1) {
            fprintf(stderr, "警告: 無法在 chroot 後掛載 /dev/pts: %s\n", strerror(errno));
        }
    }
    
    // 確保 /dev/ptmx 符號連結存在
    unlink("/dev/ptmx"); // 如果已存在則刪除
    symlink("/dev/pts/ptmx", "/dev/ptmx");
    
    // 確保 /var/lib/dpkg/info/format 檔案是 2.0（在 chroot 後強制設置）
    // 先確保目錄存在
    mkdir("/var/lib/dpkg", 0755);
    mkdir("/var/lib/dpkg/info", 0755);
    
    char format_path[512] = "/var/lib/dpkg/info/format";
    unlink(format_path); // 刪除可能存在的舊檔案
    FILE* format_file = fopen(format_path, "w");
    if (format_file) {
        fprintf(format_file, "2.0\n");
        fclose(format_file);
        chmod(format_path, 0644);
    }
    
    char format_new_path[512] = "/var/lib/dpkg/info/format-new";
    unlink(format_new_path); // 刪除可能存在的舊檔案
    format_file = fopen(format_new_path, "w");
    if (format_file) {
        fprintf(format_file, "2.0\n");
        fclose(format_file);
        chmod(format_new_path, 0644);
    }
    
    // 掛載虛擬 meminfo（已在 chroot 之前創建）
    if (limits && limits->memory_limit_mb > 0) {
        // 檢查 meminfo 文件是否存在
        if (access("/tmp/meminfo.custom", F_OK) != 0) {
            // printf("⚠️  警告: 虛擬 meminfo 文件不存在\n");
        } else {
            // 使用 bind mount 將虛擬 meminfo 掛載到 /proc/meminfo
            if (mount("/tmp/meminfo.custom", "/proc/meminfo", NULL, MS_BIND, NULL) == -1) {
                // printf("⚠️  警告: 無法掛載虛擬 meminfo: %s\n", strerror(errno));
                // printf("    提示: free 命令將顯示主機記憶體，但 cgroup 限制仍然生效\n");
            } else {
                // printf("✅ 已設置虛擬記憶體視圖: %ld MB\n", limits->memory_limit_mb);
            }
        }
    }
    
    // devtmpfs 和 devpts 已在 chroot 之前掛載

    // printf("\n容器環境已準備就緒！\n");
    // printf("輸入 'exit' 離開容器\n\n");
    
    // 執行 bash
    if (execve("/bin/bash", argv, envp) == -1) {
        perror("execve");
        return -1;
    }
    
    return 0;
}


int main() {
    
    // 檢查並創建基礎 rootfs（如果需要）
    if (!check_base_rootfs_exists()) {
        // printf("⚠️  未找到基礎容器映像\n");
        // printf("提示: 這是首次運行，需要創建基礎映像（約需 10-30 秒）\n");
        // printf("      後續容器啟動將會非常快速\n\n");
        
        char response[10];
        printf("是否現在創建? (y/n): ");
        fflush(stdout);
        
        if (fgets(response, sizeof(response), stdin) == NULL || 
            (response[0] != 'y' && response[0] != 'Y')) {
            printf("已取消\n");
            return 0;
        }
        
        if (create_base_rootfs() != 0) {
            fprintf(stderr, "錯誤: 創建基礎 rootfs 失敗\n");
            return 1;
        }
    } else {
        // printf("✓ 找到基礎容器映像: %s\n\n", BASE_ROOTFS_PATH);
    }
    
    // printf("正在創建容器...\n");
    
    // 生成唯一的容器 ID（使用當前時間戳和進程ID）
    int container_id = (int)time(NULL) % 100000 + getpid() % 1000;
    
    // printf("容器 ID: %d\n", container_id);
    
    // 配置資源限制
    static cgroup_limits_t limits = {
        .memory_limit_mb = 1024,    // 限制記憶體為 512 MB
        .cpu_shares = 1024,         // CPU 份額為 512 (預設的一半)
        .cpu_quota_us = 100000,     // CPU 配額為 50% (50000/100000)
        .pids_max = 100            // 最多 100 個進程
    };
    
    // 創建用於同步的管道
    static container_init_args_t args;
    args.limits = &limits;
    args.container_id = container_id;
    
    // 生成唯一的容器根目錄和 cgroup 名稱
    snprintf(args.container_root, sizeof(args.container_root), "%s%d", CONTAINER_ROOT_PREFIX, container_id);
    snprintf(args.cgroup_name, sizeof(args.cgroup_name), "%s%d", CGROUP_NAME_PREFIX, container_id);
    
    // printf("容器根目錄: %s\n", args.container_root);
    // printf("Cgroup 名稱: %s\n\n", args.cgroup_name);
    
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
    
    // printf("容器已創建，PID: %d\n\n", pid);
    
    // 設置用戶命名空間映射
    if (setup_user_namespace(pid) == -1) {
        fprintf(stderr, "警告: 用戶命名空間設置失敗，但容器將繼續運行\n");
    }
    // printf("\n");
    
    // 通知子進程映射已完成，可以繼續執行
    close(args.sync_pipe[1]);
    
    // 設置資源限制
    setup_cgroup_limits(pid, &limits, args.cgroup_name);
    // printf("\n");
    
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

    // 清理 OverlayFS 產生的 upper/work 目錄
    char overlay_cleanup_cmd[1024];
    char upper_dir[512], work_dir[512];
    snprintf(upper_dir, sizeof(upper_dir), "%s_upper", args.container_root);
    snprintf(work_dir, sizeof(work_dir), "%s_work", args.container_root);
    snprintf(overlay_cleanup_cmd, sizeof(overlay_cleanup_cmd), "rm -rf %s", upper_dir);
    if (system(overlay_cleanup_cmd) == -1) {
        perror("清理 OverlayFS 目錄");
    }
    snprintf(overlay_cleanup_cmd, sizeof(overlay_cleanup_cmd), "rm -rf %s", work_dir);
    if (system(overlay_cleanup_cmd) == -1) {
        perror("清理 OverlayFS 目錄");
    }
    
    printf("容器 %d 清理完成\n", container_id);
    return 0;
}