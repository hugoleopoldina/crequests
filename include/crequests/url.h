/*
 * Separação de uma URL em suas partes (protocolo, host, porta, path +
 * query string). Suporta apenas http/https, que é tudo que este cliente
 * fala.
 */
#ifndef CREQ_URL_H
#define CREQ_URL_H

#ifdef __cplusplus
extern "C" {
#endif

#define CREQ_HTTP_PORT  "80"
#define CREQ_HTTPS_PORT "443"

typedef struct CReqUrl {
    /* Exemplo para "https://user@www.example.com:8443/caminho?a=1" */
    char *protocol; /* "http" | "https" */
    char *host;     /* "www.example.com" */
    char *port;     /* "8443", ou a porta padrão do protocolo se omitida */
    char *path;     /* "/caminho?a=1" (inclui a query string, se houver) */
    int   is_tls;   /* 1 se protocol == "https", 0 caso contrário (atalho) */
} CReqUrl;

/* Analisa uma URL completa (com esquema) em suas partes. Retorna NULL em
 * caso de formato inválido ou falta de memória. O resultado deve ser
 * liberado com creq_url_free(). */
CReqUrl *creq_url_parse(const char *url);

/* Libera uma CReqUrl criada por creq_url_parse(). */
void creq_url_free(CReqUrl *url);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_URL_H */
