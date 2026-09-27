/*
 * Responsabilidade única: transformar uma CReqRequest (+ a CReqUrl já
 * analisada e, opcionalmente, uma CReqSession) nos bytes brutos de uma
 * mensagem de requisição HTTP/1.1 prontos para serem enviados pela
 * rede. Isto é o "espelho" do parser (parser.h), que faz o caminho
 * inverso para a resposta.
 */
#ifndef CREQ_REQUEST_BUILDER_H
#define CREQ_REQUEST_BUILDER_H

#include <stddef.h>
#include "crequests/request.h"
#include "crequests/url.h"
#include "crequests/session.h"

/* Monta a requisição HTTP/1.1 completa (request-line + headers +
 * Cookie: + linha vazia + corpo) em um buffer alocado com malloc.
 * '*out_length' recebe o tamanho em bytes do resultado (NÃO é uma
 * string C: o corpo pode conter bytes arbitrários, incluindo nulos).
 * Retorna NULL em caso de falha de memória.
 *
 * 'session' pode ser NULL (requisição avulsa, sem cookies/headers de
 * sessão a mesclar).
 */
unsigned char *creq_build_raw_request(const CReqRequest *request, const CReqUrl *url,
                                       const CReqSession *session, size_t *out_length);

#endif /* CREQ_REQUEST_BUILDER_H */
