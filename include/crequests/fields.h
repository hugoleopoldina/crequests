/*
 * Funções responsáveis por criar, atualizar, ler e remover os headers,
 * cookies e parâmetros de query de uma CReqRequest. Toda a manipulação
 * é feita sobre os objetos cJSON internos da requisição (ver
 * request.h) — este arquivo é o único lugar da lib que sabe como
 * "chave: valor" vira uma entrada cJSON, então é aqui que qualquer
 * ajuste futuro (ex: permitir múltiplos valores por chave) entraria.
 *
 * Todas as chaves são comparadas de forma insensível a maiúsculas para
 * headers (headers HTTP são case-insensitive por definição — RFC 7230
 * §3.2) e sensível para cookies/query params (nomes de cookie e de
 * parâmetro são case-sensitive na prática).
 */
#ifndef CREQ_FIELDS_H
#define CREQ_FIELDS_H

#include <stdbool.h>
#include "crequests/request.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Headers (case-insensitive) ------------------------------------ */

/* Cria ou atualiza um header. Se já existir um header com o mesmo nome
 * (comparação case-insensitive), seu valor é substituído. */
bool creq_header_set(CReqRequest *request, const char *name, const char *value);

/* Retorna o valor do header 'name', ou NULL se não existir. O ponteiro
 * retornado é interno (válido até a próxima modificação do header ou
 * até a requisição ser liberada) — não o libere nem o guarde. */
const char *creq_header_get(const CReqRequest *request, const char *name);

/* Remove o header 'name', se existir. Não é erro remover um header
 * inexistente (retorna normalmente). */
void creq_header_remove(CReqRequest *request, const char *name);

/* --- Cookies (case-sensitive) --------------------------------------- */

bool creq_cookie_set(CReqRequest *request, const char *name, const char *value);
const char *creq_cookie_get(const CReqRequest *request, const char *name);
void creq_cookie_remove(CReqRequest *request, const char *name);

/* --- Parâmetros de query (case-sensitive) --------------------------- */

bool creq_query_set(CReqRequest *request, const char *name, const char *value);
const char *creq_query_get(const CReqRequest *request, const char *name);
void creq_query_remove(CReqRequest *request, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_FIELDS_H */
