#ifndef _CURL_H
#define _CURL_H
#include <json-c/json.h>
ssize_t http_get(const char *url, char *resp, size_t size);
int json_get(const char *url, char *resp, size_t size, const char *attr);
int json_get_double(const char *url, double *res, const char *attr);
struct json_object *http_get_json(const char *url);
#endif
