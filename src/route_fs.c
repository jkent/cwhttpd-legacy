/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
Route handlers to let httpd use the filesystem to serve the files in it.
*/

#include "log.h"
#include "cwhttpd/route.h"
#include "cwhttpd/httpd.h"

#include <assert.h>
#include <frozen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <sys/errno.h>


#define TRY(X) ({ \
    ssize_t n = X; \
    if (n < 0) { \
        r = CWHTTPD_STATUS_FAIL; \
        goto cleanup; \
    } \
    n; \
})

#define FILE_CHUNK_LEN    (1024)
#define MAX_FILENAME_LENGTH (256)

#define ESPFS_FLAG_GZIP (1 << 1)


static bool get_filepath(cwhttpd_conn_t *conn, char *path, size_t len,
        struct stat *st, const char *index)
{
    size_t out_len = 0;
    const char *url = conn->request.url;
    const cwhttpd_route_t *route = conn->route;
    const char *rpath = route->path;

    while (*rpath == *url) {
        rpath++;
        url++;
    }

    if (route->argc < 1) {
        if (len > 0) {
            strncpy(path, url, len - 1);
            path[len - 1] = '\0';
            out_len = strlen(path);
        }
        if (path[out_len - 1] == '/' && len - out_len > 0) {
            strncpy(path + out_len, index, len - out_len - 1);
            path[len - out_len - 1] = '\0';
            out_len = strlen(path);
        }
    } else if (len > 0) {
        strncpy(path, route->argv[0], len);
        path[len - 1] = '\0';
        out_len = strlen(path);
        if (path[out_len - 1] == '/' && len - out_len > 0) {
            strncpy(path + out_len, url, len - out_len - 1);
            path[len - out_len - 1] = '\0';
            out_len = strlen(path);
        }
        if (path[out_len - 1] == '/' && len - out_len > 0) {
            strncpy(path + out_len, index, len - out_len - 1);
            path[len - out_len - 1] = '\0';
            out_len = strlen(path);
        }
    }

    if (stat(path, st) != 0) {
        return false;
    }

    if (S_ISDIR(st->st_mode)) {
        if (len - out_len - 1 > 0) {
            strncpy(path + out_len, "/", len - out_len - 1);
            path[len - out_len - 1] = '\0';
            out_len = strlen(path);
        }
        if (len - out_len - 1 > 0) {
            strncpy(path + out_len, index, len - out_len);
            path[len - out_len - 1] = '\0';
            out_len = strlen(path);
        }
        if (stat(path, st) != 0) {
            return false;
        }
    }

    if (S_ISREG(st->st_mode)) {
        return true;
    }

    return false;
}

cwhttpd_status_t cwhttpd_route_fs_get(cwhttpd_conn_t *conn)
{
    cwhttpd_status_t r = CWHTTPD_STATUS_DONE;

    /* Only process GET requests, otherwise fallthrough */
    if (conn->request.method != CWHTTPD_METHOD_GET) {
        return CWHTTPD_STATUS_NOTFOUND;
    }

    /* We can use buf here because its not needed until reading data */
    char buf[FILE_CHUNK_LEN];
    struct stat st;
    if (!get_filepath(conn, buf, sizeof(buf), &st, "index.html")) {
        printf("not found\n");
        return CWHTTPD_STATUS_NOTFOUND;
    }

    bool gzip_encoding = false;
#if defined(ESP_PLATFORM)
    /* Legacy ESPFS */
    if (st.st_spare4[0] == 0x73665345) {
        if (st.st_spare4[0] & 2) {
            gzip_encoding = true;
        }
    }

    /* ESPFS v2 or FrogFS*/
    if (st.st_spare4[0] == 0x2B534645 || st.st_spare4[0] == 0x676f7246) {
        if (st.st_spare4[0] & 1) {
            gzip_encoding = true;
        }
    }
#endif

    const char *mimetype = cwhttpd_get_mimetype(buf);

    FILE *f = fopen(buf, "r");
#if 0 // Deprecated
    if (f == NULL) {
        /* file not found, look for filename.gz */
        size_t out_len = strlen(buf);
        if (sizeof(buf) - out_len > 0) {
            strncpy(buf + out_len, ".gz", sizeof(buf) - out_len - 1);
            buf[sizeof(buf) - out_len - 1] = "\0";
        }
        f = fopen(buf, "r");
        gzip_encoding = true;
    }
#endif

    if (f == NULL) {
        return CWHTTPD_STATUS_NOTFOUND;
    }

    if (gzip_encoding) {
        /* Check the request Accept-Encoding header for gzip. Return a 500
         * response if not present. */
        const char *header = cwhttpd_get_header(conn, "Accept-Encoding");
        if (header && strstr(header, "gzip") == NULL) {
            LOGE(__func__, "client does not accept gzip!");
            fclose(f);
            TRY(cwhttpd_response(conn, 500));
            return CWHTTPD_STATUS_DONE;
        }
    }

    TRY(cwhttpd_response(conn, 200));
    if (gzip_encoding) {
        TRY(cwhttpd_send_header(conn, "Content-Encoding", "gzip"));
    }
    if (mimetype) {
        TRY(cwhttpd_send_header(conn, "Content-Type", mimetype));
    }
    TRY(cwhttpd_send_cache_header(conn, mimetype));

    size_t len;
    TRY(cwhttpd_chunk_start(conn, st.st_size));
    while ((len = fread(buf, 1, FILE_CHUNK_LEN, f)) > 0) {
        TRY(cwhttpd_send(conn, buf, len));
    }
    TRY(cwhttpd_chunk_end(conn));

cleanup:
    fclose(f);
    return r;
}

cwhttpd_status_t cwhttpd_route_fs_tpl(cwhttpd_conn_t *conn)
{
    cwhttpd_status_t r = CWHTTPD_STATUS_DONE;

    /* Only process GET requests, otherwise fallthrough */
    if (conn->request.method != CWHTTPD_METHOD_GET) {
        return CWHTTPD_STATUS_NOTFOUND;
    }

    /* We can use buf here because its not needed until reading data */
    char buf[FILE_CHUNK_LEN];
    struct stat st;
    if (!get_filepath(conn, buf, sizeof(buf), &st, "index.tpl")) {
        return CWHTTPD_STATUS_NOTFOUND;
    }

#if defined(CONFIG_IDF_TARGET_ESP8266) || defined(ESP_PLATFORM)
    bool gzip_encoding = false;

    /* Legacy ESPFS */
    if (st.st_spare4[0] == 0x73665345) {
        if (st.st_spare4[0] & 2) {
            gzip_encoding = true;
        }
    }

    /* ESPFS v2 */
    if (st.st_spare4[0] == 0x2B534645) {
        if (st.st_spare4[0] & 1) {
            gzip_encoding = true;
        }
    }

    if (gzip_encoding) {
        LOGE(__func__, "template has gzip encoding");
        return CWHTTPD_STATUS_NOTFOUND;
    }
#endif

    const char *mimetype = cwhttpd_get_mimetype(buf);

    FILE *f = fopen(buf, "r");
    if (f == NULL) {
        return CWHTTPD_STATUS_NOTFOUND;
    }

    cwhttpd_tpl_cb_t cb = conn->route->argv[1];
    TRY(cwhttpd_response(conn, 200));
    if (mimetype) {
        TRY(cwhttpd_send_header(conn, "Content-Type", mimetype));
    }

    void *user = NULL;
    size_t len;
    int token_pos = -1;
    char token[32];
    do {
        len = fread(buf, 1, FILE_CHUNK_LEN, f);
        int raw_count = 0;
        uint8_t *p = (uint8_t *) buf;
        if (len > 0) {
            for (size_t i = 0; i < len; i++) {
                if (token_pos < 0) {
                    /* we're in ordinary text */
                    if (buf[i] == '%') {
                        /* send collected raw data */
                        if (raw_count != 0) {
                            TRY(cwhttpd_send(conn, p, raw_count));
                            raw_count = 0;
                        }
                        /* start collecting token chars */
                        token_pos = 0;
                    } else {
                        raw_count++;
                    }
                } else {
                    /* we're in token text */
                    if (buf[i] == '%') {
                        if (token_pos == 0) {
                            /* this is an escape sequence */
                            TRY(cwhttpd_send(conn, "%", 1));
                        } else {
                            /* this is a token */
                            token[token_pos] = '\0'; /* zero terminate */
                            cb(conn, token, &user);
                        }

                        /* collect normal characters again */
                        p = (uint8_t *) &buf[i + 1];
                        token_pos = -1;
                    } else {
                        if (token_pos < (sizeof(token) - 1)) {
                            token[token_pos++] = buf[i];
                        }
                    }
                }
            }
        }

        /* send remainder */
        if (raw_count != 0) {
            TRY(cwhttpd_send(conn, p, raw_count));
        }
    } while (len == FILE_CHUNK_LEN);

cleanup:
    /* we're done */
    cb(conn, NULL, &user);
    fclose(f);
    return r;
}

#if 0
static int create_missing_directories(const char *fullpath)
{
    /* make a copy for modifications */
    char *path = strndup(fullpath, MAX_FILENAME_LENGTH);
    assert(path != NULL);

    for (int i = 0; path[i] != '\0'; i++) {
        if (i > 0 && path[i] == '/') {
            struct stat filestat;
            path[i] = '\0';  /* truncate path */
            int result = stat(path, &filestat);
            if (result == 0 && S_ISDIR(filestat.st_mode)) {
                path[i] = '/';
                continue;
            } else if (result != 0 && errno == ENOENT) {
                if (mkdir(path, S_IRWXU) != 0) {
                    LOGE(__func__, "mkdir failed");
                    path[i] = '/';
                    return 1;
                }
           }
           path[i] = '/'; /* restore path */
       }
    }
    free(path);
    return 0;
}

typedef struct {
    enum {UPSTATE_START, UPSTATE_WRITE, UPSTATE_DONE} state;
    FILE *file;
    char filepath[MAX_FILENAME_LENGTH];
    char *filename;
    size_t filepath_len;
    int b_written;
    const char *errtxt;
} upload_data_t;

cwhttpd_status_t cwhttpd_route_fs_put(cwhttpd_conn_t *conn)
{
    upload_data_t *data = (upload_data_t *) conn->user;

    if (conn->closed) {
        if (data != NULL) {
            if(data->file != NULL) {
                fclose(data->file);
                LOGD(__func__, "fclose: %s, r", data->filename);
            }
            free(data);
        }
        LOGE(__func__, "Connection aborted!");
        return CWHTTPD_STATUS_DONE;
    }

    if (data == NULL) {
        /* First call to this route handler */
        if (conn->post == NULL || (conn->method != CWHTTPD_METHOD_PUT &&
                conn->method != CWHTTPD_METHOD_POST)) {
            return CWHTTPD_STATUS_NOTFOUND; /* fallthrough */
        }
        data = (upload_data_t *) malloc(sizeof(upload_data_t));
        if (data == NULL) {
            LOGE(__func__, "malloc fail");
            goto err;
        }
        conn->user = data;
        memset(data, 0, sizeof(upload_data_t));

        if (conn->post->boundary != NULL) {
            /* TODO: handle multipart/form-data POST */
            /* upload using xhr.send(file), not xhr.send(form) */
            LOGE(__func__, "multipart/form-data is not supported");
            goto err;
        }

        const char *basepath = conn->route->argv[0];
        if (basepath == NULL || *basepath == 0) {
            goto err;
        }

        strncpy(data->filepath, basepath, sizeof(data->filepath) - 1);
        buf[sizeof(data->filepath) - 1] = '\0';
        data->filepath_len = strlen(data->filepath);
        if (data->filepath[data->filepath_len - 1] == '/') {
            data->filename = data->filepath + data->filepath_len;
        } else {
            data->filename = data->filepath;
        }

        if (*data->filename == '\0' && conn->post != NULL) {
            size_t len = sizeof(data->filepath) - data->filepath_len;
            cwhttpd_find_param("filename", conn->post->buf, data->filename,
                    &len);
        }

        if (*data->filename == '\0') {
            size_t len = sizeof(data->filepath) - data->filepath_len;
            cwhttpd_find_param("filename", conn->args, data->filename,
                    &len);
        }

        if (*data->filename == '\0') {
            const char *url = conn->url;
            const char *route_url = conn->route->path;

            while (*url && *route_url++ == *url++);

            if (sizeof(data->filepath) - data->filepath_len > 0) {
                strncpy(data->filename, url,
                        sizeof(data->filepath) - data->filepath_len);
                data->filename[sizeof(data->filepath) - \
                        data->filepath_len - 1] = '\0';
            }
        }

        LOGI(__func__, "Uploading: %s", data->filepath);

        if (create_missing_directories(data->filepath) != 0) {
            data->errtxt = "Error creating directories!";
            goto err;
        }

        /* Open file */
        data->file = fopen(data->filepath, "w");
        if (data->file == NULL) {
            data->errtxt = "Can't open file for writing!";
            LOGE(__func__, "%s", data->errtxt);
            goto err;
        }

        data->state = UPSTATE_WRITE;
    }

    LOGD(__func__, "Chunk: %d bytes, ", conn->post->buf_len);

    if (data->state == UPSTATE_WRITE) {
        if (data->file != NULL){
            int count = fwrite(conn->post->buf, 1, conn->post->buf_len,
                    data->file);
            data->b_written += count;
            if (count != conn->post->buf_len) {
                LOGE(__func__, "write error");
            }
            if (data->b_written >= conn->post->len) {
                data->state = UPSTATE_DONE;
            }
        }
        /* eat any extra bytes */
    } else if (data->state == UPSTATE_DONE) {
        LOGE(__func__, "extra data received");
        /* ignore those bytes. */
    }

err:
    if (conn->post->received != conn->post->len) {
        return CWHTTPD_STATUS_MORE;
    }

    /* we're done */
    if(data->file != NULL){
        fclose(data->file);
    }
    LOGI(__func__, "Total: %d bytes written.", data->b_written);

    char *json = json_asprintf("{filename:%Q,received:%d,written:%d,"
            "success:%B}", data->filename, conn->post->received,
            data->b_written, data->state == UPSTATE_DONE);
    free(data);

    cwhttpd_response(conn, 200);
    cwhttpd_send_header(conn, "Cache-Control",
            "no-store, must-revalidate, no-cache, max-age=0");
    cwhttpd_send_header(conn, "Content-Type", "application/json; charset=utf-8");
    cwhttpd_enqueue(conn, json, -1);
    free(json);

    return CWHTTPD_STATUS_DONE;
}
#endif