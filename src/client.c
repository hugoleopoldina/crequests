#include "crequests/client.h"
#include "crequests/url.h"
#include "crequests/fields.h"
#include "crequests/parser.h"
#include "crequests/socket.h"
#include "crequests/tls.h"
#include "request_builder.h"
#include "internal.h"

#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>

/* Tamanho de cada leitura da rede. Não tem relação com CREQ_MAX_LINE_LENGTH
 * (parser.c) nem com o tamanho de nenhum chunk HTTP -- é só o quanto
 * pedimos por vez ao socket/TLS; o parser lida com qualquer
 * fragmentação, então este valor só afeta o número de idas e vindas
 * ao kernel, não a correção. */
#define CREQ_READ_CHUNK_SIZE 8192

bool creq_global_init(void) {
    if (!creq_socket_platform_init()) {
        return false;
    }
    if (!creq_tls_context_create()) {
        creq_socket_platform_cleanup();
        return false;
    }

    log_debug("crequests inicializado (OpenSSL compilado: %s / em execução: %s)",
              OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));
    return true;
}

void creq_global_cleanup(void) {
    creq_tls_context_free();
    creq_socket_platform_cleanup();
}

/* Copia todos os cookies recebidos em 'response' para o cookie jar da
 * sessão, para que fiquem disponíveis em requisições futuras feitas
 * com a mesma CReqSession (ver session.h). Cookies com o mesmo nome de
 * um já presente na sessão substituem o valor antigo. */
static bool absorb_cookies_into_session(CReqSession *session, const CReqResponse *response) {
    if (!session || !response->cookies) {
        return true;
    }
    for (cJSON *c = response->cookies->child; c; c = c->next) {
        if (!cJSON_IsString(c)) {
            continue;
        }
        if (!creq_dict_set(&session->cookie_jar, c->string, c->valuestring, false)) {
            return false;
        }
    }
    return true;
}

CReqResponse *creq_perform(CReqSession *session, CReqRequest *request) {
    if (!request) {
        log_error("creq_perform: requisição nula");
        return NULL;
    }

    CReqUrl *url = NULL;
    unsigned char *raw_request = NULL;
    creq_socket_t sockfd = CREQ_INVALID_SOCKET;
    SSL *ssl = NULL;
    CReqResponse *response = NULL;
    CReqParser parser;
    bool parser_initialized = false;
    CReqResponse *result = NULL;

    url = creq_url_parse(request->url);
    if (!url) {
        goto cleanup; /* erro já registrado por creq_url_parse */
    }

    log_debug("%s %s://%s:%s%s", creq_method_to_string(request->method),
              url->protocol, url->host, url->port, url->path);

    size_t raw_length = 0;
    raw_request = creq_build_raw_request(request, url, session, &raw_length);
    if (!raw_request) {
        goto cleanup; /* erro já registrado por creq_build_raw_request */
    }

    sockfd = creq_socket_connect(url->host, url->port);
    if (sockfd == CREQ_INVALID_SOCKET) {
        goto cleanup; /* erro já registrado por creq_socket_connect */
    }

    if (url->is_tls) {
        ssl = creq_tls_connect(sockfd, url->host);
        if (!ssl) {
            goto cleanup; /* erro já registrado por creq_tls_connect */
        }
    }

    bool sent = url->is_tls
        ? (creq_tls_send(ssl, raw_request, raw_length) >= 0)
        : creq_socket_send_all(sockfd, raw_request, raw_length);

    if (!sent) {
        log_error("Falha ao enviar a requisição para %s", url->host);
        goto cleanup;
    }

    response = creq_response_create();
    if (!response) {
        log_error("creq_perform: falha de memória criando a resposta");
        goto cleanup;
    }

    creq_parser_init(&parser, response, request->method == CREQ_METHOD_HEAD);
    parser_initialized = true;

    unsigned char chunk[CREQ_READ_CHUNK_SIZE];
    CReqParseStatus status = CREQ_PARSE_NEED_MORE;

    while (status == CREQ_PARSE_NEED_MORE) {
        int read = url->is_tls
            ? creq_tls_read(ssl, chunk, sizeof(chunk))
            : creq_socket_recv(sockfd, chunk, sizeof(chunk));

        if (read > 0) {
            status = creq_parser_feed(&parser, chunk, (size_t)read);
        } else if (read == 0) {
            /* Conexão fechada normalmente pelo peer -- só é uma
             * resposta válida se o parser esperava exatamente isso
             * (corpo do tipo CREQ_BODY_UNTIL_CLOSE); creq_parser_finish
             * decide e registra um erro caso contrário. */
            status = creq_parser_finish(&parser);
            break;
        } else {
            log_error("Falha de leitura da rede ao aguardar resposta de %s", url->host);
            status = CREQ_PARSE_ERROR;
            break;
        }
    }

    if (status != CREQ_PARSE_COMPLETE) {
        log_error("Resposta HTTP de %s não pôde ser processada (status=%d)", url->host, (int)status);
        goto cleanup;
    }

    if (!absorb_cookies_into_session(session, response)) {
        log_error("Falha de memória atualizando os cookies da sessão");
        goto cleanup;
    }

    log_debug("%d %s -- corpo: %zu bytes", response->status_code, response->reason_phrase, response->body_length);

    result = response;
    response = NULL; /* posse transferida para 'result': não libere no cleanup */

cleanup:
    if (parser_initialized) {
        creq_parser_destroy(&parser);
    }
    creq_response_free(response); /* NULL (sucesso) ou a resposta parcial de uma falha */
    if (ssl) {
        SSL_free(ssl);
    }
    if (sockfd != CREQ_INVALID_SOCKET) {
        creq_socket_close(sockfd);
    }
    free(raw_request);
    creq_url_free(url);

    return result;
}

/* --------------------------------------------------------------------
 * Atalhos por método: montam uma CReqRequest simples, delegam para
 * creq_perform() e já liberam a requisição temporária.
 * ------------------------------------------------------------------ */

static CReqResponse *perform_simple(CReqSession *session, CReqMethod method, const char *url) {
    CReqRequest *request = creq_request_create(method, url);
    if (!request) {
        return NULL;
    }
    CReqResponse *response = creq_perform(session, request);
    creq_request_free(request);
    return response;
}

static CReqResponse *perform_with_body(CReqSession *session, CReqMethod method, const char *url,
                                       const void *body, size_t body_length, const char *content_type) {
    CReqRequest *request = creq_request_create(method, url);
    if (!request) {
        return NULL;
    }

    bool ok = true;
    if (body && body_length > 0) {
        ok = creq_request_set_body(request, body, body_length, true);
    }
    if (ok && content_type) {
        ok = creq_header_set(request, "Content-Type", content_type);
    }

    CReqResponse *response = ok ? creq_perform(session, request) : NULL;
    creq_request_free(request);
    return response;
}

CReqResponse *creq_get(CReqSession *session, const char *url) {
    return perform_simple(session, CREQ_METHOD_GET, url);
}

CReqResponse *creq_head(CReqSession *session, const char *url) {
    return perform_simple(session, CREQ_METHOD_HEAD, url);
}

CReqResponse *creq_delete(CReqSession *session, const char *url) {
    return perform_simple(session, CREQ_METHOD_DELETE, url);
}

CReqResponse *creq_post(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type) {
    return perform_with_body(session, CREQ_METHOD_POST, url, body, body_length, content_type);
}

CReqResponse *creq_put(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type) {
    return perform_with_body(session, CREQ_METHOD_PUT, url, body, body_length, content_type);
}

CReqResponse *creq_patch(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type) {
    return perform_with_body(session, CREQ_METHOD_PATCH, url, body, body_length, content_type);
}

CReqResponse *creq_post_json(CReqSession *session, const char *url, cJSON *json) {
    CReqRequest *request = creq_request_create(CREQ_METHOD_POST, url);
    if (!request) {
        cJSON_Delete(json);
        return NULL;
    }

    if (!creq_request_set_json_body(request, json)) {
        creq_request_free(request);
        return NULL;
    }

    CReqResponse *response = creq_perform(session, request);
    creq_request_free(request);
    return response;
}
