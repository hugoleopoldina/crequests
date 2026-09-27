/*
 * CReqSession: estrutura reutilizável entre várias solicitações. Hoje
 * seu principal papel é manter um "cookie jar" — os cookies recebidos
 * via Set-Cookie em uma resposta são automaticamente aplicados às
 * próximas requisições feitas com a mesma sessão, do jeito que
 * navegadores (e a lib "requests" do Python) fazem.
 *
 * Uma sessão é opcional: passar NULL para creq_perform()/creq_get()/etc.
 * faz uma solicitação "avulsa", sem persistir cookies.
 */
#ifndef CREQ_SESSION_H
#define CREQ_SESSION_H

#include <stdbool.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CReqSession {
    /* Cookies acumulados de todas as respostas já recebidas nesta
     * sessão, no formato {"nome": "valor", ...}. Aplicados a toda
     * requisição futura feita com esta sessão (mesclados com — e
     * sobrescritos por — cookies definidos manualmente na requisição
     * via creq_cookie_set()). */
    cJSON *cookie_jar;

    /*
     * Headers aplicados a toda requisição feita com esta sessão (ex:
     * um "Authorization" ou "User-Agent" fixo), no formato
     * {"nome": "valor", ...}. Também mesclados com — e sobrescritos
     * por — headers definidos manualmente na requisição.
     */
    cJSON *default_headers;
} CReqSession;

/* Cria uma sessão vazia (sem cookies nem headers padrão ainda). */
CReqSession *creq_session_create(void);

/* Libera a sessão e todo o seu conteúdo. */
void creq_session_free(CReqSession *session);

/* Define um header padrão, aplicado a toda requisição futura desta
 * sessão (a menos que a própria requisição já defina um header de
 * mesmo nome, que tem prioridade). */
bool creq_session_set_default_header(CReqSession *session, const char *name, const char *value);

/* Limpa todos os cookies acumulados na sessão (ex: para simular
 * logout). Não afeta default_headers. */
void creq_session_clear_cookies(CReqSession *session);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_SESSION_H */
