#include "crequests/fields.h"
#include "internal.h"

/* --- Headers (case-insensitive) ------------------------------------ */

bool creq_header_set(CReqRequest *request, const char *name, const char *value) {
    if (!request || !name || !value) {
        return false;
    }
    return creq_dict_set(&request->headers, name, value, true);
}

const char *creq_header_get(const CReqRequest *request, const char *name) {
    if (!request) {
        return NULL;
    }
    cJSON *item = creq_dict_find(request->headers, name, true);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

void creq_header_remove(CReqRequest *request, const char *name) {
    if (!request) {
        return;
    }
    creq_dict_remove(request->headers, name, true);
}

/* --- Cookies (case-sensitive) --------------------------------------- */

bool creq_cookie_set(CReqRequest *request, const char *name, const char *value) {
    if (!request || !name || !value) {
        return false;
    }
    return creq_dict_set(&request->cookies, name, value, false);
}

const char *creq_cookie_get(const CReqRequest *request, const char *name) {
    if (!request) {
        return NULL;
    }
    cJSON *item = creq_dict_find(request->cookies, name, false);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

void creq_cookie_remove(CReqRequest *request, const char *name) {
    if (!request) {
        return;
    }
    creq_dict_remove(request->cookies, name, false);
}

/* --- Parâmetros de query (case-sensitive) --------------------------- */

bool creq_query_set(CReqRequest *request, const char *name, const char *value) {
    if (!request || !name || !value) {
        return false;
    }
    return creq_dict_set(&request->query_params, name, value, false);
}

const char *creq_query_get(const CReqRequest *request, const char *name) {
    if (!request) {
        return NULL;
    }
    cJSON *item = creq_dict_find(request->query_params, name, false);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

void creq_query_remove(CReqRequest *request, const char *name) {
    if (!request) {
        return;
    }
    creq_dict_remove(request->query_params, name, false);
}
