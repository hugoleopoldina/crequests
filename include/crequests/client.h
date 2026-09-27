/*
 * API de alto nível: inicialização global, a função "mestre" que
 * executa qualquer CReqRequest, e atalhos por método HTTP.
 */
#ifndef CREQ_CLIENT_H
#define CREQ_CLIENT_H

#include <stdbool.h>
#include "crequests/request.h"
#include "crequests/response.h"
#include "crequests/session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inicializa recursos globais e compartilhados por todas as
 * requisições/sessões do processo: contexto TLS (OpenSSL) e, no
 * Windows, o Winsock (WSAStartup). Chame uma única vez, antes de
 * qualquer creq_perform()/creq_get()/etc. Retorna false em caso de
 * falha (ver log_error para detalhes, via ctinylogger). */
bool creq_global_init(void);

/* Libera os recursos globais inicializados por creq_global_init().
 * Chame uma única vez, ao encerrar o uso da lib. */
void creq_global_cleanup(void);

/*
 * Função mestre: executa 'request' (que já deve ter método, URL e,
 * opcionalmente, headers/cookies/query params/corpo definidos — ver
 * request.h e fields.h) e retorna a resposta completa, ou NULL em
 * caso de falha de rede/TLS/parsing (erros são registrados via
 * log_error/log_warning).
 *
 * 'session', se não for NULL, tem seus cookies aplicados à
 * requisição antes de enviá-la, e é atualizada com quaisquer cookies
 * recebidos na resposta (ver session.h).
 *
 * 'request' não é liberada por esta função — o chamador continua
 * dono dela e deve chamar creq_request_free() quando terminar. O
 * retorno (CReqResponse*), por sua vez, é responsabilidade do
 * chamador liberar com creq_response_free().
 */
CReqResponse *creq_perform(CReqSession *session, CReqRequest *request);

/*
 * Atalhos que constroem uma CReqRequest simples internamente (sem
 * headers/cookies/params customizados), a executam via creq_perform()
 * e já liberam a requisição temporária antes de retornar — o
 * chamador só precisa se preocupar em liberar a CReqResponse
 * resultante. Para uma requisição customizada (headers extras,
 * cookies, parâmetros, corpo), monte a CReqRequest você mesmo com
 * creq_request_create() + fields.h e chame creq_perform() diretamente.
 */
CReqResponse *creq_get(CReqSession *session, const char *url);
CReqResponse *creq_head(CReqSession *session, const char *url);
CReqResponse *creq_delete(CReqSession *session, const char *url);

/* 'content_type' pode ser NULL (nenhum header Content-Type é definido
 * automaticamente nesse caso — defina o seu via creq_request_create()
 * + creq_header_set() se precisar de controle total). */
CReqResponse *creq_post(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type);
CReqResponse *creq_put(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type);
CReqResponse *creq_patch(CReqSession *session, const char *url, const void *body, size_t body_length, const char *content_type);

/* Variante de creq_post() que serializa 'json' como corpo e define
 * Content-Type: application/json automaticamente. Assume posse de
 * 'json' (cJSON_Delete interno), igual creq_request_set_json_body(). */
CReqResponse *creq_post_json(CReqSession *session, const char *url, cJSON *json);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_CLIENT_H */
