#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "activity.h"

#define ERR_EXIT(a) { perror(a); exit(1); }
#define BUF_LEN 1024

typedef  struct {
    int activity;

    char* note;
    int note_len;
    
    char* content;
    int content_len;

    // for case of transfering file
    int size;
    char* filename;
    char* owner;
} response;

typedef struct {
    int socket_fd;  // fd to talk with client

    char* name;  // used by handle_read to know if the header is read or not.
    char* nickname;  // used by handle_read to know if the header is read or not.

    int login;
} user;

static int init_connect(char* ip, unsigned short port);

static void init_user(user* userP);
// initailize a user instance

static void free_user(user* userP);
// free resources used by a user instance

static void free_responds(response* respP);

static void send_request(int act, char* note, char* content, int fd);

static void handle_response(int conn_fd, response* respP);

static void subString(char* str, char* cpy, int start, int end);

static void print_talk(char* name, char* msg);

static void send_file(char* path);

static void* send_file_thread(void* arg);

static void recieve_file(response* resp);

static void* recieve_file_thread(void* arg);

user* self;

char** argv_g;

int main(int argc, char** argv) {
    int i, ret;
    int socket_fd;
    int checking_user = 0;

    // Parse args.
    if (argc != 3) {
        fprintf(stderr, "usage: %s [ip] [port]\n", argv[0]);
        exit(1);
    }

    argv_g = argv;

    // Initialize socket
    if ((socket_fd = init_connect(argv[1], (unsigned short) atoi(argv[2]))) < 0) 
        exit(1);

    self = malloc(sizeof(user));
    init_user(self);
    self->socket_fd = socket_fd;
    // sign in(up) for loop


    char name[128];
    char nickname[128];
    char password[128];
    fprintf(stderr, "username(new for sign up): ");
    scanf("%s", name);
   	if(strcmp(name, "new") == 0) {
    	fprintf(stderr, "sign up\nusername: ");
    	scanf("%s", name);
   		fprintf(stderr, "password: ");
    	scanf("%s", password);
    	while(1) {
    		send_request(SIGN_UP, name, password);

	    	response* respP = malloc(sizeof(response));
    		handle_response(self->socket_fd, respP);

    		if (respP->activity == ACCEPT) {
   				fprintf(stderr, "nickname: ");
    			scanf("%s", nickname);
    			send_request(SET_NICK, "0", nickname);
    			self->name = malloc(strlen(name) + 1);
    			self->nickname = malloc(strlen(nickname) + 1);
    			strcpy(self->name, name);
    			strcpy(self->nickname, nickname);
    			break;
    		}
    		fprintf(stderr, "the username has been used\n");
    		fprintf(stderr, "username: ");
    		scanf("%s", name);
   			fprintf(stderr, "password: ");
    		scanf("%s", password);
    	}
   	} else {
		fprintf(stderr, "password: ");
    	scanf("%s", password);
   		
    	while(1) {
    		send_request(SIGN_UP, name, password);

	    	response* respP = malloc(sizeof(response));
    		handle_response(self->socket_fd, respP);

    		if (respP->activity == ACCEPT) {
   				fprintf(stderr, "success!!\n");
    			self->name = malloc(strlen(name) + 1);
    			self->nickname = malloc(strlen(nickname) + 1);
    			strcpy(self->name, name);
    			strcpy(self->nickname, nickname);
    			break;
    		}
    		fprintf(stderr, "username: ");
    		scanf("%s", name);
   			fprintf(stderr, "password: ");
    		scanf("%s", password);
    	}
    }

    fd_set read_set;
    //start chat room
       while(1) {
        
        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        FD_SET(0, &read_set);

        int fd_total = select(10, &read_set, NULL, NULL, NULL);
        for (i = 0; i < 10 && fd_total > 0; i++) {
            if (FD_ISSET(i, &read_set)) {
                fd_total--;
                if (i == 0) {
                	char buf[BUF_LEN];
                	if (checking_user == 0) {
	    				fgets(buf, BUF_LEN - 1, stdin);
	    				// remove '\n'
	    				buf[strlen(buf) - 1] = '\0';

	    				if (buf[0] == '/') { //commands
	    					if (strcmp(buf, "/help") == 0) {
	    						fprintf(stderr, "[/help]    for commands\n[/users]   for users' status\n[/file]    for upload and transfer file\n[/quit]    for quit the chatroom\n");
	    					} else if (strcmp(buf, "/users") == 0) {

	    					} else if (strcmp(buf, "/file") == 0) {
	    						fprintf(stderr, "path: ");
	    						char* path = malloc(128);
	    						fgets(path, 127, stdin); 
	    						// remove '\n'
	    						path[strlen(path) - 1] = '\0';
	    						send_file(path);
	    					} else if (strcmp(buf, "/quit") == 0) {
	    						send_request(QUIT, "0", "0", self->socket_fd);
	    						close(self->socket_fd);
	    						fprintf(stderr, "Bye-bye\n");
	    						free_user(self);
	    						exit(1);
	    					} else {
	    						fprintf(stderr, "use [/help] to get more information\n");
	    					}
	    				} else { //talks
	    					send_request(TALK, self->nickname, buf, self->socket_fd);
	    				}
	    			}
                } else { // get response from server
	                response* respP = malloc(sizeof(response));
	                handle_response(i, respP);
	                
	                switch(respP->activity) {
	                    case OTHERS_TALK:
	                    	print_talk(respP->note, respP->content);
	                        break;
	                    case ASK_PERMISSION:
	                        fprintf(stderr, "Do you want to recieve file %s from %s?(y/n)", respP->filename, respP->content);
	                        int j, accept = 0;
	                        char c;
	                        for(j = 0; ; j++) {
	                        	c = getchar();
	                        	if (c == '\n') {
	                        		break;
	                        	} else if (c == 'y' || c == 'Y') {
	                        		accept = 1;
	                        	}
	                        }
	                        if (accept == 1) {
	                        	recieve_file(respP);
	                        } else {
	                        	send_request(REJECT_FILE, "0", "0", self->socket_fd);
	                        }
	                        break;
	                    case USER_STATUS:
	                    	if (checking_user == 0)
	                    		checking_user = 1;
	                    	if (strcmp(respP->note, "END") == 0)
	                    		checking_user = 0;
	                    	else
	                    		fprintf(stderr, "%s\n", respP->content);
	                    	break;
	                }
	                free_responds(respP);
	            }
            } //FD_ISSET
        } //select
    } //while
}

static void print_talk(char* name, char* msg) {
	fprintf(stderr, "\n%s: %s\n", name, msg);
}

static void send_file(char* path) {
    pthread_t pthread;
    pthread_create(&pthread, NULL, send_file_thread, (void*) path);
}

static void* send_file_thread(void* arg) {
	char* path = (char*)arg;    //get size
    int size, muti = 1, i, j = 0;

    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
    	perror("failed to open file");
    	free(path);
    	return NULL;
    }
    fseek(fp, 0L, SEEK_END);
	size = (int)ftell(fp);
	//seek back to the beginning:
	fseek(fp, 0L, SEEK_SET);

	//parse size
	char note[512];
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
	//get filename
	for (i = 0; ;i++) {
		if (path[i] == '\0')
			break;
		if (path[i] == '/')
			j = i + 1;
	}
	char filename[128];
	subString(filename, path, j, strlen(path));
	strcat(note, ";");
	strcat(note, filename);

	// start to tensfer file
	int ret, file_socket;
	char buf[BUF_LEN];

    int fd = open(path, O_RDONLY);
    if(fd < 0) {
    	perror("failed to open file");
    	free(path);
    	return NULL;
    }
    if ((file_socket = init_connect(argv_g[1], (unsigned short) atoi(argv_g[2]))) < 0) {
        perror("failed to open socket");
    	free(path);
        return NULL;
    }
    send_request(SEND_FILE, note, self->name, file_socket);

    while((ret = read(fd, buf, BUF_LEN)) > 0) {
        write(file_socket, buf, ret);
    }

    close(fd);
    close(file_socket);
    free(path);

    return NULL;
}

typedef struct {
    int size;  // fd to talk with client
    char name[128];  // used by handle_read to know if the header is read or not.
} file_struct;

static void recieve_file(response* resp) {
	file_struct* ft = malloc(sizeof(file_struct));
	strcpy(ft->name, resp->filename);
	ft->size = resp->size;

    pthread_t pthread;
    pthread_create(&pthread, NULL, recieve_file_thread, (void*) ft);
}

static void* recieve_file_thread(void* arg) {
	int i, ret, fd, file_socket, size = 0, file_size = ((file_struct*)arg)->size;
	char file[128];
	file[0] = '\0';
	strcpy(file, "save/");
	strcat(file, ((file_struct*)arg)->name);

	if ((file_socket = init_connect(argv_g[1], (unsigned short) atoi(argv_g[2]))) < 0) {
        perror("failed to open socket");
    	free(arg);
        return NULL;
    }
    send_request(RECV_FILE, "0", ((file_struct*)arg)->name, file_socket);

    sleep(2);
    if((fd = open(file, O_WRONLY|O_TRUNC|O_CREAT)) < 0) {
        perror("failede to create file");
    	free(arg);
        return NULL;
    }

    char buf[BUF_LEN];
    while((ret = read(file_socket, buf, BUF_LEN)) > 0) {
        write(fd, buf, ret);
        buf[ret] = '\0';
        size += ret;
        if (size >= file_size)
            break;
    }
    close(fd);
    close(file_socket);
    free(arg);
    return NULL;
}

static int init_connect(char* ip, unsigned short port) {
    struct sockaddr_in serv_addr;
    int fd;

    if ((fd = socket(AF_INET, SOCK_STREAM , 0)) < 0) {
        perror("failed to open socket");
        return -1;
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(ip);
    serv_addr.sin_port = htons(port);

    if (connect(fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr_in)) < 0) {    
        perror("failed to connect");
        return -1;
    }

    return fd;
}

static void handle_response(int conn_fd, response* respP) {
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
                respP->activity = atoi(tmp);
                count++;
            } else if (count == 1) { // note
                if (respP->activity == ASK_PERMISSION) {
                    int k, l = 0, flag = 0;
                    char file_size[128];
                    for (k = 0; k < i; k++) {
                        if (flag == 1) {
                            (respP->filename)[l++] = tmp[k];
                        } else if (tmp[k] == ';') {
                            flag = 1;
                            file_size[l] = '\0';
                            respP->size = atoi(file_size);
                            respP->filename = malloc(strlen(tmp));
                            l = 0;
                        } else {
                            file_size[l++] = tmp[k];
                        }
                    }
                    (respP->filename)[l] = '\0';
                } else {
                    respP->note = malloc(strlen(tmp));
                    strcpy(respP->note, tmp);
                }
                break;
            }
        }
    }
    subString(tmp, buf, j, r);
    respP->content = tmp;
}

static void send_request(int act, char* note, char* content, int fd) {
    int i;
    char msg[BUF_LEN];
    if (act/10 >= 1) {
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

    write(fd, msg, strlen(msg));
}

static void subString(char* str, char* cpy, int start, int end) {
    int i, j = 0;
    for (i = start; i < end; i++) {
        str[j] = cpy[i];
        j++;
    }
    str[j] = '\0';
}

static void init_user(user* userP) {
    userP->socket_fd = -1;
    userP->login = -1;
}

static void free_user(user* userP) {
    if (userP->name != NULL) {
        free(userP->name);
        userP->name = NULL;
    }
    if (userP->nickname != NULL) {
        free(userP->nickname);
        userP->nickname = NULL;
    }
    init_user(userP);
}

static void free_responds(response* respP) {
    if (respP->note != NULL) {
        free(respP->note);
        respP->note = NULL;
    }
    if (respP->content != NULL) {
        free(respP->content);
        respP->content = NULL;
    }
    if (respP->filename != NULL) {
        free(respP->filename);
        respP->filename = NULL;
    }
    if (respP->owner != NULL) {
        free(respP->owner);
        respP->owner = NULL;
    }

}