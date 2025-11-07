#ifndef ROOTFS_H
#define ROOTFS_H

#define BASE_ROOTFS_PATH "/tmp/docker_in_c_base_rootfs"

/**
 * 檢查基礎 rootfs 是否已存在
 * @return 1 存在，0 不存在
 */
int check_base_rootfs_exists(void);

/**
 * 創建基礎 rootfs（只需執行一次）
 * 包含所有需要的系統文件、命令和庫
 * @return 0 成功，-1 失敗
 */
int create_base_rootfs(void);

/**
 * 為容器準備 rootfs（使用基礎 rootfs）
 * 可以選擇複製或使用 bind mount
 * @param container_root 容器根目錄路徑
 * @param use_copy 是否複製（0=bind mount, 1=複製）
 * @return 0 成功，-1 失敗
 */
int setup_container_rootfs(const char* container_root, int use_copy);

/**
 * 複製命令及其依賴庫
 * @param cmd_path 命令路徑
 * @param dest_name 目標名稱
 * @param container_root 容器根目錄
 */
void copy_command_with_libs(const char* cmd_path, const char* dest_name, const char* container_root);

/**
 * 複製 terminfo 資料庫
 * @param container_root 容器根目錄
 */
void terminfo_copy(const char* container_root);

/**
 * 創建基本的系統文件
 * @param container_root 容器根目錄
 */
void create_basic_system_files(const char* container_root);

/**
 * 創建設備文件
 * @param container_root 容器根目錄
 */
void device_copy(const char* container_root);

/**
 * 設置 bash 別名和環境變量
 * @param container_root 容器根目錄
 */
void set_alias(const char* container_root);

/**
 * 複製 vim 配置和運行時文件
 * @param container_root 容器根目錄
 */
void vim_copy(const char* container_root);

/**
 * 複製 man 命令及其相關文件
 * @param container_root 容器根目錄
 */
void man_command_copy(const char* container_root);

/**
 * 複製 apt-get 及其相關工具和配置
 * @param container_root 容器根目錄
 */
void apt_get_copy(const char* container_root);

#endif // ROOTFS_H

