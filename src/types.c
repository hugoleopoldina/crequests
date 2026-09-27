#include "crequests/types.h"

const char *creq_method_to_string(CReqMethod method) {
    switch (method) {
        case CREQ_METHOD_GET:     return "GET";
        case CREQ_METHOD_POST:    return "POST";
        case CREQ_METHOD_PUT:     return "PUT";
        case CREQ_METHOD_PATCH:   return "PATCH";
        case CREQ_METHOD_DELETE:  return "DELETE";
        case CREQ_METHOD_HEAD:    return "HEAD";
        case CREQ_METHOD_OPTIONS: return "OPTIONS";
        default:                 return "GET";
    }
}
