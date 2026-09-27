#include "crequests/request.h"
#include "crequests/fields.h"
#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

CReqRequest *creq_request_create(CReqMethod method, const char *url) {
    if (!url) {
        log_error("creq_request_create: url nula");
        return NULL;
    }

    CReqRequest *request = (CReqRequest *)calloc(1, sizeof(CReqRequest));
    if (!request) {
        log_error("creq_request_create: falha de memória");
        return NULL;
    }

    size_t url_len = strlen(url) + 1;
    request->url = (char *)malloc(url_len);
    if (!request->url) {
        log_error("creq_request_create: falha de memória para a url");
        free(request);
        return NULL;
    }
    memcpy(request->url, url, url_len);

    request->method = method;
    /* headers/cookies/query_params começam NULL: criados sob demanda
     * na primeira chamada de creq_header_set/creq_cookie_set/
     * creq_query_set (ver fields.c) -- evita alocar 3 objetos cJSON
     * vazios para requisições simples que não os usam. */
    return request;
}

void creq_request_free(CReqRequest *request) {
    if (!request) {
        return;
    }
    free(request->url);
    cJSON_Delete(request->headers);
    cJSON_Delete(request->cookies);
    cJSON_Delete(request->query_params);
    if (request->owns_body) {
        free(request->body);
    }
    free(request);
}

bool creq_request_set_body(CReqRequest *request, const void *data, size_t length, bool copy) {
    if (!request) {
        return false;
    }

    /* Libera um corpo anterior que a requisição já possuísse, antes de
     * substituir (evita vazamento ao chamar set_body mais de uma vez). */
    if (request->owns_body) {
        free(request->body);
    }

    if (!data || length == 0) {
        request->body = NULL;
        request->body_length = 0;
        request->owns_body = false;
        return true;
    }

    if (copy) {
        unsigned char *dup = (unsigned char *)malloc(length);
        if (!dup) {
            log_error("creq_request_set_body: falha de memória copiando %zu bytes", length);
            request->body = NULL;
            request->body_length = 0;
            request->owns_body = false;
            return false;
        }
        memcpy(dup, data, length);
        request->body = dup;
        request->owns_body = true;
    } else {
        request->body = (unsigned char *)data;
        request->owns_body = true; /* a requisição passa a ser dona do ponteiro recebido */
    }

    request->body_length = length;
    return true;
}

bool creq_request_set_json_body(CReqRequest *request, cJSON *json) {
    if (!request || !json) {
        return false;
    }

    char *serialized = cJSON_PrintUnformatted(json);
    if (!serialized) {
        log_error("creq_request_set_json_body: falha ao serializar JSON");
        return false; /* 'json' NÃO é consumido em caso de falha, como documentado em request.h */
    }

    size_t length = strlen(serialized);

    /* set_body com copy=false: a requisição passa a ser dona de
     * 'serialized' (alocado por cJSON_PrintUnformatted com malloc, e
     * liberado por creq_request_free()/próxima chamada a set_body). */
    if (!creq_request_set_body(request, serialized, length, false)) {
        free(serialized);
        cJSON_Delete(json);
        return false;
    }

    /* content_length é size_t (sem sinal); "%zu" é o especificador
     * correto e portável (C99) para imprimi-lo. */
    char content_length_str[32];
    snprintf(content_length_str, sizeof(content_length_str), "%zu", length);

    if (!creq_header_set(request, "Content-Type", "application/json") ||
        !creq_header_set(request, "Content-Length", content_length_str)) {
        cJSON_Delete(json);
        return false; /* corpo já setado, mas cabeçalhos falharam: ainda assim reportamos erro */
    }

    cJSON_Delete(json); /* a lib assume posse de 'json', como documentado em request.h */
    return true;
}
