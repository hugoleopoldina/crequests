/*
 * CReqResponse: estrutura bem definida com TUDO que uma resposta HTTP
 * traz de volta — status, versão, headers, cookies e o corpo em bytes.
 * É o valor de retorno de creq_perform() e dos atalhos de client.h.
 */
#ifndef CREQ_RESPONSE_H
#define CREQ_RESPONSE_H

#include <stddef.h>
#include <cJSON.h>

#include "crequests/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CReqResponse {
    char http_version[16];   /* ex: "1.1" */
    int  status_code;        /* 100-599 */
    char reason_phrase[64];  /* ex: "Not Found" */

    /*
     * Headers da resposta (exceto Set-Cookie, ver 'cookies' abaixo),
     * como um objeto cJSON {"nome-do-header": "valor", ...}. Nomes são
     * armazenados exatamente como o servidor os enviou, mas devem ser
     * buscados de forma case-insensitive — use creq_response_header_get()
     * em vez de mexer neste cJSON diretamente.
     *
     * Observação: por simplicidade, se o servidor repetir um header
     * (raro, exceto Set-Cookie), apenas o último valor é mantido.
     */
    cJSON *headers;

    /*
     * Um cookie por entrada {"nome": "valor", ...}, extraído de todos
     * os headers "Set-Cookie" da resposta (que não podem ser
     * armazenados em 'headers' como os demais, pois frequentemente
     * aparecem repetidos e seus atributos — Path, Expires, etc. — usam
     * ';' e ',' de um jeito que quebraria uma junção ingênua). Apenas o
     * par nome=valor de cada cookie é mantido; atributos são
     * descartados.
     */
    cJSON *cookies;

    CReqBodyType body_type;  /* como o corpo foi delimitado (ver types.h) */
    unsigned char *body;     /* bytes crus do corpo (pode ser NULL se body_length == 0) */
    size_t body_length;
} CReqResponse;

/* Cria uma CReqResponse vazia, pronta para ser preenchida
 * incrementalmente pelo parser (ver parser.h). Uso interno da lib
 * (client.c); exposto aqui caso o chamador queira usar o parser
 * diretamente. */
CReqResponse *creq_response_create(void);

/* Libera uma CReqResponse e todo o seu conteúdo (headers, cookies, corpo). */
void creq_response_free(CReqResponse *response);

/* Busca um header da resposta por nome, de forma case-insensitive.
 * Retorna NULL se não existir. Ponteiro interno — não libere. */
const char *creq_response_header_get(const CReqResponse *response, const char *name);

/* Busca um cookie da resposta por nome (case-sensitive). Retorna NULL
 * se não existir. Ponteiro interno — não libere. */
const char *creq_response_cookie_get(const CReqResponse *response, const char *name);

/* Converte o corpo da resposta em um objeto json */
inline cJSON* creq_response_body_to_json(CReqResponse* response)
    { return cJSON_ParseWithLength((char*)response->body, response->body_length);  }

#ifdef __cplusplus
}
#endif

#endif /* CREQ_RESPONSE_H */
