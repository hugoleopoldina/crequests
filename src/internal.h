/*
 * Utilitários internos compartilhados entre os .c da lib. Nada aqui
 * faz parte da API pública (não fica em include/, não é instalado).
 */
#ifndef CREQ_INTERNAL_H
#define CREQ_INTERNAL_H

#include <stddef.h>
#include <stdbool.h>
#include <cJSON.h>

/* --------------------------------------------------------------------
 * Buffer de bytes com crescimento amortizado (dobra de tamanho a cada
 * realloc), usado tanto para acumular a requisição HTTP sendo montada
 * (request_builder.c) quanto os bytes brutos ainda não processados
 * pelo parser de resposta (parser.c).
 * ------------------------------------------------------------------ */
typedef struct {
    unsigned char *data;
    size_t len;   /* bytes válidos escritos */
    size_t cap;   /* capacidade alocada */
} CReqBuf;

/* Garante espaço para pelo menos 'extra' bytes adicionais além de
 * buf->len, crescendo (dobrando) o buffer se necessário. Retorna false
 * em caso de falha de alocação (o buffer original permanece intacto). */
bool creq_buf_reserve(CReqBuf *buf, size_t extra);

/* Acrescenta 'len' bytes de 'data' ao final do buffer. */
bool creq_buf_append(CReqBuf *buf, const void *data, size_t len);

/* Acrescenta uma string C (sem o terminador nulo) ao final do buffer. */
bool creq_buf_append_str(CReqBuf *buf, const char *str);

/* Remove os primeiros 'count' bytes já consumidos do início do buffer,
 * deslocando o restante para o começo (memmove). Usado pelo parser
 * para não deixar o buffer de entrada crescer indefinidamente durante
 * uma resposta grande: uma vez que uma linha/pedaço de corpo é
 * totalmente processado, ele é descartado daqui. */
void creq_buf_consume(CReqBuf *buf, size_t count);

/* Libera a memória do buffer e zera a estrutura. */
void creq_buf_free(CReqBuf *buf);

/* --------------------------------------------------------------------
 * Pequeno "dicionário" string:string sobre cJSON, com busca
 * case-insensitive (necessário para headers HTTP, que por definição
 * não diferenciam maiúsculas de minúsculas — RFC 7230 §3.2) e
 * case-sensitive (para cookies e parâmetros de query, onde a caixa
 * importa). Usado por fields.c (headers/cookies/params da requisição)
 * e por response.c/parser.c (headers/cookies da resposta).
 * ------------------------------------------------------------------ */

/* Comparação de strings case-insensitive, portável (não depende de
 * strcasecmp POSIX nem de _stricmp do MSVC). Usada para nomes de
 * header HTTP e para comparar valores como "chunked" sem se importar
 * com a caixa usada pelo servidor. */
int creq_strcasecmp(const char *a, const char *b);

/* strdup() é POSIX, não C padrão -- em modo estritamente C11 no MSVC
 * ou com _POSIX_C_SOURCE indefinido ela pode não estar declarada. Uma
 * cópia própria evita depender de feature-test macros só por causa
 * desta função. Retorna NULL em caso de falta de memória. */
char *creq_strdup(const char *s);

/* Busca 'key' em 'object' (que pode ser NULL, retornando NULL nesse
 * caso). Se 'case_insensitive' for true, compara com strcasecmp. */
cJSON *creq_dict_find(const cJSON *object, const char *key, bool case_insensitive);

/* Cria ou atualiza a chave 'key' em '*object_ptr' com o valor 'value'.
 * Cria o objeto cJSON sob demanda se '*object_ptr' for NULL. Retorna
 * false em caso de falha de alocação. */
bool creq_dict_set(cJSON **object_ptr, const char *key, const char *value, bool case_insensitive);

/* Remove 'key' de 'object', se existir (no-op se 'object' for NULL ou
 * a chave não existir). */
void creq_dict_remove(cJSON *object, const char *key, bool case_insensitive);

/* --------------------------------------------------------------------
 * URL-encoding (application/x-www-form-urlencoded, RFC 3986) de um
 * valor a ser usado como parâmetro de query. Retorna uma string
 * alocada com malloc (o chamador deve dar free()), ou NULL em caso de
 * falta de memória.
 * ------------------------------------------------------------------ */
char *creq_percent_encode(const char *value);

#endif /* CREQ_INTERNAL_H */
