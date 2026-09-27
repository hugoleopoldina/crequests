#include "internal.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* strcasecmp não é C padrão (é POSIX/BSD); no MSVC o equivalente se
 * chama _stricmp. Aqui evitamos a dependência inteira definindo nossa
 * própria comparação case-insensitive, que funciona igual em qualquer
 * plataforma sem precisar de feature-test macros. */
char *creq_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}

int creq_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

bool creq_buf_reserve(CReqBuf *buf, size_t extra) {
    if (buf->len + extra <= buf->cap) {
        return true; /* já cabe */
    }

    size_t new_cap = buf->cap == 0 ? 256 : buf->cap;
    while (new_cap < buf->len + extra) {
        new_cap *= 2;
    }

    unsigned char *tmp = (unsigned char *)realloc(buf->data, new_cap);
    if (!tmp) {
        return false;
    }

    buf->data = tmp;
    buf->cap = new_cap;
    return true;
}

bool creq_buf_append(CReqBuf *buf, const void *data, size_t len) {
    if (len == 0) {
        return true;
    }
    if (!creq_buf_reserve(buf, len)) {
        return false;
    }
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return true;
}

bool creq_buf_append_str(CReqBuf *buf, const char *str) {
    return creq_buf_append(buf, str, strlen(str));
}

void creq_buf_consume(CReqBuf *buf, size_t count) {
    if (count == 0) {
        return;
    }
    if (count >= buf->len) {
        /* todo o conteúdo foi consumido: só "esvazia" (mantém a
         * capacidade alocada para reaproveitar em leituras futuras) */
        buf->len = 0;
        return;
    }
    memmove(buf->data, buf->data + count, buf->len - count);
    buf->len -= count;
}

void creq_buf_free(CReqBuf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

cJSON *creq_dict_find(const cJSON *object, const char *key, bool case_insensitive) {
    if (!object) {
        return NULL;
    }
    for (cJSON *item = object->child; item; item = item->next) {
        if (!item->string) {
            continue;
        }
        int matches = case_insensitive
            ? (creq_strcasecmp(item->string, key) == 0)
            : (strcmp(item->string, key) == 0);
        if (matches) {
            return item;
        }
    }
    return NULL;
}

bool creq_dict_set(cJSON **object_ptr, const char *key, const char *value, bool case_insensitive) {
    if (!*object_ptr) {
        *object_ptr = cJSON_CreateObject();
        if (!*object_ptr) {
            return false;
        }
    }

    cJSON *existing = creq_dict_find(*object_ptr, key, case_insensitive);
    if (existing) {
        /* cJSON não tem uma forma direta de "renomear+atualizar" um
         * item sem recriar as strings internas; o jeito mais simples e
         * seguro é remover o antigo e adicionar de novo com o valor
         * novo. Isso muda a posição da chave para o fim da ordem de
         * iteração, o que é aceitável aqui (não fazemos promessa de
         * ordem estável entre atualizações). */
        cJSON_DeleteItemFromObjectCaseSensitive(*object_ptr, existing->string);
    }

    return cJSON_AddStringToObject(*object_ptr, key, value) != NULL;
}

void creq_dict_remove(cJSON *object, const char *key, bool case_insensitive) {
    cJSON *existing = creq_dict_find(object, key, case_insensitive);
    if (existing) {
        cJSON_DeleteItemFromObjectCaseSensitive(object, existing->string);
    }
}

char *creq_percent_encode(const char *value) {
    static const char *hex = "0123456789ABCDEF";
    size_t len = strlen(value);

    /* pior caso: todo byte vira "%XX" (3x o tamanho original) + nulo */
    char *out = (char *)malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }

    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)value[i];

        /* RFC 3986 "unreserved": letras, dígitos e -_.~ passam direto */
        bool is_unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';

        if (is_unreserved) {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[(c >> 4) & 0xF];
            out[o++] = hex[c & 0xF];
        }
    }
    out[o] = '\0';
    return out;
}
