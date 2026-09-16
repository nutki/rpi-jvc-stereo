#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include "curl.h"

struct buffer {
    char   *data;
    size_t  size, len;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    struct buffer *buf = userdata;
    size_t n = size * nmemb, left = buf->size - 1 - buf->len;
    memcpy(buf->data + buf->len, ptr, n > left ? left : n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return n;
}

ssize_t http_get(const char *url, char *resp, size_t size) {
    CURL *curl;
    CURLcode res;
    struct buffer buf = { resp, size };
    curl = curl_easy_init();
    if (!curl) return -1;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) return -1;
    return buf.len;
}

struct json_object *http_get_json(const char *url) {
    char tmp[1024 * 64];
    ssize_t len = http_get(url, tmp, sizeof tmp);
    if (len < 0) return 0;
    if (len > sizeof tmp - 1) {
        printf("Output truncated: %ld > %ld\n", len, sizeof tmp - 1);
    }
    struct json_object *root = json_tokener_parse(tmp);
    if (!root) return 0;
    return root;
}
