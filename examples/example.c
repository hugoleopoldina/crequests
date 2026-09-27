/*
 * Exemplo de uso da crequests. Compilado automaticamente pelo
 * CMakeLists.txt (alvo "crequests_example") a menos que
 * CREQUESTS_BUILD_EXAMPLE=OFF seja passado ao configurar o projeto.
 */
#include <crequests/crequests.h>
#include <stdio.h>
#include <string.h>

static void print_response(CReqResponse *response) {
    if (!response) {
        printf("Requisição falhou (ver logs acima)\n");
        return;
    }
    printf("HTTP/%s %d %s\n", response->http_version, response->status_code, response->reason_phrase);
    printf("Corpo (%zu bytes): %.*s\n\n", response->body_length,
           (int)response->body_length, (const char *)response->body);
}

int main(void) {
    if (!creq_global_init()) {
        return 1;
    }

    /* Uma sessão persiste cookies entre requisições, como um
     * navegador faz. Passe NULL em vez de 'session' em qualquer
     * chamada abaixo para uma requisição avulsa, sem esse
     * comportamento. */
    CReqSession *session = creq_session_create();
    creq_session_set_default_header(session, "User-Agent", "crequests-example/1.0");

    /* --- GET simples --- */
    CReqResponse *r1 = creq_get(session, "https://httpbin.org/get");
    print_response(r1);
    creq_response_free(r1);

    /* --- GET customizado: headers, cookies e parâmetros de query --- */
    CReqRequest *req = creq_request_create(CREQ_METHOD_GET, "https://httpbin.org/get");
    creq_header_set(req, "X-Meu-Header", "valor-customizado");
    creq_cookie_set(req, "preferencia", "modo-escuro");
    creq_query_set(req, "busca", "café & pão"); /* percent-encoded automaticamente */
    creq_query_set(req, "pagina", "2");

    CReqResponse *r2 = creq_perform(session, req);
    print_response(r2);
    creq_request_free(req);
    creq_response_free(r2);

    /* --- POST com corpo JSON --- */
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "nome", "Hugo");
    cJSON_AddStringToObject(payload, "linguagem", "C");
    /* creq_post_json assume posse de 'payload' e o libera internamente */
    CReqResponse *r3 = creq_post_json(session, "https://httpbin.org/post", payload);
    print_response(r3);
    creq_response_free(r3);

    /* --- POST com corpo bruto (ex: form-urlencoded) --- */
    const char *form_body = "campo1=valor1&campo2=valor2";
    CReqResponse *r4 = creq_post(session, "https://httpbin.org/post",
                                  form_body, strlen(form_body),
                                  "application/x-www-form-urlencoded");
    print_response(r4);
    creq_response_free(r4);

    creq_session_free(session);
    creq_global_cleanup();
    return 0;
}
