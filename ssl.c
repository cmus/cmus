#include "ssl.h"
#include "debug.h"
#include "ip.h" /* IP_ERROR_OPENSSL */
#include "http.h" /* sockfd_ssl */

#include <unistd.h> /* read, write */
#include <openssl/ssl.h>

static SSL_CTX *ssl_context = NULL;


int init_ssl_context(void)
{
    ssl_context = SSL_CTX_new(TLS_client_method());

    if (ssl_context == NULL) {
        d_print("SSL_CTX_new() failed\n");
		return -IP_ERROR_OPENSSL;
	}

    if (SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION) != 1){ /* Older TLS versions than TLS 1.2 are deprecated. */
		d_print("unable to set min_version to TLS1.2");
		return -IP_ERROR_OPENSSL;
	}

    SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, NULL);	/* Prevent an attacker from impersonating server */

	if (SSL_CTX_set_default_verify_paths(ssl_context) != 1){ /* Use system's defaults location for CA cerificates */
        d_print("Unable to use default location for CA certificates\n");
		return -IP_ERROR_OPENSSL;
	}

	return IP_ERROR_SUCCESS;
}

static void wait_eof(SSL* ssl)
{
	char b;
	int rc=1;
	while(rc>0){
		rc = SSL_read(ssl, &b, 1);
	}
}

int ssl_close(SSL* ssl)
{
	int ret = SSL_shutdown(ssl);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		wait_eof(ssl);
		if (SSL_shutdown(ssl) != 1) {
			d_print("SSL_shutdown failed\n");
			return -IP_ERROR_OPENSSL;
		}
	}

	if (ssl != NULL)
		SSL_free(ssl);

	if (ssl_context != NULL)
		SSL_CTX_free(ssl_context);

	return SSL_ERROR_NONE;
}

int ssl_init(struct http_get *hg)
{
	int rc;

	if(ssl_context == NULL){
		rc = init_ssl_context();
		if(rc)
			return rc;
	}

	struct connection *conn = hg->conn;
	SSL *ssl = SSL_new(ssl_context);
	if (ssl == NULL) {
		d_print("Failed to create SSL struct\n");
		return -IP_ERROR_OPENSSL;
	}
	SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE); /* Imitate the behavior of write */
	conn->ssl = ssl;

	if (SSL_set_fd(conn->ssl, *conn->fd_ref) != 1) {
		d_print("Failed to set the file descriptor\n");
		return -IP_ERROR_OPENSSL;
	}

	return IP_ERROR_SUCCESS;
}

int ssl_connect(struct http_get *hg)
{
		if(ssl_init(hg))
			return -IP_ERROR_OPENSSL;

		struct connection *conn = hg->conn;
		int rc = SSL_connect(conn->ssl); /* 1 if successful, <=0 else */
        if (rc <= 0) {
			int err = SSL_get_error(conn->ssl, rc);
			d_print("SSL_connect() failed (%d), SSL_get_error() returns %d\n", rc, err);
            return -IP_ERROR_OPENSSL;
        }
		return rc - 1; /* 0 on success */
}

/*
 * Checking EOF with SSL_read() is undocumented.
 * Need to access underlying BIO (automatically set by SSL_set_fd())
 * See https://github.com/openssl/openssl/issues/1903#issuecomment-264599892
 */
int handle_ssl_error(SSL* ssl, int ret)
{
	int err = SSL_get_error(ssl, ret);
	if(err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE){
		return -1; /* try again */
	} else if (err == SSL_ERROR_SYSCALL && BIO_eof(SSL_get_rbio(ssl))){
		return 0; /* EOF */
	}
	d_print("SSL encountered an error: %d\n", err);
	return -1;
}

int https_write(struct connection *conn, const char *in_buf, int count)
{
	SSL* ssl = conn->ssl;
	int ret = SSL_write(ssl, in_buf, count); /* >0 on success, <=0 else */
	if(ret <= 0){
		return handle_ssl_error(ssl, ret);
	}
	return ret;
}

int https_read(struct connection *conn, char *out_buf, int count)
{
	SSL *ssl = conn->ssl;
	int ret = SSL_read(ssl, out_buf, count); /* >0 on success, <=0 else */
	if(ret <= 0){
		return handle_ssl_error(ssl, ret);
	}
	return ret;
}

int socket_read(struct connection *conn, char *out_buf, int count)
{
	return read(*conn->fd_ref, out_buf, count);
}

int socket_write(struct connection *conn, const char *in_buf, int count)
{
	return write(*conn->fd_ref, in_buf, count);
}