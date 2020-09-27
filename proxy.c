#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEFAULT_PORT 80

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-connection: close\r\n";

void doit(int client_fd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void write_request_headers(char *newreq, char *host, char *port);
void *thread_routine(void *vargp);

int main(int argc, char **argv)
{
    pthread_t tid;

    int listenfd;
    int  *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // Ignore SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    init_cache();

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        printf("Proxy server is listening !!!\n");

        connfd = Malloc(sizeof(int));
        clientlen = sizeof(clientaddr);
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread_routine, connfd);                                               //line:netp:tiny:doit                                          //line:netp:tiny:close
    }

    Close(listenfd);

    free_cache();

    return 0;
}

void *thread_routine(void *vargp)
{
    int connfd = *((int *)vargp);
    // Detach mode.
    Pthread_detach(pthread_self());
    // Free the heap malloc.
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * Handler: one HTTP request/response transaction
 */
// Input argument: client-socket-fd.
// rio_t is mainly used as arg for reading line(s) from socket.
void doit(int client_fd)
{
    // Boilerplate code...
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];

    /*store the request line arguments*/
    char hostname[MAXLINE], path[MAXLINE]; //path eg  /hub/index.html
    int port;

    /* Read request line and headers */
    rio_t from_client;
    Rio_readinitb(&from_client, client_fd);
    if (!Rio_readlineb(&from_client, buf, MAXLINE))
    {
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET"))
    {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Proxy Server does not implement this method");
        return;
    }

    // printf("Reading cache for %s\n", uri);

    // If cache hit & write to socket, return.
    if (read_cache(uri, client_fd))
    {
    	printf("\n\n !!! \n\n Read cache successful for %s\n", uri);
        return;
    }

    // Extract hostname/port/path from URI.
    parse_uri(uri, hostname, path, &port);
    char port_str[10];
    sprintf(port_str, "%d", port);

    // Socket for talking with dest server.
    int endserver_fd = Open_clientfd(hostname, port_str);
    if(endserver_fd < 0)
    {
        printf("Proxy connection failed\n");
        return;
    }
    rio_t to_endserver;
    Rio_readinitb(&to_endserver, endserver_fd);

    // New HTTP request bytes to be sent to dest/end server.
    char newreq[MAXLINE];

    //set up first line eg.GET /hub/index.html HTTP/1.0
    sprintf(newreq, "GET %s HTTP/1.0\r\n", path);

    // Populate headers
    // populate_request_headers(&from_client, newreq, hostname, port_str);
    write_request_headers(newreq, hostname, port_str);

    // Relay request to dest server.
    // printf("Proxy server forwarding request to dest server !!!\n");
    Rio_writen(endserver_fd, newreq, strlen(newreq));

    // Read response from dest-server and relay back to client socket.
    int n = 0;
    int data_len = 0;  // size of data being recevied from end server.
    char data[MAX_OBJECT_SIZE];
    while ((n = Rio_readlineb(&to_endserver, buf, MAXLINE)))
    {
        if (data_len <= MAX_OBJECT_SIZE)
        {
            memcpy(data + data_len, buf, n);
            data_len += n;
        }
        //printf("proxy received %d bytes,then send\n",n);
        Rio_writen(client_fd, buf, n);  //real server response to real client
    }

    if (data_len <= MAX_OBJECT_SIZE)
    {
        write_cache(uri, data, data_len);
        printf("\n\n ! \n\n Wrote cache done for %s\n", uri);
    }
    // printf("Proxy server forwarded response back to client !!!\n");
    Close(endserver_fd);
}

// Extract hostname/port/path from URI (helper).
void parse_uri(char *uri, char *hostname, char *path, int *port)
{
    *port = 80;
    //uri http://www.cmu.edu/hub/index.html
    char *pos1 = strstr(uri, "//");
    if (pos1 == NULL)
    {
        pos1 = uri;
    }
    else pos1 += 2;

    //printf("parse uri pos1 %s\n",pos1);//pos1 www.cmu.edu/hub/index.html

    char *pos2 = strstr(pos1, ":");
    /*pos1 www.cmu.edu:8080/hub/index.html, pos2 :8080/hub/index.html */
    if (pos2 != NULL)
    {
        *pos2 = '\0'; //pos1 www.cmu.edu/08080/hub/index.html
        strncpy(hostname, pos1, MAXLINE);
        sscanf(pos2 + 1, "%d%s", port, path); //pos2+1 8080/hub/index.html
        *pos2 = ':';
    }
    else
    {
        pos2 = strstr(pos1, "/"); //pos2 /hub/index.html
        if (pos2 == NULL)  /*pos1 www.cmu.edu*/
        {
            strncpy(hostname, pos1, MAXLINE);
            strcpy(path, "");
            return;
        }
        *pos2 = '\0';
        strncpy(hostname, pos1, MAXLINE);
        *pos2 = '/';
        strncpy(path, pos2, MAXLINE);
    }

}

// Populate HTTP headers (helper).
void write_request_headers(char *newreq, char *host, char *port)
{
    if (strlen(port) > 0)
    {
        sprintf(newreq, "%sHost: %s:%s\r\n", newreq, host, port);
    }
    else
    {
        sprintf(newreq, "%sHost: %s\r\n", newreq, host);
    }
    sprintf(newreq, "%s%s", newreq, user_agent);
    sprintf(newreq, "%s%s", newreq, connection);
    sprintf(newreq, "%s%s\r\n", newreq, proxy_connection);
    return;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
