#include "save_extractor.h"
#include "bar_file.h"
#include "bar_srv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_TRACKED_TITLES 512
#define TITLE_KEY_LEN 96

typedef enum {
    SAVE_KIND_NONE = 0,
    SAVE_KIND_PS4,
    SAVE_KIND_PS5,
    SAVE_KIND_SYSTEM
} save_kind;

static char g_title_keys[MAX_TRACKED_TITLES][TITLE_KEY_LEN];
static int g_title_count = 0;

static int starts_with(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static save_kind classify_save_path(const char *path) {
    if (starts_with(path, "/system_data/savedata/") ||
        starts_with(path, "/system_data/savedata_prospero/") ||
        starts_with(path, "/user/savedata/"))
        return SAVE_KIND_SYSTEM;

    if (starts_with(path, "/user/home/") &&
        (strstr(path, "/savedata_prospero/") ||
         strstr(path, "/savedata_prospero_meta/") ||
         strstr(path, "/savedata_prospero_for_cloud/")))
        return SAVE_KIND_PS5;

    if (starts_with(path, "/user/home/") &&
        (strstr(path, "/savedata/") ||
         strstr(path, "/savedata_meta/")))
        return SAVE_KIND_PS4;

    return SAVE_KIND_NONE;
}

static const char *kind_name(save_kind kind) {
    switch (kind) {
        case SAVE_KIND_PS4: return "PS4";
        case SAVE_KIND_PS5: return "PS5";
        case SAVE_KIND_SYSTEM: return "SYSTEM";
        default: return "OTHER";
    }
}

static void copy_component_after(const char *path, const char *token, char *out, size_t out_size) {
    const char *p = strstr(path, token);
    size_t len;

    if (!out_size) return;
    out[0] = '\0';
    if (!p) return;

    p += strlen(token);
    const char *end = strchr(p, '/');
    len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static void parse_user_and_title(const char *path, save_kind kind,
                                 char *user_id, size_t user_size,
                                 char *title_id, size_t title_size) {
    if (user_size) user_id[0] = '\0';
    if (title_size) title_id[0] = '\0';

    if (starts_with(path, "/user/home/"))
        copy_component_after(path, "/user/home/", user_id, user_size);

    if (kind == SAVE_KIND_PS4) {
        if (strstr(path, "/savedata_meta/user/"))
            copy_component_after(path, "/savedata_meta/user/", title_id, title_size);
        else
            copy_component_after(path, "/savedata/", title_id, title_size);
    } else if (kind == SAVE_KIND_PS5) {
        if (strstr(path, "/savedata_prospero_meta/user/"))
            copy_component_after(path, "/savedata_prospero_meta/user/", title_id, title_size);
        else if (strstr(path, "/savedata_prospero/"))
            copy_component_after(path, "/savedata_prospero/", title_id, title_size);
    }
}

static void track_title(const char *user_id, save_kind kind, const char *title_id) {
    char key[TITLE_KEY_LEN];
    if (!title_id || !title_id[0]) return;

    snprintf(key, sizeof(key), "%s|%s|%s", user_id && user_id[0] ? user_id : "unknown",
             kind_name(kind), title_id);

    for (int i = 0; i < g_title_count; i++) {
        if (strcmp(g_title_keys[i], key) == 0)
            return;
    }

    if (g_title_count < MAX_TRACKED_TITLES) {
        snprintf(g_title_keys[g_title_count], TITLE_KEY_LEN, "%s", key);
        g_title_count++;
    }
}

static bar_file_segment_metadata *find_metadata(bar_session *session, int segment_id) {
    for (uint64_t i = 0; i < session->n_segments; i++) {
        if (session->segment_metadata[i].segment_id == segment_id)
            return &session->segment_metadata[i];
    }
    return NULL;
}

static int mkdir_p(const char *path) {
    char tmp[0x800];
    size_t len;

    if (!path) return -1;
    len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return -1;

    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int ensure_parent_dir(const char *file_path) {
    char parent[0x800];
    char *slash;
    size_t len = strlen(file_path);
    if (len >= sizeof(parent)) return -1;
    memcpy(parent, file_path, len + 1);
    slash = strrchr(parent, '/');
    if (!slash) return 0;
    *slash = '\0';
    return mkdir_p(parent);
}

static int write_all(int fd, const void *buffer, uint64_t size) {
    const unsigned char *p = (const unsigned char *)buffer;
    uint64_t total = 0;

    while (total < size) {
        size_t chunk = (size - total > 0x40000000ULL) ?
                       0x40000000U : (size_t)(size - total);
        ssize_t n = write(fd, p + total, chunk);
        if (n <= 0) return -1;
        total += (uint64_t)n;
    }
    return 0;
}

static int dump_save_file(bar_session *session, void **buffer,
                          const bar_dir_file *entry, int index,
                          save_kind kind) {
    int segment_id = index + 0x2710;
    bar_file_segment_metadata *meta = find_metadata(session, segment_id);
    char out_path[0x800];
    char user_id[32];
    char title_id[64];
    struct stat st;

    parse_user_and_title(entry->path, kind, user_id, sizeof(user_id), title_id, sizeof(title_id));
    track_title(user_id, kind, title_id);

    snprintf(out_path, sizeof(out_path), SAVE_DUMP_DIR "%s", entry->path);

    log_printf("\n[SAVE] %s\n", entry->path);
    log_printf("[SAVE] kind=%s index=%d segment=0x%04x special=%u\n",
               kind_name(kind), index, segment_id, (unsigned)entry->special);

    if (!meta) {
        log_printf("[SAVE] ERROR: segment metadata not found\n");
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t0\tmetadata-not-found\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id, entry->path);
        return -1;
    }

    log_printf("[SAVE] offset=0x%lx compressed=%lu uncompressed=%lu part=%u\n",
               (unsigned long)meta->data_offset,
               (unsigned long)meta->compressed_size,
               (unsigned long)meta->uncompressed_size,
               (unsigned)meta->part_number);

    if (entry->special || meta->part_number != 0) {
        log_printf("[SAVE] SKIP: special/multipart file is not supported yet\n");
        manifest_printf("SKIP\t%s\t%s\t%s\t0x%04x\t%lu\tspecial-or-multipart\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)meta->uncompressed_size, entry->path);
        return 1;
    }

    if (meta->uncompressed_size > MAX_BUFFERED_FILE_SIZE) {
        log_printf("[SAVE] SKIP: file is larger than 2 GiB buffered limit\n");
        manifest_printf("SKIP\t%s\t%s\t%s\t0x%04x\t%lu\tover-2GiB\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)meta->uncompressed_size, entry->path);
        return 1;
    }

    if (ensure_parent_dir(out_path) != 0) {
        log_printf("[SAVE] ERROR: could not create output directories, errno=%d\n", errno);
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t%lu\tmkdir-failed\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)meta->uncompressed_size, entry->path);
        return -1;
    }

    uint64_t decrypted_size = decrypt_segment(session, buffer, segment_id, 0, 0);
    if (decrypted_size == (uint64_t)-1 || !*buffer) {
        log_printf("[SAVE] ERROR: decryption failed\n");
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t%lu\tdecrypt-failed\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)meta->uncompressed_size, entry->path);
        return -1;
    }

    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        log_printf("[SAVE] ERROR: open(%s) failed, errno=%d\n", out_path, errno);
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t%lu\topen-failed\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)decrypted_size, entry->path);
        return -1;
    }

    if (write_all(fd, *buffer, decrypted_size) != 0) {
        log_printf("[SAVE] ERROR: write failed, errno=%d\n", errno);
        close(fd);
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t%lu\twrite-failed\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)decrypted_size, entry->path);
        return -1;
    }

    if (fsync(fd) != 0)
        log_printf("[SAVE] WARNING: fsync failed, errno=%d\n", errno);
    close(fd);

    if (stat(out_path, &st) != 0) {
        log_printf("[SAVE] ERROR: stat after write failed, errno=%d\n", errno);
        manifest_printf("ERROR\t%s\t%s\t%s\t0x%04x\t%lu\tstat-failed\t%s\n",
                        kind_name(kind), user_id, title_id, segment_id,
                        (unsigned long)decrypted_size, entry->path);
        return -1;
    }

    log_printf("[SAVE] OK: %lu bytes -> %s\n",
               (unsigned long)st.st_size, out_path);
    manifest_printf("OK\t%s\t%s\t%s\t0x%04x\t%lu\tok\t%s\n",
                    kind_name(kind), user_id, title_id, segment_id,
                    (unsigned long)st.st_size, entry->path);
    return 0;
}

int main(void) {
    bar_session session;
    void *buffer = NULL;
    bar_dir_file *file_list = NULL;
    int total_files = 0;
    int total_dirs = 0;
    int total_users = 0;
    int matched = 0;
    int ok = 0;
    int skipped = 0;
    int failed = 0;

    memset(&session, 0, sizeof(session));

    FILE *lf = fopen(LOG_FILE, "w");
    if (lf) {
        fprintf(lf, "PS5 BAR Save Extractor log\n");
        fclose(lf);
    }

    log_printf("=== PS5 BAR SAVE EXTRACTOR ===\n");
    log_printf("Input : " MAIN_DIR "archive.dat\n");
    log_printf("Output: " SAVE_DUMP_DIR "\n");
    log_printf("Mode  : all PS4 + PS5 save-related files\n\n");

    if (read_header(&session) != 0) {
        log_printf("FATAL: could not initialize BAR session\n");
        failed++;
        goto cleanup;
    }

    uint64_t info_size = decrypt_segment(&session, &buffer, 1, 0, 0);
    if (info_size == (uint64_t)-1 || !buffer || info_size < 0x18) {
        log_printf("FATAL: could not read backup info segment\n");
        failed++;
        goto cleanup;
    }

    total_users = *(int *)((unsigned char *)buffer + 0x0C);
    total_dirs  = *(int *)((unsigned char *)buffer + 0x10);
    total_files = *(int *)((unsigned char *)buffer + 0x14);

    log_printf("Backup users  : %d\n", total_users);
    log_printf("Backup folders: %d\n", total_dirs);
    log_printf("Backup files  : %d\n\n", total_files);

    if (mkdir_p(SAVE_DUMP_DIR) != 0) {
        log_printf("FATAL: could not create %s, errno=%d\n", SAVE_DUMP_DIR, errno);
        failed++;
        goto cleanup;
    }

    FILE *mf = fopen(MANIFEST_FILE, "w");
    if (mf) {
        fprintf(mf, "status\tplatform\tuser_id\ttitle_id\tsegment\tsize\treason\tsource_path\n");
        fclose(mf);
    } else {
        log_printf("WARNING: could not create manifest.tsv, errno=%d\n", errno);
    }

    uint64_t list_size = decrypt_segment(&session, (void **)&file_list, 3, 0, 0);
    if (list_size == (uint64_t)-1 || !file_list) {
        log_printf("FATAL: could not decrypt file table\n");
        failed++;
        goto cleanup;
    }

    for (int i = 0; i < total_files; i++) {
        save_kind kind = classify_save_path(file_list[i].path);
        if (kind == SAVE_KIND_NONE)
            continue;

        matched++;
        int ret = dump_save_file(&session, &buffer, &file_list[i], i, kind);
        if (ret == 0) ok++;
        else if (ret > 0) skipped++;
        else failed++;
    }

    log_printf("\n=== EXTRACTION SUMMARY ===\n");
    log_printf("Save-related entries matched: %d\n", matched);
    log_printf("Extracted successfully       : %d\n", ok);
    log_printf("Skipped                      : %d\n", skipped);
    log_printf("Failed                       : %d\n", failed);
    log_printf("Unique game save sets        : %d\n", g_title_count);

    if (g_title_count > 0) {
        log_printf("\nDetected titles:\n");
        for (int i = 0; i < g_title_count; i++)
            log_printf("  %s\n", g_title_keys[i]);
    }

    if (failed == 0)
        log_printf("\nRESULT: SUCCESS\n");
    else
        log_printf("\nRESULT: COMPLETED WITH ERRORS - inspect log and manifest.tsv\n");

cleanup:
    close_bar_fd();
    if (file_list) free(file_list);
    if (session.segment_metadata) free(session.segment_metadata);
    if (session.segment_hash) free(session.segment_hash);
    if (buffer) free(buffer);
    return failed ? 1 : 0;
}

int read_header(bar_session* session) {
    const char *file = MAIN_DIR "archive.dat";
    int file_fd = -1;
    bar_file_header *f_header = NULL;
    int ret = -1;

    file_fd = open(file, O_RDONLY);
    if (file_fd == -1) {
        log_printf("File %s not found\n", file);
        goto cleanup;
    }

    f_header = malloc(sizeof(bar_file_header));
    if (!f_header) {
        log_printf("Could not allocate BAR header\n");
        goto cleanup;
    }

    if (read(file_fd, f_header, sizeof(bar_file_header)) != (ssize_t)sizeof(bar_file_header)) {
        log_printf("Could not read complete BAR header\n");
        goto cleanup;
    }

    if (*(uint64_t *)&f_header->magic != 0x464143454953ULL) {
        log_printf("Header magic is wrong in file: %s\n", file);
        goto cleanup;
    }

    log_printf("BAR magic   : %s\n", f_header->magic);
    log_printf("BAR mode    : %d\n", f_header->mode);
    log_printf("BAR version : %d\n", f_header->version);
    log_printf("BAR segments: 0x%lx\n", (unsigned long)f_header->n_segments);
    log_printf("BAR data off: 0x%lx\n", (unsigned long)f_header->data_offset);
    log_printf("BAR data sz : 0x%lx\n\n", (unsigned long)f_header->data_size);
    /* Deliberately do not print BAR key/IV into public logs. */

    session->mode = f_header->mode;
    session->version = f_header->version;
    session->n_segments = f_header->n_segments;
    memcpy(session->key, f_header->key, 16);
    memcpy(session->iv, f_header->iv, 12);

    session->segment_metadata = malloc(sizeof(bar_file_segment_metadata) * session->n_segments);
    session->segment_hash = malloc(sizeof(bar_file_segment_hash) * session->n_segments);
    if (!session->segment_metadata || !session->segment_hash) {
        log_printf("Could not allocate BAR tables\n");
        goto cleanup;
    }

    if (read(file_fd, session->segment_metadata,
             sizeof(bar_file_segment_metadata) * session->n_segments) !=
        (ssize_t)(sizeof(bar_file_segment_metadata) * session->n_segments)) {
        log_printf("Could not read BAR metadata table\n");
        goto cleanup;
    }

    if (read(file_fd, session->segment_hash,
             sizeof(bar_file_segment_hash) * session->n_segments) !=
        (ssize_t)(sizeof(bar_file_segment_hash) * session->n_segments)) {
        log_printf("Could not read BAR hash table\n");
        goto cleanup;
    }

    if (read(file_fd, session->complete_hash, 0x20) < 0) {
        log_printf("Could not read BAR complete hash\n");
        goto cleanup;
    }

    if (bar_context_create(&session->context) != 0) {
        log_printf("Could not create BAR context\n");
        goto cleanup;
    }
    if (bar_context_init(session->context, session->version, session->mode,
                         session->key, session->iv) != 0) {
        log_printf("Could not initialize BAR context\n");
        goto cleanup;
    }
    if (bar_update_aad(session->context, f_header, sizeof(bar_file_header)) != 0 ||
        bar_update_aad(session->context, session->segment_metadata,
                       sizeof(bar_file_segment_metadata) * session->n_segments) != 0 ||
        bar_update_aad(session->context, session->segment_hash,
                       sizeof(bar_file_segment_hash) * session->n_segments) != 0) {
        log_printf("Could not update BAR authentication data\n");
        goto cleanup;
    }
    if (bar_finish_decrypt(session->context, session->complete_hash) != 0) {
        log_printf("BAR header authentication failed\n");
        goto cleanup;
    }

    bar_context_destroy(session->context);
    session->context = 0;
    ret = 0;

cleanup:
    if (file_fd != -1) close(file_fd);
    if (f_header) free(f_header);
    if (ret != 0 && session->context) {
        bar_context_destroy(session->context);
        session->context = 0;
    }
    return ret;
}

uint64_t decrypt_segment(bar_session* session, void** buffer, int segment_id,
                         int special_file, int flag_validate) {
    int meta_index = -1;
    int hash_index = -1;
    bar_file_segment_metadata *metadata = NULL;
    void *encrypted_data = NULL;

    if (special_file) {
        log_printf("Skipping special file\n");
        return (uint64_t)-1;
    }

    for (uint64_t i = 0; i < session->n_segments; i++) {
        if (session->segment_metadata[i].segment_id == segment_id) {
            meta_index = (int)i;
            break;
        }
    }

    if (flag_validate) {
        for (uint64_t i = 0; i < session->n_segments; i++) {
            if (session->segment_hash[i].segment_id == segment_id) {
                hash_index = (int)i;
                break;
            }
        }
    }

    if (meta_index == -1 || (flag_validate && hash_index == -1)) {
        log_printf("Could not find segment with ID: %d\n", segment_id);
        return (uint64_t)-1;
    }

    metadata = &session->segment_metadata[meta_index];
    if (metadata->uncompressed_size > MAX_BUFFERED_FILE_SIZE) {
        log_printf("Skipping segment >2 GiB\n");
        return (uint64_t)-1;
    }

    if (bar_context_create(&session->context) != 0 ||
        bar_context_init(session->context, session->version, session->mode,
                         session->key, metadata->iv) != 0) {
        log_printf("Could not initialize segment BAR context\n");
        if (session->context) {
            bar_context_destroy(session->context);
            session->context = 0;
        }
        return (uint64_t)-1;
    }

    encrypted_data = malloc(metadata->compressed_size);
    if (!encrypted_data) {
        log_printf("Could not allocate encrypted segment buffer\n");
        bar_context_destroy(session->context);
        session->context = 0;
        return (uint64_t)-1;
    }

    if (*buffer) {
        free(*buffer);
        *buffer = NULL;
    }
    *buffer = malloc(metadata->uncompressed_size);
    if (!*buffer) {
        log_printf("Could not allocate decrypted segment buffer\n");
        free(encrypted_data);
        bar_context_destroy(session->context);
        session->context = 0;
        return (uint64_t)-1;
    }

    if (read_from_archive(encrypted_data, metadata->compressed_size, metadata->data_offset) < 0) {
        log_printf("Could not read encrypted segment data\n");
        free(encrypted_data);
        bar_context_destroy(session->context);
        session->context = 0;
        return (uint64_t)-1;
    }

    if (bar_update_decrypt(session->context, *buffer, encrypted_data,
                           (int)metadata->uncompressed_size) != 0) {
        log_printf("BAR decrypt ioctl failed\n");
        free(encrypted_data);
        bar_context_destroy(session->context);
        session->context = 0;
        return (uint64_t)-1;
    }

    bar_context_destroy(session->context);
    session->context = 0;
    free(encrypted_data);
    return metadata->uncompressed_size;
}

int read_from_archive(void* buffer, uint64_t size, uint64_t offset) {
    uint64_t archive_number = offset / 0xFFFF0000ULL;
    uint64_t offset_tmp = offset - archive_number * 0xFFFF0000ULL;

    if (size + (offset % 0xFFFF0000ULL) < 0xFFFF0001ULL) {
        return read_from_archive_number(archive_number, buffer, size, offset_tmp);
    }

    uint64_t first_size = 0xFFFF0000ULL - (offset % 0xFFFF0000ULL);
    int ret = read_from_archive_number(archive_number, buffer, first_size, offset_tmp);
    if (ret < 0) {
        log_printf("Could not read split archive part %lu\n", (unsigned long)archive_number);
        return -1;
    }

    return read_from_archive_number(archive_number + 1,
                                    (unsigned char *)buffer + first_size,
                                    size - first_size, 0);
}

int read_from_archive_number(uint64_t archive_number, void* buffer,
                             uint64_t size, uint64_t offset) {
    int file_fd;
    char file[0x100];
    ssize_t ret;

    if (archive_number == 0)
        snprintf(file, sizeof(file), MAIN_DIR "archive.dat");
    else
        snprintf(file, sizeof(file), MAIN_DIR "archive%04lu.dat", (unsigned long)archive_number);

    file_fd = open(file, O_RDONLY);
    if (file_fd == -1) {
        log_printf("File %s not found\n", file);
        return -1;
    }

    ret = pread(file_fd, buffer, (size_t)size, (off_t)offset);
    if (ret < 0)
        log_printf("Could not read %s size=%lu offset=%lu\n", file,
                   (unsigned long)size, (unsigned long)offset);
    close(file_fd);
    return (int)ret;
}

void log_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file) {
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fclose(log_file);
    }
}

void manifest_printf(const char *format, ...) {
    FILE *manifest = fopen(MANIFEST_FILE, "a");
    if (!manifest) return;

    va_list args;
    va_start(args, format);
    vfprintf(manifest, format, args);
    va_end(args);
    fclose(manifest);
}
