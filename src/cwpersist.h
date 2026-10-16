#ifndef CWPERSIST_H
#define CWPERSIST_H

/*
 * CW Persist — التخزين الدائم
 * 
 * التوصية الفورية #2: إضافة persistent storage
 * 
 * المشكلة: فقدان البيانات عند restart
 * الحل: حفظ البيانات في ملف
 * 
 * الميزة: لا فقدان للبيانات، تحليل تاريخي، backup/restore
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>

#define CWPERSIST_MAX_PATH 256
#define CWPERSIST_VERSION 1

/* File header */
typedef struct {
    uint32_t magic;           /* 0x43575053 = "CWPS" */
    uint32_t version;         /* file format version */
    uint32_t checksum;        /* CRC32 of data */
    time_t timestamp;         /* save time */
    uint32_t data_size;       /* size of data section */
} cwpersist_header_t;

/* Persist state */
typedef struct {
    char base_path[CWPERSIST_MAX_PATH];
    int enabled;
    int auto_save_interval;   /* seconds */
    time_t last_save;
    int total_saves;
    int total_loads;
    int save_errors;
    int load_errors;
} cwpersist_state_t;

static cwpersist_state_t g_cwpersist_state = {0};

/* CRC32 calculation */
static inline uint32_t cwpersist_crc32(const void *data, size_t size) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *buf = (const uint8_t *)data;
    
    size_t i;
    int j;
    for (i = 0; i < size; i++) {
        crc ^= buf[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

/* تهيئة */
static inline int cwpersist_init(const char *base_path, int auto_save_interval) {
    if (!base_path) return -1;
    
    strncpy(g_cwpersist_state.base_path, base_path, CWPERSIST_MAX_PATH - 1);
    g_cwpersist_state.base_path[CWPERSIST_MAX_PATH - 1] = '\0';
    g_cwpersist_state.enabled = 1;
    g_cwpersist_state.auto_save_interval = auto_save_interval;
    g_cwpersist_state.last_save = 0;
    g_cwpersist_state.total_saves = 0;
    g_cwpersist_state.total_loads = 0;
    g_cwpersist_state.save_errors = 0;
    g_cwpersist_state.load_errors = 0;
    
    /* Create directory if it doesn't exist */
    mkdir(base_path, 0755);
    
    return 0;
}

/* حفظ بيانات */
static inline int cwpersist_save(const char *filename, const void *data, size_t size) {
    if (!g_cwpersist_state.enabled || !filename || !data || size == 0) {
        return -1;
    }
    
    char filepath[CWPERSIST_MAX_PATH * 2];
    snprintf(filepath, sizeof(filepath), "%s/%s", g_cwpersist_state.base_path, filename);
    
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        g_cwpersist_state.save_errors++;
        return -1;
    }
    
    /* Write header */
    cwpersist_header_t header;
    header.magic = 0x43575053;  /* "CWPS" */
    header.version = CWPERSIST_VERSION;
    header.checksum = cwpersist_crc32(data, size);
    header.timestamp = time(NULL);
    header.data_size = (uint32_t)size;
    
    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        g_cwpersist_state.save_errors++;
        return -1;
    }
    
    /* Write data */
    if (fwrite(data, size, 1, fp) != 1) {
        fclose(fp);
        g_cwpersist_state.save_errors++;
        return -1;
    }
    
    fclose(fp);
    
    g_cwpersist_state.total_saves++;
    g_cwpersist_state.last_save = time(NULL);
    
    return 0;
}

/* تحميل بيانات */
static inline int cwpersist_load(const char *filename, void *data, size_t max_size, size_t *actual_size) {
    if (!g_cwpersist_state.enabled || !filename || !data || max_size == 0) {
        return -1;
    }
    
    char filepath[CWPERSIST_MAX_PATH * 2];
    snprintf(filepath, sizeof(filepath), "%s/%s", g_cwpersist_state.base_path, filename);
    
    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    /* Read header */
    cwpersist_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    /* Validate header */
    if (header.magic != 0x43575053) {
        fclose(fp);
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    if (header.version != CWPERSIST_VERSION) {
        fclose(fp);
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    if (header.data_size > max_size) {
        fclose(fp);
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    /* Read data */
    if (fread(data, header.data_size, 1, fp) != 1) {
        fclose(fp);
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    fclose(fp);
    
    /* Validate checksum */
    uint32_t checksum = cwpersist_crc32(data, header.data_size);
    if (checksum != header.checksum) {
        g_cwpersist_state.load_errors++;
        return -1;
    }
    
    if (actual_size) {
        *actual_size = header.data_size;
    }
    
    g_cwpersist_state.total_loads++;
    
    return 0;
}

/* حذف ملف */
static inline int cwpersist_delete(const char *filename) {
    if (!g_cwpersist_state.enabled || !filename) {
        return -1;
    }
    
    char filepath[CWPERSIST_MAX_PATH * 2];
    snprintf(filepath, sizeof(filepath), "%s/%s", g_cwpersist_state.base_path, filename);
    
    return remove(filepath);
}

/* فحص إذا كان الملف موجود */
static inline int cwpersist_exists(const char *filename) {
    if (!g_cwpersist_state.enabled || !filename) {
        return 0;
    }
    
    char filepath[CWPERSIST_MAX_PATH * 2];
    snprintf(filepath, sizeof(filepath), "%s/%s", g_cwpersist_state.base_path, filename);
    
    struct stat st;
    return (stat(filepath, &st) == 0);
}

/* Auto-save check */
static inline int cwpersist_should_auto_save(void) {
    if (!g_cwpersist_state.enabled || g_cwpersist_state.auto_save_interval <= 0) {
        return 0;
    }
    
    time_t now = time(NULL);
    return (difftime(now, g_cwpersist_state.last_save) >= g_cwpersist_state.auto_save_interval);
}

/* الحصول على الإحصائيات */
static inline void cwpersist_get_stats(int *saves, int *loads, int *save_errors, int *load_errors) {
    if (saves) *saves = g_cwpersist_state.total_saves;
    if (loads) *loads = g_cwpersist_state.total_loads;
    if (save_errors) *save_errors = g_cwpersist_state.save_errors;
    if (load_errors) *load_errors = g_cwpersist_state.load_errors;
}

/* تصفير الإحصائيات */
static inline void cwpersist_reset_stats(void) {
    g_cwpersist_state.total_saves = 0;
    g_cwpersist_state.total_loads = 0;
    g_cwpersist_state.save_errors = 0;
    g_cwpersist_state.load_errors = 0;
}

#endif /* CWPERSIST_H */
