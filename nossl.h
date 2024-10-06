#ifndef CMUS_NOSSL_H
#define CMUS_NOSSL_H

#include "http.h"

int https_connection_open(struct http_get *hg, struct connection *conn){
    d_print("OpenSSL support disabled at build time, cannot open HTTPS streams\n");
    return -1;
};

#endif