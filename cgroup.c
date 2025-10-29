#include "cgroup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

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
int detect_cgroup_version(void) {
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

