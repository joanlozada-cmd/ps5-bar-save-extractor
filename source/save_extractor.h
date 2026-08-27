#ifndef __SAVE_EXTRACTOR_H__
#define __SAVE_EXTRACTOR_H__

#include <stdint.h>
#include <stdarg.h>
#include "bar_file.h"

#define MAIN_DIR "/mnt/usb0/PS5/EXPORT/BACKUP/"
#define SAVE_DUMP_DIR MAIN_DIR "SAVES_DUMP"
#define LOG_FILE MAIN_DIR "ps5-bar-save-extractor.log"
#define MANIFEST_FILE SAVE_DUMP_DIR "/manifest.tsv"
#define MAX_BUFFERED_FILE_SIZE 0x80000000ULL /* 2 GiB */

typedef struct _bar_session {
    int         context;
    int         mode;
    int         version;
    uint8_t     key[16];
    uint8_t     iv[12];
    uint64_t    n_segments;
    bar_file_segment_metadata* segment_metadata;
    bar_file_segment_hash* segment_hash;
    uint8_t     complete_hash[16];
} bar_session;

int main(void);
int read_header(bar_session* session);
uint64_t decrypt_segment(bar_session* session, void** buffer, int segment_id, int special_file, int flag_validate);
int read_from_archive(void* buffer, uint64_t size, uint64_t offset);
int read_from_archive_number(uint64_t archive_number, void* buffer, uint64_t size, uint64_t offset);
void log_printf(const char *format, ...);
void manifest_printf(const char *format, ...);

#endif
