#include "crequests/response.h"
#include "internal.h"

#include <stdlib.h>

CReqResponse *creq_response_create(void) {
    return (CReqResponse *)calloc(1, sizeof(CReqResponse));
}

void creq_response_free(CReqResponse *response) {
    if (!response) {
        return;
    }
    cJSON_Delete(response->headers);
    cJSON_Delete(response->cookies);
    free(response->body);
    free(response);
}

const char *creq_response_header_get(const CReqResponse *response, const char *name) {
    if (!response) {
        return NULL;
    }
    cJSON *item = creq_dict_find(response->headers, name, true);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}

const char *creq_response_cookie_get(const CReqResponse *response, const char *name) {
    if (!response) {
        return NULL;
    }
    cJSON *item = creq_dict_find(response->cookies, name, false);
    return (item && cJSON_IsString(item)) ? item->valuestring : NULL;
}
