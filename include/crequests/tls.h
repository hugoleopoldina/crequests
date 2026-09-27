/*
 * Camada fina sobre a API cliente do OpenSSL: um único SSL_CTX
 * compartilhado por todo o processo (criado em creq_global_init()) e
 * funções para abrir uma conexão TLS sobre um socket TCP já conectado,
 * enviar e ler dados.
 *
 * O SSL_CTX é a mesma implementação em Linux, Windows e Termux — o
 * OpenSSL já é multiplataforma por conta própria, então não há
 * necessidade de arquivos por SO aqui.
 */
#ifndef CREQ_TLS_H
#define CREQ_TLS_H

#include <stdbool.h>
#include <stddef.h>
#include <openssl/ssl.h>

#include "crequests/socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cria o contexto SSL global (chamado por creq_global_init()). */
bool creq_tls_context_create(void);

/* Libera o contexto SSL global (chamado por creq_global_cleanup()). */
void creq_tls_context_free(void);

/* Realiza o handshake TLS sobre 'sockfd' (já conectado), usando SNI
 * com 'host'. Retorna a sessão SSL pronta para uso, ou NULL em caso de
 * falha (erro já registrado via log_error). O chamador deve liberar o
 * resultado com SSL_free() após o uso. */
SSL *creq_tls_connect(creq_socket_t sockfd, const char *host);

/* Envia 'length' bytes de 'data' pela sessão TLS. Retorna a quantidade
 * de bytes enviados, ou -1 em caso de erro. */
int creq_tls_send(SSL *ssl, const void *data, size_t length);

/* Lê até 'buf_size' bytes da sessão TLS para 'buf'. Retorna a
 * quantidade lida (> 0), 0 se a conexão foi fechada normalmente pelo
 * peer, ou um valor negativo em caso de erro. */
int creq_tls_read(SSL *ssl, unsigned char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* CREQ_TLS_H */
