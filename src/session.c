#include "crequests/session.h"
#include "internal.h"
#include <ctinylogger/ctinylogger.h>

#include <stdlib.h>

CReqSession *creq_session_create(void) {
    CReqSession *session = (CReqSession *)calloc(1, sizeof(CReqSession));
    if (!session) {
        log_error("creq_session_create: falha de memória");
    }
    return session;
}

void creq_session_free(CReqSession *session) {
    if (!session) {
        return;
    }
    cJSON_Delete(session->cookie_jar);
    cJSON_Delete(session->default_headers);
    free(session);
}

bool creq_session_set_default_header(CReqSession *session, const char *name, const char *value) {
    if (!session || !name || !value) {
        return false;
    }
    return creq_dict_set(&session->default_headers, name, value, true);
}

void creq_session_clear_cookies(CReqSession *session) {
    if (!session) {
        return;
    }
    cJSON_Delete(session->cookie_jar);
    session->cookie_jar = NULL;
}
