#include "namespace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

// 獲取真實用戶 UID（即使在 sudo 下運行）
uid_t get_real_uid(void) {
    char *sudo_uid = getenv("SUDO_UID");
    if (sudo_uid) {
        return (uid_t)atoi(sudo_uid);
    }
    return getuid();
}

// 獲取真實用戶 GID（即使在 sudo 下運行）
gid_t get_real_gid(void) {
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

