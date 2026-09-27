#include "crequests/tls.h"
#include <ctinylogger/ctinylogger.h>

#include <openssl/err.h>
#include <string.h>

/* Único contexto SSL do processo, compartilhado por todas as conexões
 * TLS abertas por qualquer sessão/requisição. Criar um SSL_CTX por
 * conexão seria um desperdício -- ele guarda configuração (métodos,
 * verificação de certificado, etc.), não estado de uma conexão
 * específica (isso é o SSL* individual). */
static SSL_CTX *g_ssl_ctx = NULL;

bool creq_tls_context_create(void) {
    g_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!g_ssl_ctx) {
        log_error("Não foi possível criar o contexto SSL");
        ERR_print_errors_fp(stderr);
        return false;
    }
    return true;
}

void creq_tls_context_free(void) {
    SSL_CTX_free(g_ssl_ctx);
    g_ssl_ctx = NULL;
}

SSL *creq_tls_connect(creq_socket_t sockfd, const char *host) {
    if (!g_ssl_ctx) {
        log_error("creq_tls_connect chamado antes de creq_global_init()");
        return NULL;
    }

    SSL *ssl = SSL_new(g_ssl_ctx);
    if (!ssl) {
        log_error("Falha ao criar a instância SSL");
        return NULL;
    }

    if (!SSL_set_fd(ssl, (int)sockfd)) {
        log_error("Não foi possível associar o socket à instância SSL");
        SSL_free(ssl);
        return NULL;
    }

    /* SNI (Server Name Indication): necessário para que servidores que
     * hospedam vários domínios no mesmo IP/porta apresentem o
     * certificado correto. */
    if (!SSL_set_tlsext_host_name(ssl, host)) {
        log_error("Não foi possível definir o SNI para o host '%s'", host);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    int ret = SSL_connect(ssl);
    if (ret != 1) {
        int err = SSL_get_error(ssl, ret);
        log_error("Handshake TLS falhou com '%s': ret=%d ssl_error=%d", host, ret, err);
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        return NULL;
    }

    return ssl;
}

int creq_tls_send(SSL *ssl, const void *data, size_t length) {
    const char *cursor = (const char *)data;
    size_t remaining = length;
    int total_sent = 0;

    while (remaining > 0) {
        int chunk = (remaining > (size_t)0x7FFFFFFF) ? 0x7FFFFFFF : (int)remaining;
        int sent = SSL_write(ssl, cursor, chunk);
        if (sent <= 0) {
            log_error("Falha ao enviar dados TLS: ret=%d ssl_error=%d", sent, SSL_get_error(ssl, sent));
            return -1;
        }
        cursor += sent;
        remaining -= (size_t)sent;
        total_sent += sent;
    }
    return total_sent;
}

int creq_tls_read(SSL *ssl, unsigned char *buf, size_t buf_size) {
    int size = (buf_size > (size_t)0x7FFFFFFF) ? 0x7FFFFFFF : (int)buf_size;
    int ret = SSL_read(ssl, buf, size);
    if (ret > 0) {
        return ret;
    }

    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_ZERO_RETURN) {
        return 0; /* o peer mandou close_notify: fim limpo da conexão TLS */
    }
    /*
     * SSL_ERROR_SYSCALL com ret == 0 normalmente indica que a conexão
     * TCP foi fechada sem um close_notify TLS -- tecnicamente uma
     * finalização "suja" da sessão TLS, mas extremamente comum na
     * prática (muitos servidores fazem isso). Tratamos como EOF normal
     * em vez de erro, já que o client.c decide se isso é aceitável
     * consultando o estado do parser (ver creq_parser_finish()).
     */
    if (err == SSL_ERROR_SYSCALL && ret == 0) {
        return 0;
    }

    log_error("Falha na leitura TLS: ret=%d ssl_error=%d", ret, err);
    return -1;
}
