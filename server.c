#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <my_global.h>
#include <mysql.h>
#include "activity.h"

#define ERR_EXIT(a) { perror(a); exit(1); }
#define BUF_LEN 1024


typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    // you don't need to change this.

    char* name;  // used by handle_read to know if the header is read or not.
    char* nickname;  // used by handle_read to know if the header is read or not.
    char* password;

    int online;
    int file;
} onlineuser;

typedef  struct {
    int activity;

    char* note;
    int note_len;
    
    char* content;
    int content_len;

    // for case of transfering file
    int size;
    char* filename;
} request;

server svr;  // server
onlineuser* onlineuserP = NULL;  // point to a list of onlineusers
int maxfd;  // size of open file descriptor table, size of onlineuser list
int thread_num;

const char* accept_header = "ACCEPT\n";
const char* reject_header = "REJECT\n";

static int init_socket(unsigned short port);
// initailize a server, exit for error

static void init_onlineuser(onlineuser* userP);
// initailize a onlineuser instance

static void free_onlineuser(onlineuser* userP);
// free resources used by a onlineuser instance

static void init_request(request* userP);

static void free_request(request* userP);

static void send_responds(int act, char* note, char* content, int client_fd);

static void handle_request(int conn_fd, request* reqP);

static void pass_talk(onlineuser* user, request* request);

static void subString(char* str, char* cpy, int start, int end);

static void ask_permission(onlineuser* user, request* req, int self); 

static void handle_file(onlineuser* user, request* req);

static void send_file(int fd, request* req);

static void check_users(onlineuser* user);

static void* receive_file(void *arg);

static void* pass_file(void *arg);

static void* pass_user_status(void *arg);

char* my_name = "abby";

int main(int argc, char** argv) {
    int i, ret, socket_fd;

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    // Initialize server
    socket_fd = init_socket((unsigned short) atoi(argv[1]));

    //initialize sql connection
    MYSQL *con = mysql_init(NULL);
    if (con == NULL) {
        fprintf(stderr, "%s\n", mysql_error(con));
        exit(1);
    }  
    if (mysql_real_connect(con, "localhost", "root", "123456", "chatroom", 0, NULL, 0) == NULL) {
        finish_with_error(con);
        perror("sql connect failed");
        exit(1);
    }    

    //Get file descripter table size and initize onlineuser table
    thread_num = 0;
    maxfd = getdtablesize();
    onlineuserP = (onlineuser*) malloc(sizeof(onlineuser) * maxfd);
    if (onlineuserP == NULL) {
        ERR_EXIT("out of memory allocating all onlineusers")
    }
    for (i = 0; i < maxfd; i++) {
        init_onlineuser(&onlineuserP[i]);
    }
    onlineuserP[socket_fd].conn_fd = socket_fd;
    strcpy(onlineuserP[socket_fd].host, svr.hostname);

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    // fprintf(stderr, "\nstarting on %d\n", socket_fd);

    fd_set read_set;
    struct timeval tvptr;

    tvptr.tv_sec = 2;
    tvptr.tv_usec = 0;

    // struct sockaddr_in cliaddr;  // used by accept()
    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    //start to select
    while(1) {
        clilen = sizeof(cliaddr);
        
        FD_ZERO(&read_set);
        for (i = 0; i < maxfd; i++) {
            if (onlineuserP[i].online == 1 && onlineuserP[i].file == -1) {
                FD_SET(onlineuserP[i].conn_fd, &read_set);
            }
        }
        FD_SET(svr.listen_fd, &read_set);

        int fd_total = select(maxfd, &read_set, NULL, NULL, NULL);
        for (i = 0; i < maxfd && fd_total > 0; i++) {
            if (FD_ISSET(i, &read_set)) {
                fd_total--;
                if (i == socket_fd) {
                    int conn_fd = accept(socket_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
                    if (conn_fd < 0) {
                        if (errno == EINTR || errno == EAGAIN) continue;  // try again
                        if (errno == ENFILE) {
                            (void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                            continue;
                        }
                        ERR_EXIT("failed to accept connection")
                    }
                    onlineuserP[conn_fd].conn_fd = conn_fd;
                    strcpy(onlineuserP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
                    fprintf(stderr, "getting a new onlineuser... fd %d from %s\n", conn_fd, onlineuserP[conn_fd].host);
                    onlineuserP[conn_fd].online = 1;
                    continue;
                }
                // get user request
                request* reqP = malloc(sizeof(request));
                init_request(reqP);
                handle_request(i, reqP);
fprintf(stderr, "%d, %s, %s\n", reqP->activity, reqP->note, reqP->content);
fprintf(stderr, "%d, %d, %s, %s\n", reqP->activity, reqP->size, reqP->filename, reqP->content);
                
                switch(reqP->activity) {
                        char sql[1024];
                        sql[0] = 0;
                    case SIGN_UP:
                        strcat(sql, "SELECT name FROM users WHERE name = ");
                        strcat(sql, reqP->note);
                        if (mysql_query(con, sql)) {
                            finish_with_error(con);
                            perror("sql query failed");
                        }
                        MYSQL_RES *result = mysql_store_result(con);
                        if (result != NULL) {
                            send_responds(REJECT, "0", "0", i);
                            mysql_free_result(result);
                        } else {
                            send_responds(ACCEPT, "0", "0", i);
                            onlineuserP[i].name = malloc(strlen(reqP->note) + 1);
                            strcpy(onlineuserP[i].name, strlen(reqP->note));
                            onlineuserP[i].password = malloc(strlen(reqP->content) + 1);
                            strcpy(onlineuserP[i].password, strlen(reqP->content));
                        }
                        break;
                    case SET_NICK:
                        onlineuserP[i].nickname = malloc(strlen(reqP->content) + 1);
                        strcpy(onlineuserP[i].nickname, strlen(reqP->content));

                        strcat(sql, "INSERT INTO users VALUES (");
                        strcat(sql, onlineuser[i].name);
                        strcat(sql, ", ");
                        strcat(sql, onlineuser[i].nickname);
                        strcat(sql, ", ");
                        strcat(sql, onlineuser[i].password);
                        strcat(sql, ")");

                        if (mysql_query(con, sql)) {
                            finish_with_error(con);
                            perror("sql query failed");
                        }

                        break;
                    case SIGN_IN:
                        strcat(sql, "SELECT name FROM users WHERE name = ");
                        strcat(sql, reqP->note);
                        if (mysql_query(con, sql)) {
                            finish_with_error(con);
                            perror("sql query failed");
                        }
                        MYSQL_RES *result = mysql_store_result(con);
                        if (result == NULL) {
                            send_responds(REJECT, "0", "0", i);
                        } else {
                            int num_fields = mysql_num_fields(result);
                            MYSQL_ROW row = mysql_fetch_row(result);
                            if (strcmp(row[2], reqP->content) == 0) {
                                send_responds(ACCEPT, "0", "0", i);

                                onlineuserP[i].name = malloc(strlen(reqP->note) + 1);
                                strcpy(onlineuserP[i].name, strlen(reqP->note));
                                onlineuserP[i].nickname = malloc(strlen(row[1]) + 1);
                                strcpy(onlineuserP[i].nickname, strlen(row[1]));
                            } else {
                                send_responds(REJECT, "0", "0", i);
                            }
                            mysql_free_result(result);
                        }
                        break;
                    case TALK:
                        pass_talk(&(onlineuserP[i]), reqP);
                        break;
                    case SEND_FILE:
                        onlineuserP[i].file = 1;
                        handle_file(&(onlineuserP[i]), reqP);
                        break;
                    case RECV_FILE:
                        onlineuserP[i].file = 1;
                        send_file(i, reqP);
                        break;
                    case CHECK_USERS:
                        check_users(int fd);
                        break;
                    case QUIT:
                        free_onlineuser(&(onlineuserP[i]));
                        close(i);
                        break;
                }
                free_request(reqP);
            } //FD_ISSET
        } //select
    } //while

}

typedef  struct {
    int user_id;
    int file_size;
    char filename[128];
} file_trans;

static void pass_talk(onlineuser* user, request* req) {
    int i;
    for (i = 0; i < maxfd; i++) {
        if (onlineuserP[i].online == 1) {
            send_responds(OTHERS_TALK, user->nickname, req->content, i);
        }
    }
}

static void ask_permission(onlineuser* user, request* req, int self) {
    int i, size = req->size, muti = 1;
    char note[128];

    while(size / muti >= 10)
        muti *= 10;
    for (i = 0;; i++) {
        note[i] = '0' + size/muti;
        if (muti == 1) {
            note[++i] = '\0';
            break;
        }
        size %= muti;
        muti /= 10;
    }
    strcat(note, ";");
    strcat(note, req->filename);

    for (i = 0; i < maxfd; i++) {
        if (i == self)
            continue;
        if (onlineuserP[i].online == 1 && onlineuserP[i].file == -1) {
            send_responds(ASK_PERMISSION, note, user->nickname, i);
        }
    }
}

static void handle_file(onlineuser* user, request* req) {
    //check user
    int i, self;
    for (i = 0; i <maxfd; i++) {
        if (onlineuserP[i].online == 1 && onlineuserP[i].file == -1) {
            if (i == user->conn_fd)
                continue;
            if (strcmp(onlineuserP[i].name, req->content) == 0){
                user->name = onlineuserP[i].name;
                user->nickname = onlineuserP[i].nickname;
                self = i;
                break;
            }
        }
    }
    ask_permission(user, req, self);
    //fork/thread to receive file
    file_trans* ft = malloc(sizeof(file_trans));
    ft->user_id = user->conn_fd;
    ft->file_size = req->size;
    strcpy(ft->filename, req->filename);
    pthread_t pthread;
    pthread_create(&pthread, NULL, receive_file, (void*) ft);
}

static void* receive_file(void *arg) {
    int id = ((file_trans*)arg)->user_id;
    int file_size = ((file_trans*)arg)->file_size;
    char* filename = ((file_trans*)arg)->filename;

    int ret, size;
    char buf[BUF_LEN];
    int fd = open(filename, O_WRONLY|O_TRUNC|O_CREAT);
    while((ret = read(id, buf, BUF_LEN)) > 0) {
        write(fd, buf, ret);
        buf[ret] = '\0';
        size += ret;
        if (size >= file_size)
            break;
    }
    close(fd);
    close(id);
    free_onlineuser(&(onlineuserP[id]));
    free(arg);

    return NULL;
}

static void send_file(int fd, request* req) {
    file_trans* ft = malloc(sizeof(file_trans));
    ft->user_id = fd;
    strcpy(ft->filename, req->content);

    pthread_t pthread;
    pthread_create(&pthread, NULL, pass_file, (void*) ft);
}

static void* pass_file(void *arg) {
    int id = ((file_trans*)arg)->user_id;
    char* filename = ((file_trans*)arg)->filename;
    
    int ret, size;
    char buf[BUF_LEN];
    int fd = open(filename, O_RDONLY);
    while((ret = read(fd, buf, BUF_LEN)) > 0) {
        buf[ret] = 0;
        write(id, buf, ret);
    }
    close(fd);
    close(id);
    free_onlineuser(&(onlineuserP[id]));
    free(arg);
    return NULL;
}

static void check_users(int fd) {
    pthread_t pthread;
    pthread_create(&pthread, NULL, pass_file, (void*) fd);
}

static void* pass_user_status(void *arg) {
    int fd = (int)arg;
    if (mysql_query(con, "SELECT name FROM users")) {
        inish_with_error(con);
        perror("sql query failed");
    }
    MYSQL_RES *result = mysql_store_result(con);
    if (result == NULL) {
        perror("query user failed");
        return NULL;        
    } else {
        int num_fields = mysql_num_fields(result);
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result))) {
            int u, online = 0;
            char status[128];
            status[0] = 0;
            strcpy(status, row[0]);
            for(u = 0; u < maxfd; u++) {
                if (onlineuserP[u].online < 0 || onlineuserP[u].name == NULL) 
                    continue;
                if (strcmp(row[0], onlineuserP[u].name) == 0) {
                    online = 1;
                    strcat(status, ": online");
                    break;
                }
            }
            if (online == 0) {
                strcat(status, ": offline");
            }
            send_responds(USER_STATUS, "0", status, i);
        }
        send_responds(USER_STATUS, "END", "0", i);
        mysql_free_result(result);
    }
    return NULL;
}

static int init_socket(unsigned short port) {
    struct sockaddr_in serv_addr;
    int fd;

    if((fd = socket(AF_INET, SOCK_STREAM , 0)) < 0) {
        ERR_EXIT("failed to open socket")
    }

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;
    svr.listen_fd = fd;

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if(bind(fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr_in)) < 0) {    
        ERR_EXIT("failed to bind socket")
    }

    if (listen(fd, 1024) < 0) {
        ERR_EXIT("failed to listen")
    }
    return fd;
}

static void handle_request(int conn_fd, request* reqP) {
    // Read in request from client
    int r;
    char buf[BUF_LEN];
    if((r = read(conn_fd, buf, sizeof(buf) - 1)) < 0)
        ERR_EXIT("failed to read");
    buf[r] = '\0';

    int i;
    int j = 0, count = 0;
    char* tmp = malloc(BUF_LEN);
    for(i = 0; i < r; i++) {
        if (buf[i] == ',') {
            subString(tmp, buf, j, i);
            j = i + 1;
            if (count == 0) { // activity
                reqP->activity = atoi(tmp);
                count++;
            } else if (count == 1) { // note
                if (reqP->activity == SEND_FILE) {
                    int k, l = 0, flag = 0;
                    char file_size[128];
                    for (k = 0; k < i; k++) {
                        if (flag == 1) {
                            (reqP->filename)[l++] = tmp[k];
                        } else if (tmp[k] == ';') {
                            flag = 1;
                            file_size[l] = '\0';
                            reqP->size = atoi(file_size);
                            reqP->filename = malloc(strlen(tmp));
                            l = 0;
                        } else {
                            file_size[l++] = tmp[k];
                        }
                    }
                    (reqP->filename)[l] = '\0';
                } else {
                    reqP->note = malloc(strlen(tmp));
                    strcpy(reqP->note, tmp);
                }
                break;
            }
        }
    }
    subString(tmp, buf, j, r);
    reqP->content = tmp;
}

static void send_responds(int act, char* note, char* content, int client_fd) {
    fprintf(stderr, "%d, %s, %s, %d\n", act, note, content, client_fd);
    int i;
    char msg[BUF_LEN];

    if (act/10 > 1) {
        msg[0] = '0' + act/10;
        msg[1] = '0' + act%10;
        msg[2] = '\0';

    } else {
        msg[0] = '0' + act;
        msg[1] = '\0';
    }

    strcat(msg, ",");
    strcat(msg, note);
    strcat(msg, ",");
    strcat(msg, content);

    write(client_fd, msg, strlen(msg));
}

static void subString(char* str, char* cpy, int start, int end) {
    int i, j = 0;
    for (i = start; i < end; i++)
        str[j++] = cpy[i];
    str[j] = '\0';
}

static void init_onlineuser(onlineuser* userP) {
    userP->conn_fd = -1;
    userP->buf_len = 0;
    userP->online = -1;
    userP->file = -1;
}

static void free_onlineuser(onlineuser* userP) {
    if (userP->name != NULL) {
        free(userP->name);
        userP->name = NULL;
    }
    if (userP->nickname != NULL) {
        free(userP->nickname);
        userP->nickname = NULL;
    }
    if (userP->password != NULL) {
        free(userP->password);
        userP->password = NULL;     
    }
    init_onlineuser(userP);
}

static void init_request(request* reqP) {
    reqP->note = NULL;
    reqP->content = NULL;
    reqP->filename = NULL;
}

static void free_request(request* reqP) {
    if (reqP->note != NULL) {
        free(reqP->note);
        reqP->note = NULL;
    }
    if (reqP->content != NULL) {
        free(reqP->content);
        reqP->content = NULL;
    }
    if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }
}
