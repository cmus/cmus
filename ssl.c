#include "ssl.h"
#include "debug.h"
#include "ip.h" /* IP_ERROR_OPENSSL */
#include "http.h" /* sockfd_ssl */

#include <unistd.h> /* read, write */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <fcntl.h>

static SSL_CTX *ssl_context = NULL;

int init_ssl_context(void)
{
	ssl_context = SSL_CTX_new(TLS_client_method());

	if (ssl_context == NULL) {
		d_print("SSL_CTX_new() failed\n");
		return -IP_ERROR_OPENSSL;
	}

	/* Older TLS versions than TLS 1.2 are deprecated. */
	if (SSL_CTX_set_min_proto_version(ssl_context, TLS1_2_VERSION) != 1) {
		d_print("unable to set min_version to TLS1.2");
		return -IP_ERROR_OPENSSL;
	}

	/* Enable certificate verification */
	SSL_CTX_set_verify(ssl_context, SSL_VERIFY_PEER, NULL);

	if (SSL_CTX_set_default_verify_paths(ssl_context) != 1)	{
		d_print("Unable to use default location for CA certificates\n");
		return -IP_ERROR_OPENSSL;
	}

	return IP_ERROR_SUCCESS;
}

int init_ssl(struct connection *conn)
{
	int rc;
	if (ssl_context == NULL) {
		rc = init_ssl_context();
		if (rc)
			return rc;
	}

	SSL *ssl = SSL_new(ssl_context);
	if (ssl == NULL) {
		d_print("Failed to create SSL struct\n");
		return -IP_ERROR_OPENSSL;
	}
	SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE); /* Imitate the behavior of write */
	conn->ssl = ssl;

	if (SSL_set_fd(conn->ssl, get_sockfd(conn)) != 1) {
		d_print("Failed to set the file descriptor\n");
		return -IP_ERROR_OPENSSL;
	}
	return IP_ERROR_SUCCESS;
}

int ssl_open(struct connection *conn)
{
	if (init_ssl(conn))
		return -IP_ERROR_OPENSSL;

	int rc = SSL_connect(conn->ssl); /* 1 if successful, <=0 else */
	if (rc <= 0) {
		int err = SSL_get_error(conn->ssl, rc);
		d_print("SSL_connect() failed (%d), SSL_get_error() returns %d\n", rc, err);
		return -IP_ERROR_OPENSSL;
	}
	return rc - 1; /* 0 on success */
}

int https_connection_open(struct http_get *hg, struct connection *conn){
	if(hg->proxy != NULL) {
		/*
		 * TODO : Supporting proxy with HTTPS is not too difficult.
		 * We need to perform a CONNECT request before ssl_connect()
		 * We would need to use hg->uri.uri (instead of hg->uri.path for proxy with HTTP)
		 *
		 * In order to do that, we need to refactor the code that does GET requests
		 * to also support CONNECT request. This is getting out of scope for HTTPS support.
		 * We can return an error message for now.
		 */
		d_print("HTTPS stream with proxy not yet supported.\n");
		return -IP_ERROR_FUNCTION_NOT_SUPPORTED;
	}

	if (ssl_open(conn))
		return -IP_ERROR_OPENSSL;

	return IP_ERROR_SUCCESS;
}

/*
 * Checking EOF with SSL_read() is undocumented.
 * Need to access underlying BIO (automatically set by SSL_set_fd())
 * See https://github.com/openssl/openssl/issues/1903#issuecomment-264599892
 */

int handle_ssl_error(SSL *ssl, int ret)
{
	int err = SSL_get_error(ssl, ret);

	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
		errno = EAGAIN;
		return -IP_ERROR_ERRNO; /* try again */
	} else if (err == SSL_ERROR_ZERO_RETURN) {
		ssl_close(ssl);
		return -IP_ERROR_SUCCESS; /* Received a close_notify */ // TODO
	} else if (err == SSL_ERROR_SYSCALL && BIO_eof(SSL_get_rbio(ssl))) {
		return -IP_ERROR_SUCCESS; /* EOF */
	} else if (err == SSL_ERROR_SYSCALL) {
		d_print("errno: %d\n", errno);
	}
	d_print("SSL encountered an error: %d\n", err);
	return -IP_ERROR_OPENSSL;
}

/*
 * RFC5246 : "It is not required for the initiator of the close
 * to wait for the responding close_notify alert before closing
 * the read side of the connection."
 *
 * We will only call SSL_shutdown once before closing the socket.
 */
int ssl_close(SSL *ssl)
{
	int ret = SSL_shutdown(ssl);
	d_print("shutdown: ret=%d\n", ret);
	if (ret < 0) {
		handle_ssl_error(ssl, ret);
	}

	if (ssl != NULL) {
		SSL_free(ssl);
		ssl = NULL;
	}

	if (ssl_context != NULL) {
		SSL_CTX_free(ssl_context);
		ssl_context = NULL;
	}

	if (ret >= 0)
		return SSL_ERROR_NONE;
	return ret;
}

int https_write(struct connection *conn, const char *in_buf, int count)
{
	SSL *ssl = conn->ssl;
	int ret = SSL_write(ssl, in_buf, count); /* >0 on success, <=0 else */
	if (ret <= 0) {
		return handle_ssl_error(ssl, ret);
	}
	return ret;
}

int https_read(struct connection *conn, char *out_buf, int count)
{
	SSL *ssl = conn->ssl;
	if (conn->ssl == NULL)
		return -1;
	int ret = SSL_read(ssl, out_buf, count); /* returns >0 on success, <=0 else */
	if (ret <= 0) {
		ret = handle_ssl_error(ssl, ret);
		if (ret == -IP_ERROR_OPENSSL)
			return -1; /* https_read() should emulate socket_read() which returns -1 on errors */
		return ret;
	}
	return ret;
}