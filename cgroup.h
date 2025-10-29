#ifndef CGROUP_H
#define CGROUP_H

#include <sys/types.h>

// cgroup root directory version 2
#define CGROUP_ROOT "/sys/fs/cgroup"

// 資源限制配置結構
typedef struct {
    long memory_limit_mb;      // 記憶體限制 (MB)
    int cpu_shares;            // CPU 份額 (預設 1024)
    int cpu_quota_us;          // CPU 配額 (微秒/100ms週期)
    int pids_max;              // 最大進程數
} cgroup_limits_t;

/**
 * 寫入 cgroup 檔案的輔助函數
 * @param cgroup_path cgroup 路徑
 * @param filename 檔案名稱
 * @param value 要寫入的值
 * @return 0 成功，-1 失敗
 */
int write_cgroup_file(const char* cgroup_path, const char* filename, const char* value);

/**
 * 檢測 cgroup 版本 (v1 或 v2)
 * @return 2 為 cgroup v2, 1 為 cgroup v1, 0 為不支援
 */
int detect_cgroup_version(void);

/**
 * 創建並配置 cgroup v2
 * @param pid 進程 ID
 * @param limits 資源限制配置
 * @param cgroup_name cgroup 名稱
 * @return 0 成功，-1 失敗
 */
int setup_cgroup_v2(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name);

/**
 * 創建並配置 cgroup v1
 * @param pid 進程 ID
 * @param limits 資源限制配置
 * @param cgroup_name cgroup 名稱
 * @return 0 成功，-1 失敗
 */
int setup_cgroup_v1(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name);

/**
 * 設置 cgroup 資源限制（自動檢測版本）
 * @param pid 進程 ID
 * @param limits 資源限制配置
 * @param cgroup_name cgroup 名稱
 * @return 0 成功，-1 失敗
 */
int setup_cgroup_limits(pid_t pid, const cgroup_limits_t* limits, const char* cgroup_name);

/**
 * 清理 cgroup
 * @param cgroup_name cgroup 名稱
 */
void cleanup_cgroup(const char* cgroup_name);

#endif // CGROUP_H

