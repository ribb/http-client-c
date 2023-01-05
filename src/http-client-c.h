/*
	http-client-c
	Copyright (C) 2012-2013  Swen Kooij

	This file is part of http-client-c.

    http-client-c is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    http-client-c is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with http-client-c. If not, see <http://www.gnu.org/licenses/>.

	Warning:
	This library does not tend to work that stable nor does it fully implent the
	standards described by IETF. For more information on the precise implentation of the
	Hyper Text Transfer Protocol:

	http://www.ietf.org/rfc/rfc2616.txt
*/

#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//#ifdef _WIN32
//	#include <winsock2.h>
//	#include <ws2tcpip.h>
//	#include <stdio.h>
//	#pragma comment(lib, "Ws2_32.lib")
//#elif _LINUX
//	#include <sys/socket.h>
//#elif __FreeBSD__
    #include <sys/socket.h>
	
    #include <netinet/in.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <unistd.h>
	
//#define OPENSSL
#if defined(OPENSSL)
	#include <openssl/crypto.h>
	#include <openssl/bio.h>
	#include <openssl/ssl.h>
	#include <openssl/err.h>
	#include <openssl/pem.h>
	#include <openssl/x509.h>
	#include <openssl/x509_vfy.h>
#endif
//#else
//	#error Platform not suppoted.
//#endif

#include <errno.h>
#include "stringx.h"
#include "urlparser.h"

/*
	Prototype functions
*/
struct http_response* http_req(char *http_headers, struct parsed_url *purl);
struct http_response* http_put(char *url, char *custom_headers);
struct http_response* http_get(char *url, char *custom_headers);
struct http_response* http_head(char *url, char *custom_headers);
struct http_response* http_post(char *url, char *custom_headers, char *post_data);


/*
	Represents an HTTP html response
*/
struct http_response
{
	struct parsed_url *request_uri;
	char *body;
	char *status_code;
	int status_code_int;
	char *status_text;
	char *request_headers;
	char *response_headers;
};

/*
	Handles redirect if needed for get requests
*/
struct http_response* handle_redirect_get(struct http_response* hresp, char* custom_headers)
{
	if(hresp->status_code_int > 300 && hresp->status_code_int < 399)
	{
		char *token = strtok(hresp->response_headers, "\r\n");
		while(token != NULL)
		{
			if(str_contains(token, "Location:"))
			{
				/* Extract url */
				char *location = str_replace("Location: ", "", token);
				return http_get(location, custom_headers);
			}
			token = strtok(NULL, "\r\n");
		}
	}
	else
	{
		/* We're not dealing with a redirect, just return the same structure */
		return hresp;
	}
}

/*
	Handles redirect if needed for head requests
*/
struct http_response* handle_redirect_head(struct http_response* hresp, char* custom_headers)
{
	if(hresp->status_code_int > 300 && hresp->status_code_int < 399)
	{
		char *token = strtok(hresp->response_headers, "\r\n");
		while(token != NULL)
		{
			if(str_contains(token, "Location:"))
			{
				/* Extract url */
				char *location = str_replace("Location: ", "", token);
				return http_head(location, custom_headers);
			}
			token = strtok(NULL, "\r\n");
		}
	}
	else
	{
		/* We're not dealing with a redirect, just return the same structure */
		return hresp;
	}
}

/*
	Handles redirect if needed for post requests
*/
struct http_response* handle_redirect_post(struct http_response* hresp, char* custom_headers, char *post_data)
{
	if(hresp->status_code_int > 300 && hresp->status_code_int < 399)
	{
		char *token = strtok(hresp->response_headers, "\r\n");
		while(token != NULL)
		{
			if(str_contains(token, "Location:"))
			{
				/* Extract url */
				char *location = str_replace("Location: ", "", token);
				return http_post(location, custom_headers, post_data);
			}
			token = strtok(NULL, "\r\n");
		}
	}
	else
	{
		/* We're not dealing with a redirect, just return the same structure */
		return hresp;
	}
}

/*
	Makes a HTTP request and returns the response
*/
struct http_response* http_req(char *http_headers, struct parsed_url *purl)
{
	
	SSL *ssl; 
	SSL_CTX *ctx; 
	SSL_METHOD *client_method; 
	X509 *server_cert;
	
	int sd,err;	
	char *str;
	int ishttps;
	
	/* Parse url */
	if(purl == NULL)
	{
		printf("Unable to parse url");
		return NULL;
	}

	/* Declare variable */
	int sock;
	int tmpres;
	char buf[BUFSIZ+1];
	struct sockaddr_in *remote;

	/* Allocate memeory for htmlcontent */
	struct http_response *hresp = (struct http_response*)malloc(sizeof(struct http_response));
	if(hresp == NULL)
	{
		printf("Unable to allocate memory for htmlcontent.");
		return NULL;
	}
	hresp->body = NULL;
	hresp->request_headers = NULL;
	hresp->response_headers = NULL;
	hresp->status_code = NULL;
	hresp->status_text = NULL;

	/* Create TCP socket */
	if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
	    printf("Can't create TCP socket");
		return NULL;
	}

	/* Set remote->sin_addr.s_addr */
	remote = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in *));
	remote->sin_family = AF_INET;
  	tmpres = inet_pton(AF_INET, purl->ip, (void *)(&(remote->sin_addr.s_addr)));
  	if( tmpres < 0)
  	{
		free(remote);
    	printf("Can't set remote->sin_addr.s_addr");
    	return NULL;
  	}
	else if(tmpres == 0)
  	{
		free(remote);
		printf("Not a valid IP");
    	return NULL;
  	}
	remote->sin_port = htons(atoi(purl->port));
	
	if(atoi(purl->port) == 443)
		ishttps = 1;
	else
		ishttps = 0;
	
	if(ishttps)
	{
		/* init ssl */
		printf("(1) SSL context initialized\n\n"); 
		SSLeay_add_ssl_algorithms();
		client_method = SSLv23_client_method(); 
		SSL_load_error_strings(); 
		ctx = SSL_CTX_new(client_method);
	}
	/* Connect */
	if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0)
	{
		free(remote);
	    printf("Could not connect");
		return NULL;
	}
	if(ishttps)
	{
		printf("(3) TCP connection open to host '%s', port %d\n\n", purl->host, remote->sin_port); 
		ssl = SSL_new(ctx);
		SSL_set_fd(ssl, sock); /* attach SSL stack to socket */
		
		// SNI support
		SSL_set_tlsext_host_name(ssl,purl->host);
		
		err = SSL_connect(ssl); /* initiate SSL handshake */ 
		printf("(4) SSL endpoint created & handshake completed\n\n"); 
		printf("(5) SSL connected with cipher: %s\n\n", SSL_get_cipher(ssl)); 
		server_cert = SSL_get_peer_certificate(ssl); 
		printf("(6) server's certificate was received:\n\n"); 
		str = X509_NAME_oneline(X509_get_subject_name(server_cert), 0, 0); 
		printf(" subject: %s\n", str); 
		str = X509_NAME_oneline(X509_get_issuer_name(server_cert), 0, 0); 
		printf(" issuer: %s\n\n", str); 
		X509_free(server_cert);
	}
	/* Send headers to server */
	int sent = 0;
	while(sent < strlen(http_headers))
	{
		if(ishttps)
			tmpres = SSL_write(ssl, http_headers+sent, strlen(http_headers)-sent);
		else
			tmpres = send(sock, http_headers+sent, strlen(http_headers)-sent, 0);
		if(tmpres == -1)
		{
			printf("Can't send headers");
			return NULL;
		}
		sent += tmpres;
	 }
	
	shutdown (sd, 1); /* send EOF to server */
	if(ishttps)
		printf("(7) sent HTTP request over encrypted channel:\n\n%s\n",http_headers);	

	/* Recieve into response*/
	char *response = (char*)malloc(0);
	char BUF[BUFSIZ];
	size_t recived_len = 0;

//	while((recived_len = recv(sock, BUF, BUFSIZ-1, 0)) > 0)
	if(ishttps)
	{
		while((recived_len = SSL_read(ssl, BUF, BUFSIZ-1)) > 0)
		{
			BUF[recived_len] = '\0';
			response = (char*)realloc(response, strlen(response) + strlen(BUF) + 1);
			sprintf(response, "%s%s", response, BUF);
		}
	}
	else {
		while((recived_len = recv(sock, BUF, BUFSIZ-1, 0)) > 0)
		{
			BUF[recived_len] = '\0';
			response = (char*)realloc(response, strlen(response) + strlen(BUF) + 1);
			sprintf(response, "%s%s", response, BUF);			
		}
	}
	
	if (recived_len < 0)
    {
		free(remote);
		free(response);
		free(http_headers);
		#ifdef _WIN32
			closesocket(sock);
		#else
			close(sock);
		#endif
        printf("Unabel to recieve");
		return NULL;
    }

	/* Reallocate response */
	response = (char*)realloc(response, strlen(response) + 1);

	if(ishttps)
		printf ("(8) HTTP response:\n\n%s\n",response); 

	/* Close socket */
	#ifdef _WIN32
		closesocket(sock);
	#else
	if(ishttps)
	{
		SSL_shutdown(ssl); 
		close(sock);
		SSL_free (ssl); 
		SSL_CTX_free (ctx);
	}
	else {
		close(sock);
	}
	#endif

	/* Parse status code and text */
	char *status = get_until(response, "\r\n");
	char *status_line = str_replace("HTTP/1.1 ", "", status);
	
	free(status);
	
	status = strndup(status_line, 4);
	char *status_code = str_replace(" ", "", status);
	
	free(status);
	
	status = str_replace(status_code, "", status_line);
	char *status_text = str_replace(" ", "", status);
	
	free(status);
	free(status_line);
	
	hresp->status_code = status_code;
	hresp->status_code_int = atoi(status_code);
	hresp->status_text = status_text;

	/* Parse response headers */
	char *headers = get_until(response, "\r\n\r\n");
	hresp->response_headers = headers;

	/* Assign request headers */
	hresp->request_headers = http_headers;

	/* Assign request url */
	hresp->request_uri = purl;

	/* Parse body */
	char *body = strstr(response, "\r\n\r\n");
	body = str_replace("\r\n\r\n", "", body);
	hresp->body = body;
	
	free(remote);
	free(response);
	

	/* Return response */
	return hresp;
}


/*
Makes a HTTP PUT request to the given url
*/
struct http_response* http_put(char *url, char *custom_headers)
{
	/* Parse url */
	struct parsed_url *purl = parse_url(url);
	if(purl == NULL)
		{
			printf("Unable to parse url");
			return NULL;
		}
	
	/* Declare variable */
	char *http_headers = (char*)malloc(1024);
	memset(http_headers, 0, 1024);
	
	/* Build query/headers */
	if(purl->path != NULL)
		{
			if(purl->query != NULL)
				{
					sprintf(http_headers, "PUT /%s?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->query, purl->host);
				}
			else
				{
					sprintf(http_headers, "PUT /%s HTTP/1.1\r\nHost:%s\r\napplication/javascript\r\nContent-length: %d\r\nConnection:close\r\n\r\n%s", \
						purl->path, purl->host, strlen(custom_headers),custom_headers);
				}
		}
	else
		{
			if(purl->query != NULL)
				{
					sprintf(http_headers, "PUT /?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->query, purl->host);
				}
			else
				{
					sprintf(http_headers, "PUT / HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->host);
				}
		}
	
	/* Handle authorisation if needed */
	if(purl->username != NULL)
		{
			/* Format username:password pair */
			char *upwd = (char*)malloc(1024);
			sprintf(upwd, "%s:%s", purl->username, purl->password);
			upwd = (char*)realloc(upwd, strlen(upwd) + 1);
			
			/* Base64 encode */
			char *base64 = base64_encode(upwd);
			
			/* Form header */
			char *auth_header = (char*)malloc(1024);
			sprintf(auth_header, "Authorization: Basic %s\r\n", base64);
			auth_header = (char*)realloc(auth_header, strlen(auth_header) + 1);
			
			/* Add to header */
			http_headers = (char*)realloc(http_headers, strlen(http_headers) + strlen(auth_header) + 2);
			sprintf(http_headers, "%s%s", http_headers, auth_header);
		}

	/* Add custom headers, and close */
	if(custom_headers != NULL)
		{
			sprintf(http_headers, "%s%s\r\n", http_headers, custom_headers);
		}
	else
		{
			sprintf(http_headers, "%s\r\n", http_headers);
		}

	http_headers = (char*)realloc(http_headers, strlen(http_headers) + 1);

	/* Make request and return response */
	struct http_response *hresp = http_req(http_headers, purl);
	
	/* Handle redirect */
	return handle_redirect_get(hresp, custom_headers);

}

/*
	Makes a HTTP GET request to the given url
*/
struct http_response* http_get(char *url, char *custom_headers)
{
	/* Parse url */
	struct parsed_url *purl = parse_url(url);
	if(purl == NULL)
	{
		printf("Unable to parse url");
		return NULL;
	}

	/* Declare variable */
	char *http_headers = (char*)malloc(1024);

	/* Build query/headers */
	if(purl->path != NULL)
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "GET /%s?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "GET /%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->host);
		}
	}
	else
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "GET /?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "GET / HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->host);
		}
	}

	/* Handle authorisation if needed */
	if(purl->username != NULL)
	{
		/* Format username:password pair */
		char *upwd = (char*)malloc(1024);
		sprintf(upwd, "%s:%s", purl->username, purl->password);
		upwd = (char*)realloc(upwd, strlen(upwd) + 1);

		/* Base64 encode */
		char *base64 = base64_encode(upwd);

		/* Form header */
		char *auth_header = (char*)malloc(1024);
		sprintf(auth_header, "Authorization: Basic %s\r\n", base64);
		auth_header = (char*)realloc(auth_header, strlen(auth_header) + 1);

		/* Add to header */
		http_headers = (char*)realloc(http_headers, strlen(http_headers) + strlen(auth_header) + 2);
		sprintf(http_headers, "%s%s", http_headers, auth_header);
	}

	/* Add custom headers, and close */
	if(custom_headers != NULL)
	{
		sprintf(http_headers, "%s%s\r\n", http_headers, custom_headers);
	}
	else
	{
		sprintf(http_headers, "%s\r\n", http_headers);
	}
	http_headers = (char*)realloc(http_headers, strlen(http_headers) + 1);

	/* Make request and return response */
	struct http_response *hresp = http_req(http_headers, purl);

	/* Handle redirect */
	return handle_redirect_get(hresp, custom_headers);
}

/*
	Makes a HTTP POST request to the given url
*/
struct http_response* http_post(char *url, char *custom_headers, char *post_data)
{
	/* Parse url */
	struct parsed_url *purl = parse_url(url);
	if(purl == NULL)
	{
		printf("Unable to parse url");
		return NULL;
	}

	/* Declare variable */
	char *http_headers = (char*)malloc(1024);

	/* Build query/headers */
	if(purl->path != NULL)
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "POST /%s?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\nContent-Length:%zu\r\nContent-Type:application/x-www-form-urlencoded\r\n", purl->path, purl->query, purl->host, strlen(post_data));
		}
		else
		{
			sprintf(http_headers, "POST /%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\nContent-Length:%zu\r\nContent-Type:application/x-www-form-urlencoded\r\n", purl->path, purl->host, strlen(post_data));
		}
	}
	else
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "POST /?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\nContent-Length:%zu\r\nContent-Type:application/x-www-form-urlencoded\r\n", purl->query, purl->host, strlen(post_data));
		}
		else
		{
			sprintf(http_headers, "POST / HTTP/1.1\r\nHost:%s\r\nConnection:close\r\nContent-Length:%zu\r\nContent-Type:application/x-www-form-urlencoded\r\n", purl->host, strlen(post_data));
		}
	}

	/* Handle authorisation if needed */
	if(purl->username != NULL)
	{
		/* Format username:password pair */
		char *upwd = (char*)malloc(1024);
		sprintf(upwd, "%s:%s", purl->username, purl->password);
		upwd = (char*)realloc(upwd, strlen(upwd) + 1);

		/* Base64 encode */
		char *base64 = base64_encode(upwd);

		/* Form header */
		char *auth_header = (char*)malloc(1024);
		sprintf(auth_header, "Authorization: Basic %s\r\n", base64);
		auth_header = (char*)realloc(auth_header, strlen(auth_header) + 1);

		/* Add to header */
		http_headers = (char*)realloc(http_headers, strlen(http_headers) + strlen(auth_header) + 2);
		sprintf(http_headers, "%s%s", http_headers, auth_header);
	}

	if(custom_headers != NULL)
	{
		sprintf(http_headers, "%s%s\r\n", http_headers, custom_headers);
		sprintf(http_headers, "%s\r\n%s", http_headers, post_data);
	}
	else
	{
		sprintf(http_headers, "%s\r\n%s", http_headers, post_data);
	}
	http_headers = (char*)realloc(http_headers, strlen(http_headers) + 1);

	/* Make request and return response */
	struct http_response *hresp = http_req(http_headers, purl);

	/* Handle redirect */
	return handle_redirect_post(hresp, custom_headers, post_data);
}

/*
	Makes a HTTP HEAD request to the given url
*/
struct http_response* http_head(char *url, char *custom_headers)
{
	/* Parse url */
	struct parsed_url *purl = parse_url(url);
	if(purl == NULL)
	{
		printf("Unable to parse url");
		return NULL;
	}

	/* Declare variable */
	char *http_headers = (char*)malloc(1024);

	/* Build query/headers */
	if(purl->path != NULL)
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "HEAD /%s?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "HEAD /%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->host);
		}
	}
	else
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "HEAD/?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "HEAD / HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->host);
		}
	}

	/* Handle authorisation if needed */
	if(purl->username != NULL)
	{
		/* Format username:password pair */
		char *upwd = (char*)malloc(1024);
		sprintf(upwd, "%s:%s", purl->username, purl->password);
		upwd = (char*)realloc(upwd, strlen(upwd) + 1);

		/* Base64 encode */
		char *base64 = base64_encode(upwd);

		/* Form header */
		char *auth_header = (char*)malloc(1024);
		sprintf(auth_header, "Authorization: Basic %s\r\n", base64);
		auth_header = (char*)realloc(auth_header, strlen(auth_header) + 1);

		/* Add to header */
		http_headers = (char*)realloc(http_headers, strlen(http_headers) + strlen(auth_header) + 2);
		sprintf(http_headers, "%s%s", http_headers, auth_header);
	}

	if(custom_headers != NULL)
	{
		sprintf(http_headers, "%s%s\r\n", http_headers, custom_headers);
	}
	else
	{
		sprintf(http_headers, "%s\r\n", http_headers);
	}
	http_headers = (char*)realloc(http_headers, strlen(http_headers) + 1);

	/* Make request and return response */
	struct http_response *hresp = http_req(http_headers, purl);

	/* Handle redirect */
	return handle_redirect_head(hresp, custom_headers);
}

/*
	Do HTTP OPTIONs requests
*/
struct http_response* http_options(char *url)
{
	/* Parse url */
	struct parsed_url *purl = parse_url(url);
	if(purl == NULL)
	{
		printf("Unable to parse url");
		return NULL;
	}

	/* Declare variable */
	char *http_headers = (char*)malloc(1024);

	/* Build query/headers */
	if(purl->path != NULL)
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "OPTIONS /%s?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "OPTIONS /%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->path, purl->host);
		}
	}
	else
	{
		if(purl->query != NULL)
		{
			sprintf(http_headers, "OPTIONS/?%s HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->query, purl->host);
		}
		else
		{
			sprintf(http_headers, "OPTIONS / HTTP/1.1\r\nHost:%s\r\nConnection:close\r\n", purl->host);
		}
	}

	/* Handle authorisation if needed */
	if(purl->username != NULL)
	{
		/* Format username:password pair */
		char *upwd = (char*)malloc(1024);
		sprintf(upwd, "%s:%s", purl->username, purl->password);
		upwd = (char*)realloc(upwd, strlen(upwd) + 1);

		/* Base64 encode */
		char *base64 = base64_encode(upwd);

		/* Form header */
		char *auth_header = (char*)malloc(1024);
		sprintf(auth_header, "Authorization: Basic %s\r\n", base64);
		auth_header = (char*)realloc(auth_header, strlen(auth_header) + 1);

		/* Add to header */
		http_headers = (char*)realloc(http_headers, strlen(http_headers) + strlen(auth_header) + 2);
		sprintf(http_headers, "%s%s", http_headers, auth_header);
	}

	/* Build headers */
	sprintf(http_headers, "%s\r\n", http_headers);
	http_headers = (char*)realloc(http_headers, strlen(http_headers) + 1);

	/* Make request and return response */
	struct http_response *hresp = http_req(http_headers, purl);

	/* Handle redirect */
	return hresp;
}

/*
	Free memory of http_response
*/
void http_response_free(struct http_response *hresp)
{
	if(hresp != NULL)
	{
		if(hresp->request_uri != NULL) parsed_url_free(hresp->request_uri);
		if(hresp->body != NULL) free(hresp->body);
		if(hresp->status_code != NULL) free(hresp->status_code);
		if(hresp->status_text != NULL) free(hresp->status_text);
		if(hresp->request_headers != NULL) free(hresp->request_headers);
		if(hresp->response_headers != NULL) free(hresp->response_headers);
		free(hresp);
	}
}
