#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <sys/types.h>

/**
 * 獲取真實用戶 UID（即使在 sudo 下運行）
 * 如果程式是通過 sudo 運行，將返回原始用戶的 UID
 * @return 真實用戶的 UID
 */
uid_t get_real_uid(void);

/**
 * 獲取真實用戶 GID（即使在 sudo 下運行）
 * 如果程式是通過 sudo 運行，將返回原始用戶的 GID
 * @return 真實用戶的 GID
 */
gid_t get_real_gid(void);

/**
 * 設置用戶命名空間的 UID/GID 映射
 * 將容器內的 root (UID 0) 映射到主機真實用戶，實現權限隔離
 * @param pid 目標進程 ID
 * @return 0 成功，-1 失敗
 */
int setup_user_namespace(pid_t pid);

#endif // NAMESPACE_H

