#include "crequests/url.h"
#include "internal.h"
#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>
#include <string.h>

/* Compara protocolo por igualdade completa e case-insensitive (ex:
 * "HTTP" == "http"), diferente de creq_strcasecmp que também serve
 * para isso -- usamos ela diretamente aqui. */
#define ieq(a, b) (creq_strcasecmp((a), (b)) == 0)

void creq_url_free(CReqUrl *url) {
    if (!url) {
        return;
    }
    free(url->protocol);
    free(url->host);
    free(url->port);
    free(url->path);
    free(url);
}

CReqUrl *creq_url_parse(const char *url) {
    if (!url || !*url) {
        log_error("creq_url_parse: URL vazia");
        return NULL;
    }

    const char *scheme_sep = strstr(url, "://");
    if (!scheme_sep) {
        log_error("creq_url_parse: URL sem esquema (esperado http:// ou https://): %s", url);
        return NULL;
    }

    CReqUrl *result = (CReqUrl *)calloc(1, sizeof(CReqUrl));
    if (!result) {
        log_error("creq_url_parse: falha de memória");
        return NULL;
    }

    result->protocol = (char *)malloc((size_t)(scheme_sep - url) + 1);
    if (!result->protocol) {
        goto oom;
    }
    memcpy(result->protocol, url, (size_t)(scheme_sep - url));
    result->protocol[scheme_sep - url] = '\0';

    if (!ieq(result->protocol, "http") && !ieq(result->protocol, "https")) {
        log_error("creq_url_parse: esquema não suportado '%s' (use http ou https)", result->protocol);
        creq_url_free(result);
        return NULL;
    }
    result->is_tls = ieq(result->protocol, "https");

    const char *authority = scheme_sep + 3; /* logo após "://" */
    const char *authority_end;
    const char *path_start;

    /* suporte básico a literais IPv6 entre colchetes, ex: [::1]:8080/x */
    if (*authority == '[') {
        const char *bracket_end = strchr(authority, ']');
        if (!bracket_end) {
            log_error("creq_url_parse: literal IPv6 malformado em '%s'", url);
            creq_url_free(result);
            return NULL;
        }
        authority_end = bracket_end + 1; /* aponta logo após ']' */
    } else {
        authority_end = authority;
    }

    /* encontra o início do path: primeira '/' após o início da authority
     * (respeitando o literal IPv6, se houver) */
    path_start = strchr(authority_end, '/');

    const char *host_port_end = path_start ? path_start : (authority + strlen(authority));

    /* dentro de [host_port_end], separa host de porta pela ÚLTIMA ':'
     * -- exceto quando o host é um literal IPv6 (já sabemos onde ele
     * termina, então buscamos ':' só depois do ']') */
    const char *port_sep = NULL;
    {
        const char *search_from = (*authority == '[') ? authority_end : authority;
        const char *p = host_port_end;
        while (p > search_from) {
            p--;
            if (*p == ':') {
                port_sep = p;
                break;
            }
        }
    }

    size_t host_len = (size_t)((port_sep ? port_sep : host_port_end) - authority);
    if (host_len == 0) {
        log_error("creq_url_parse: host vazio em '%s'", url);
        creq_url_free(result);
        return NULL;
    }

    /* Remove os colchetes do literal IPv6 ao guardar o host: eles são
     * só uma convenção de sintaxe de URL (RFC 3986 §3.2.2) para
     * delimitar onde termina o host antes de ':porta' — getaddrinfo()
     * espera o endereço "puro", sem colchetes. */
    const char *host_start = authority;
    size_t host_copy_len = host_len;
    if (host_len >= 2 && authority[0] == '[' && authority[host_len - 1] == ']') {
        host_start = authority + 1;
        host_copy_len = host_len - 2;
    }

    result->host = (char *)malloc(host_copy_len + 1);
    if (!result->host) {
        goto oom;
    }
    memcpy(result->host, host_start, host_copy_len);
    result->host[host_copy_len] = '\0';

    if (port_sep) {
        size_t port_len = (size_t)(host_port_end - (port_sep + 1));
        if (port_len == 0) {
            log_error("creq_url_parse: porta vazia em '%s'", url);
            creq_url_free(result);
            return NULL;
        }
        result->port = (char *)malloc(port_len + 1);
        if (!result->port) {
            goto oom;
        }
        memcpy(result->port, port_sep + 1, port_len);
        result->port[port_len] = '\0';
    } else {
        result->port = creq_strdup(result->is_tls ? CREQ_HTTPS_PORT : CREQ_HTTP_PORT);
        if (!result->port) {
            goto oom;
        }
    }

    result->path = creq_strdup(path_start ? path_start : "/");
    if (!result->path) {
        goto oom;
    }

    return result;

oom:
    log_error("creq_url_parse: falha de memória");
    creq_url_free(result);
    return NULL;
}
