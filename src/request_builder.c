/*
 * Monta os bytes crus de uma requisição HTTP/1.1 a partir de uma
 * CReqRequest, a CReqUrl já analisada e (opcionalmente) uma
 * CReqSession. É aqui que headers, cookies e parâmetros de query são
 * de fato "traduzidos" para o formato que o servidor espera receber
 * (requisito de correção do req #8 do projeto).
 */
#include "request_builder.h"
#include "internal.h"
#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Copia todas as entradas de 'source' (objeto cJSON string:string)
 * para dentro de '*dest_ptr', criando o objeto de destino sob demanda.
 * Usado para mesclar os headers/cookies padrão da sessão com os da
 * requisição, sem exigir cJSON_Duplicate (mantém a dependência de
 * cJSON no mínimo necessário). Uma chave repetida em 'source' apenas
 * sobrescreve o valor já presente em '*dest_ptr' -- por isso a ordem
 * das chamadas importa: mesclar a sessão primeiro e a requisição
 * depois faz a requisição "vencer" em caso de conflito. */
static bool merge_dict_into(cJSON **dest_ptr, const cJSON *source, bool case_insensitive) {
    if (!source) {
        return true;
    }
    for (cJSON *item = source->child; item; item = item->next) {
        if (!cJSON_IsString(item)) {
            continue;
        }
        if (!creq_dict_set(dest_ptr, item->string, item->valuestring, case_insensitive)) {
            return false;
        }
    }
    return true;
}

/* Constrói "path[?|&]k1=v1&k2=v2..." a partir do path já presente na
 * URL (que pode já conter sua própria query string) e dos parâmetros
 * extras definidos via creq_query_set(). Chaves e valores são
 * percent-encoded (RFC 3986) para que caracteres como espaço, '&' ou
 * '=' dentro de um valor não quebrem a query string resultante.
 * Retorna uma string malloc'd (sempre terminada em nulo), ou NULL em
 * caso de falha de memória. */
static char *build_path_with_query(const char *base_path, const cJSON *query_params) {
    if (!query_params || !query_params->child) {
        return creq_strdup(base_path);
    }

    bool has_existing_query = strchr(base_path, '?') != NULL;
    CReqBuf buf = {0};
    bool ok = creq_buf_append_str(&buf, base_path);

    bool first_param = true;
    for (cJSON *p = query_params->child; ok && p; p = p->next) {
        if (!cJSON_IsString(p)) {
            continue;
        }

        char *enc_key = creq_percent_encode(p->string);
        char *enc_val = creq_percent_encode(p->valuestring);
        if (!enc_key || !enc_val) {
            free(enc_key);
            free(enc_val);
            ok = false;
            break;
        }

        const char *sep = first_param ? (has_existing_query ? "&" : "?") : "&";
        first_param = false;

        ok = creq_buf_append_str(&buf, sep)
          && creq_buf_append_str(&buf, enc_key)
          && creq_buf_append_str(&buf, "=")
          && creq_buf_append_str(&buf, enc_val);

        free(enc_key);
        free(enc_val);
    }

    if (ok) {
        ok = creq_buf_append(&buf, "", 1); /* terminador nulo */
    }

    if (!ok) {
        creq_buf_free(&buf);
        return NULL;
    }

    return (char *)buf.data;
}

/* Serializa os cookies de 'cookies' (objeto cJSON string:string) no
 * formato de um único header Cookie: "nome1=valor1; nome2=valor2".
 * Retorna NULL (sem erro) se 'cookies' for NULL/vazio -- nesse caso o
 * chamador simplesmente não adiciona o header. Retorna uma string
 * malloc'd em caso de sucesso, ou definido como erro por retornar NULL
 * com 'out_ok' setado para false (necessário para distinguir "sem
 * cookies" de "falha de memória"). */
static char *build_cookie_header(const cJSON *cookies, bool *out_ok) {
    *out_ok = true;
    if (!cookies || !cookies->child) {
        return NULL;
    }

    CReqBuf buf = {0};
    bool ok = true;
    bool first = true;

    for (cJSON *c = cookies->child; ok && c; c = c->next) {
        if (!cJSON_IsString(c)) {
            continue;
        }
        if (!first) {
            ok = creq_buf_append_str(&buf, "; ");
        }
        first = false;
        ok = ok && creq_buf_append_str(&buf, c->string)
                 && creq_buf_append_str(&buf, "=")
                 && creq_buf_append_str(&buf, c->valuestring);
    }

    if (ok) {
        ok = creq_buf_append(&buf, "", 1);
    }

    if (!ok) {
        creq_buf_free(&buf);
        *out_ok = false;
        return NULL;
    }

    return (char *)buf.data;
}

unsigned char *creq_build_raw_request(const CReqRequest *request, const CReqUrl *url,
                                       const CReqSession *session, size_t *out_length) {
    cJSON *final_headers = NULL;
    cJSON *final_cookies = NULL;
    char *final_path = NULL;
    char *cookie_header_value = NULL;
    CReqBuf out = {0};
    bool ok = true;

    /* 1) Mescla headers/cookies da sessão (base) com os da requisição
     *    (que têm prioridade em caso de conflito de nomes). */
    if (session) {
        ok = merge_dict_into(&final_headers, session->default_headers, true)
          && merge_dict_into(&final_cookies, session->cookie_jar, false);
    }
    ok = ok
      && merge_dict_into(&final_headers, request->headers, true)
      && merge_dict_into(&final_cookies, request->cookies, false);

    if (!ok) {
        goto cleanup;
    }

    /* 2) Path final, com os parâmetros de query devidamente
     *    codificados e anexados. */
    final_path = build_path_with_query(url->path, request->query_params);
    if (!final_path) {
        ok = false;
        goto cleanup;
    }

    /* 3) Headers "de infraestrutura" completados automaticamente,
     *    apenas quando o chamador ainda não os definiu manualmente. */
    if (!creq_dict_find(final_headers, "Host", true)) {
        bool is_default_port = (url->is_tls && strcmp(url->port, CREQ_HTTPS_PORT) == 0) ||
                                (!url->is_tls && strcmp(url->port, CREQ_HTTP_PORT) == 0);
        char host_header[512];
        if (is_default_port) {
            snprintf(host_header, sizeof(host_header), "%s", url->host);
        } else {
            snprintf(host_header, sizeof(host_header), "%s:%s", url->host, url->port);
        }
        ok = creq_dict_set(&final_headers, "Host", host_header, true);
    }
    ok = ok && (creq_dict_find(final_headers, "User-Agent", true) ||
                creq_dict_set(&final_headers, "User-Agent", "crequests/1.0", true));
    ok = ok && (creq_dict_find(final_headers, "Accept", true) ||
                creq_dict_set(&final_headers, "Accept", "*/*", true));
    /* Este cliente não implementa keep-alive/pipelining: cada
     * requisição abre sua própria conexão TCP/TLS e a fecha ao
     * terminar (ver client.c). "Connection: close" é o valor correto
     * e evita que o servidor fique esperando reaproveitar uma conexão
     * que nunca mais será usada. */
    ok = ok && (creq_dict_find(final_headers, "Connection", true) ||
                creq_dict_set(&final_headers, "Connection", "close", true));

    if (ok && request->body_length > 0 && !creq_dict_find(final_headers, "Content-Length", true)) {
        char len_buf[32];
        snprintf(len_buf, sizeof(len_buf), "%zu", request->body_length);
        ok = creq_dict_set(&final_headers, "Content-Length", len_buf, true);
    }

    if (!ok) {
        goto cleanup;
    }

    /* 4) Cookies mesclados viram um único header Cookie:, a menos que
     *    o chamador já tenha definido um header Cookie manualmente
     *    (nesse caso, respeitamos a escolha explícita dele). */
    if (!creq_dict_find(final_headers, "Cookie", true)) {
        bool cookie_ok;
        cookie_header_value = build_cookie_header(final_cookies, &cookie_ok);
        if (!cookie_ok) {
            ok = false;
            goto cleanup;
        }
        if (cookie_header_value) {
            ok = creq_dict_set(&final_headers, "Cookie", cookie_header_value, true);
            if (!ok) {
                goto cleanup;
            }
        }
    }

    /* 5) Serialização final: request-line + headers + linha vazia + corpo. */
    ok = creq_buf_append_str(&out, creq_method_to_string(request->method))
      && creq_buf_append_str(&out, " ")
      && creq_buf_append_str(&out, final_path)
      && creq_buf_append_str(&out, " HTTP/1.1\r\n");

    for (cJSON *h = ok && final_headers ? final_headers->child : NULL; ok && h; h = h->next) {
        if (!cJSON_IsString(h)) {
            continue;
        }
        ok = creq_buf_append_str(&out, h->string)
          && creq_buf_append_str(&out, ": ")
          && creq_buf_append_str(&out, h->valuestring)
          && creq_buf_append_str(&out, "\r\n");
    }

    ok = ok && creq_buf_append_str(&out, "\r\n");

    if (ok && request->body_length > 0) {
        ok = creq_buf_append(&out, request->body, request->body_length);
    }

cleanup:
    cJSON_Delete(final_headers);
    cJSON_Delete(final_cookies);
    free(final_path);
    free(cookie_header_value);

    if (!ok) {
        creq_buf_free(&out);
        log_error("creq_build_raw_request: falha ao montar a requisição (memória?)");
        return NULL;
    }

    *out_length = out.len;
    return out.data;
}
