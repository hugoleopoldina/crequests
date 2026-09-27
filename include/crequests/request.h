/*
 * CReqRequest: estrutura que descreve uma solicitação HTTP antes de ser
 * enviada (método, URL, headers, cookies, parâmetros de query e corpo).
 * É construída pelo chamador, opcionalmente customizada com as funções
 * de fields.h, e então passada para creq_perform() (client.h).
 */
#ifndef CREQ_REQUEST_H
#define CREQ_REQUEST_H

#include <stddef.h>
#include <stdbool.h>
#include <cJSON.h>

#include "crequests/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CReqRequest {
    CReqMethod method;
    char *url; /* URL completa e crua, como passada pelo chamador */

    /*
     * headers, cookies e query_params são objetos cJSON no formato
     * {"chave": "valor", ...} (sempre string:string). Usar cJSON aqui
     * dá de graça: iteração ordenada por inserção, busca por chave e
     * um jeito natural de crescer/remover entradas sem reinventar uma
     * hashtable própria. Podem ser NULL até a primeira chamada de
     * creq_header_set/creq_cookie_set/creq_query_set correspondente
     * (criados sob demanda).
     */
    cJSON *headers;
    cJSON *cookies;
    cJSON *query_params;

    /* Corpo bruto da requisição (ex: JSON serializado, form-urlencoded,
     * bytes arbitrários). NULL/0 se não houver corpo. */
    unsigned char *body;
    size_t body_length;
    bool owns_body; /* se true, creq_request_free() libera 'body' */
} CReqRequest;

/* Cria uma requisição para 'url' com o método informado. 'url' é
 * copiada internamente (o chamador pode liberar/reutilizar seu próprio
 * buffer). Retorna NULL em caso de falta de memória. */
CReqRequest *creq_request_create(CReqMethod method, const char *url);

/* Libera uma CReqRequest e todos os campos que ela possui (headers,
 * cookies, query_params, e o corpo se owns_body for true). */
void creq_request_free(CReqRequest *request);

/*
 * Define o corpo da requisição a partir de bytes arbitrários.
 * Se 'copy' for true, os dados são duplicados internamente (o chamador
 * continua dono do buffer original); se false, a requisição passa a ser
 * dona do ponteiro 'data' (deve ter sido alocado com malloc/realloc) e
 * o libera em creq_request_free().
 * NÃO define Content-Type automaticamente — use creq_header_set() para
 * isso, ou prefira creq_request_set_json_body() para corpos JSON.
 */
bool creq_request_set_body(CReqRequest *request, const void *data, size_t length, bool copy);

/*
 * Serializa 'json' (formato compacto, sem espaços) e o define como
 * corpo da requisição, além de setar automaticamente os headers
 * "Content-Type: application/json" e "Content-Length". A lib assume
 * posse de 'json' e o libera (cJSON_Delete) internamente — não o
 * acesse nem o libere após esta chamada.
 * Retorna false (e não consome 'json') em caso de falha de serialização
 * ou de memória.
 */
bool creq_request_set_json_body(CReqRequest *request, cJSON *json);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_REQUEST_H */
