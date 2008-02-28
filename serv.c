#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>

/* GD */
#include <gd.h>
#include <gdfonts.h>

/* Pthreads, or on win32, emulated pthreads */
#include "win32-pthread-emul.h"

/* Win32 requires O_BINARY as an option to open(), but noone else even has it defined */
#ifndef O_BINARY
#define O_BINARY 0
#endif

struct session_info_st;
struct session_info_st {
	uint32_t sessionid;
	gdImagePtr imgptr;
	pthread_mutex_t imgptr_mut;
	uint16_t lastx;
	uint16_t lasty;
	int img_color_black;
	time_t last_used_time;
	struct session_info_st *_next;
};
struct session_info_st *session_list = NULL;
pthread_mutex_t session_list_mut;

struct image_info_st {
	void *imgbuf;
	int32_t imgbuflen;
};

typedef enum {
	WEBDRAW_EVENT_MOVE,
	WEBDRAW_EVENT_CLICK,
} webdraw_event_t;


struct session_info_st *find_session_info(uint32_t sessionid, int createIfNotExisting) {
	struct session_info_st *ret = NULL, *chk_session_list;

	/* search for an existing session */
	pthread_mutex_lock(&session_list_mut);

	for (chk_session_list = session_list; chk_session_list; chk_session_list = chk_session_list->_next) {
		if (chk_session_list->sessionid == sessionid) {
			ret = chk_session_list;
			break;
		}
	}

	/* create it and initialize it if needed */
	if (!ret && createIfNotExisting) {
		ret = malloc(sizeof(*ret));
		if (ret) {
			ret->sessionid = sessionid;
			ret->imgptr = NULL;
			ret->last_used_time = 0;
			ret->lastx = 65535;
			ret->lasty = 65535;
			pthread_mutex_init(&ret->imgptr_mut, NULL);

			ret->_next = session_list;
			session_list = ret;
		}
	}

	pthread_mutex_unlock(&session_list_mut);

	return(ret);
}

void cleanup_sessions(int session_age_limit) {
	struct session_info_st *chk_session_list, *next, *prev = NULL;
	time_t expire_time;

	expire_time = time(NULL) - session_age_limit;

	pthread_mutex_lock(&session_list_mut);

	for (chk_session_list = session_list; chk_session_list; chk_session_list = next) {
		next = chk_session_list->_next;

		if (chk_session_list->last_used_time < expire_time) {
			if (chk_session_list) {
				if (chk_session_list->imgptr) {
					gdImageDestroy(chk_session_list->imgptr);
				}
				pthread_mutex_destroy(&chk_session_list->imgptr_mut);
			}

			if (session_list == chk_session_list) {
				session_list = next;
			}

			if (prev) {
				prev->_next = next;
			}

			free(chk_session_list);
		} else {
			prev = chk_session_list;
		}
	}

	pthread_mutex_unlock(&session_list_mut);

	return;
}

struct image_info_st *get_image(uint32_t sessionid) {
	struct image_info_st *ret;
	struct session_info_st *curr_sess = NULL;

	curr_sess = find_session_info(sessionid, 0);

	if (!curr_sess) {
		return(NULL);
	}

	ret = malloc(sizeof(*ret));
	if (!ret) {
		return(NULL);
	}

	pthread_mutex_lock(&curr_sess->imgptr_mut);
	ret->imgbuf = gdImagePngPtr(curr_sess->imgptr, &ret->imgbuflen);
	pthread_mutex_unlock(&curr_sess->imgptr_mut);

	return(ret);
}

struct image_info_st *get_image_str(const char *sessionid_str) {
	uint32_t sessionid;

	sessionid = strtoul(sessionid_str, NULL, 10);

	return(get_image(sessionid));
}

int handle_event(uint32_t sessionid, uint16_t x, uint16_t y, webdraw_event_t type) {
	struct session_info_st *curr_sess = NULL;
	FILE *pngfp;

	curr_sess = find_session_info(sessionid, 1);

	if (!curr_sess) {
		return(-1);
	}

	/* Update session with new data */
	pthread_mutex_lock(&curr_sess->imgptr_mut);
	if (!curr_sess->imgptr) {
		pngfp = fopen("blank.png", "rb");
		if (pngfp) {
			curr_sess->imgptr = gdImageCreateFromPng(pngfp);
			fclose(pngfp);

			if (curr_sess->imgptr) {
				curr_sess->img_color_black = gdImageColorAllocate(curr_sess->imgptr, 0, 0, 0); 
			}
		}
	}

	if (curr_sess->imgptr && curr_sess->lastx != 65535 && curr_sess->lasty != 65535) {
		/* Update image */
		gdImageSetAntiAliased(curr_sess->imgptr, curr_sess->img_color_black);
		gdImageLine(curr_sess->imgptr, curr_sess->lastx, curr_sess->lasty, x, y, gdAntiAliased);
	}

	if (curr_sess->imgptr && type == WEBDRAW_EVENT_CLICK) {
		gdImageFilledArc(curr_sess->imgptr, x, y, 5, 5, 0, 360, curr_sess->img_color_black, gdArc);
	}

	curr_sess->lastx = x;
	curr_sess->lasty = y;
	pthread_mutex_unlock(&curr_sess->imgptr_mut);

	/* Update session information */
	curr_sess->last_used_time = time(NULL);

	return(0);
}

int handle_event_str(char *str, webdraw_event_t type) {
	uint32_t sessionid;
	uint16_t x, y;
	char *sessionid_str, *x_str, *y_str;

	sessionid_str = str;
	x_str = strchr(sessionid_str, ',');
	if (!x_str) {
		return(-1);
	}
	*x_str = '\0';
	x_str++;

	y_str = strchr(x_str, ',');
	if (!y_str) {
		return(-1);
	}
	*y_str = '\0';
	y_str++;

	sessionid = strtoul(sessionid_str, NULL, 10);
	x = strtoul(x_str, NULL, 10);
	y = strtoul(y_str, NULL, 10);

	return(handle_event(sessionid, x, y, type));
}

/* Not HTTP/1.0 compliant, yet */
THREAD_FUNCTION_RETURN handle_connection(void *arg) {
	ssize_t bytes_recv;
	size_t buflen, bufused;
	char buf[16384], *buf_p, *request_end_p = NULL;
	char *request_line, *request_line_end_p, *request_line_operation, *request_line_resource, *request_line_protocol;
	char *curr_header_p, *next_header_p, *curr_header_var, *curr_header_val;
	int fd, *fd_p;
	int abort = 0, close_conn, invalid_request = 0;

	struct image_info_st *imginfo = NULL;
	struct stat fileinfo;
	ssize_t sent_bytes, read_bytes;
	size_t bytes_to_send, bytes_to_read;
	char reply_buf[1024], *reply_buf_p, copy_buf[8192];
	char *http_reply_msg, *http_reply_body, *http_reply_body_file, *http_reply_content_type;
	int http_reply_code, http_reply_content_length = 0;
	int reply_buf_len;
	int stat_ret, event_ret;
	int srcfd;

	/* Determine our args original values */
	fd_p = arg;
	fd = *fd_p;

	/* Pre-use initialization */
	buf_p = buf;
	bufused = 0;
	*buf_p = '\0';
	while (1) {
		close_conn = 1;
		imginfo = NULL;
		while (1) {
			buflen = sizeof(buf) - (buf_p - buf) - bufused - 1;
			if (buflen == 0) {
				/* We ran out of buffer space on input, oops. */
				abort = 1;
				break;
			}

			buf_p[bufused] = '\0';
			request_end_p = strstr(buf_p, "\015\012\015\012");
			if (bufused == 0 || !request_end_p) {
				bytes_recv = recv(fd, buf_p + bufused, buflen, 0);
				if (bytes_recv <= 0) {
					abort = 1;
					break;
				}

				bufused += bytes_recv;
				buf_p[bufused] = '\0';
			}

			request_end_p = strstr(buf_p, "\015\012\015\012");
			if (!request_end_p) {
				continue;
			}

			/* We do not handle POST requests */
			break;
		}

		if (abort) {
			break;
		}

		if (!request_end_p) {
			break;
		}

		/* Parse HTTP request */
		request_line_end_p = strstr(buf_p, "\015\012");
		*request_line_end_p = '\0';
		request_line = buf_p;

		request_line_operation = request_line;
		request_line_resource = strchr(request_line_operation, ' ');
		if (!request_line_resource) {
			break;
		}
		*request_line_resource = '\0';
		request_line_resource++;

		request_line_protocol = strchr(request_line_resource, ' ');
		if (!request_line_protocol) {
			request_line_protocol = "HTTP/1.0";
		} else {
			*request_line_protocol = '\0';
			request_line_protocol++;
		}

		if (strcasecmp(request_line_operation, "GET") != 0) {
			/* We only support GET */
			invalid_request = 1;
			break;
		}

		if (strcasecmp(request_line_protocol, "HTTP/1.1") == 0) {
			close_conn = 0;
		}

		curr_header_p = request_line_end_p + 2;
		while (1) {
			if (curr_header_p == (request_end_p + 2)) {
				break;
			}

			next_header_p = strstr(curr_header_p, "\015\012");
			if (next_header_p) {

				*next_header_p = '\0';
			}

			/* Parse header into variable and value components, and trim leading spaces */
			curr_header_var = curr_header_p;
			curr_header_val = strchr(curr_header_p, ':');
			if (!curr_header_val) {
				/* Malformed header */
				abort = 1;
				break;
			}
			*curr_header_val = '\0';
			curr_header_val++;
			while (*curr_header_val && (*curr_header_val == ' ' || *curr_header_val == '\t')) {
				curr_header_val++;
			}

			/* Handle HTTP headers as appropriate */
			if (strcasecmp(curr_header_var, "connection") == 0) {
				if (strcasecmp(curr_header_val, "close") == 0) {
					close_conn = 1;
				}
				if (strcasecmp(curr_header_val, "keep-alive") == 0) {
					close_conn = 0;
				}
			}

			/* Iterate */
			if (!next_header_p) {
				break;
			}
			curr_header_p = next_header_p + 2;
		}

		if (abort) {
			break;
		}

		/* Process request */
		if (strncmp(request_line_resource, "/event/", 7) == 0) {
			/* Process an event */
			if (strncmp(request_line_resource + 7, "move?", 5) == 0) {
				event_ret = handle_event_str(request_line_resource + 12, WEBDRAW_EVENT_MOVE);
			} else if (strncmp(request_line_resource + 7, "click?", 6) == 0) {
				event_ret = handle_event_str(request_line_resource + 13, WEBDRAW_EVENT_CLICK);
			} else {
				event_ret = -1;
			}

			/* Reply to event */
			if (event_ret == 0) {
				http_reply_code = 200;
				http_reply_msg = "OK";
				http_reply_content_type = "text/plain";
				http_reply_body_file = NULL;

				/* Body should be a valid JavaScript command, it will be eval()'d */
				http_reply_body = "";
				http_reply_content_length = strlen(http_reply_body);
			} else {
				http_reply_code = 500;
				http_reply_msg = "Event Error";
				http_reply_content_type = "text/plain";
				http_reply_body = "Event Error";
				http_reply_body_file = NULL;
				http_reply_content_length = strlen(http_reply_body);
			}
		} else if (strncmp(request_line_resource, "/dynamic/image?", 15) == 0) {
			/* Return an image */
			imginfo = get_image_str(request_line_resource + 15);

			if (imginfo) {
				http_reply_code = 200;
				http_reply_msg = "OK";
				http_reply_content_type = "image/png";
				http_reply_body = imginfo->imgbuf;
				http_reply_body_file = NULL;
				http_reply_content_length = imginfo->imgbuflen;
			} else {
				http_reply_code = 500;
				http_reply_msg = "Event Error";
				http_reply_content_type = "text/plain";
				http_reply_body = "Event Error";
				http_reply_body_file = NULL;
				http_reply_content_length = strlen(http_reply_body);
			}
		} else if (strcmp(request_line_resource, "/static/page.html") == 0) {
			/* Return a file */
			http_reply_code = 200;
			http_reply_msg = "OK";
			http_reply_content_type = "text/html";
			http_reply_body = NULL;
			http_reply_body_file = "page.html";
		} else if (strcmp(request_line_resource, "/static/page-test.html") == 0) {
			/* Return a file */
			http_reply_code = 200;
			http_reply_msg = "OK";
			http_reply_content_type = "text/html";
			http_reply_body = NULL;
			http_reply_body_file = "page-test.html";
		} else if (strcmp(request_line_resource, "/static/blank.png") == 0) {
			/* Return a file */
			http_reply_code = 200;
			http_reply_msg = "OK";
			http_reply_content_type = "image/png";
			http_reply_body = NULL;
			http_reply_body_file = "blank.png";
		} else if (strcmp(request_line_resource, "/static/serv.c") == 0) {
			/* Return a file */
			http_reply_code = 200;
			http_reply_msg = "OK";
			http_reply_content_type = "text/plain";
			http_reply_body = NULL;
			http_reply_body_file = "serv.c";
		} else {
			/* Return an error */
			http_reply_code = 404;
			http_reply_msg = "Resource not found";
			http_reply_body = "<html><head><title>Resource not found</title></head><body><h1>Resource not found</h1><br>This HTTP server offers very limited resources.</body></html>";
			http_reply_body_file = NULL;
			http_reply_content_type = "text/html";
			http_reply_content_length = strlen(http_reply_body);
		}

		if (http_reply_body == NULL) {
			if (http_reply_body_file != NULL) {
				stat_ret = stat(http_reply_body_file, &fileinfo);
				if (stat_ret != 0) {
					break;
				}

				http_reply_content_length = fileinfo.st_size;
			} else {
				break;
			}
		} else {
			http_reply_body_file = NULL;
		}

		/* Form reply and send it */
		reply_buf_len = snprintf(reply_buf, sizeof(reply_buf), "%s %i %s\015\012Date: %s\015\012Server: webdraw\015\012Connection: %s\015\012Content-Length: %i\015\012Content-Type: %s\015\012\015\012", request_line_protocol, http_reply_code, http_reply_msg, "Thu, 21 Feb 2008 08:16:03 GMT", (close_conn ? "close" : "keep-alive"), http_reply_content_length, http_reply_content_type);
		bytes_to_send = reply_buf_len;
		reply_buf_p = reply_buf;
		while (bytes_to_send) {
			sent_bytes = send(fd, reply_buf_p, bytes_to_send, 0);
			if (sent_bytes <= 0) {
				abort = 1;
				break;
			}

			bytes_to_send -= sent_bytes;
			reply_buf_p += sent_bytes;
		}
		if (abort) {
			break;
		}

		/* Send body */
		if (http_reply_body != NULL) {
			/* String */
			bytes_to_send = http_reply_content_length;
			reply_buf_p = http_reply_body;
			while (bytes_to_send) {
				sent_bytes = send(fd, reply_buf_p, bytes_to_send, 0);
				if (sent_bytes <= 0) {
					abort = 1;
					break;
				}

				bytes_to_send -= sent_bytes;
				reply_buf_p += sent_bytes;
			}
		} else {
			/* File */
			srcfd = open(http_reply_body_file, O_RDONLY | O_BINARY);

			bytes_to_send = http_reply_content_length;
			reply_buf_p = http_reply_body;
			while (bytes_to_send) {
				if (bytes_to_send <= sizeof(copy_buf)) {
					bytes_to_read = bytes_to_send;
				} else {
					bytes_to_read = sizeof(copy_buf);
				}

				read_bytes = read(srcfd, copy_buf, bytes_to_read);
				if (read_bytes <= 0) {
					abort = 1;
					break;
				}

				sent_bytes = send(fd, copy_buf, read_bytes, 0);
				if (sent_bytes <= 0) {
					abort = 1;
					break;
				}

				bytes_to_send -= sent_bytes;
				reply_buf_p += sent_bytes;
			}

			close(srcfd);
		}

		if (imginfo) {
			if (imginfo->imgbuf) {
				gdFree(imginfo->imgbuf);
				imginfo->imgbuf = NULL;
			}
			free(imginfo);
			imginfo = NULL;
		}

		if (abort) {
			break;
		}
		
		/* Close connection if set */
		if (close_conn) {
			break;
		}

		/* Prepare for the next connection */
		/* We support pipelining by ignoring part of the buffer, or resetting it if possible */
		bufused -= (request_end_p + 4) - buf_p;
		if (bufused == 0) {
			buf_p = buf;
		} else {
			buf_p = request_end_p + 4;
		}
	}
	
	if (imginfo) {
		if (imginfo->imgbuf) {
			gdFree(imginfo->imgbuf);
			imginfo->imgbuf = NULL;
		}
		free(imginfo);
		imginfo = NULL;
	}


	close(fd);
	free(fd_p);

#ifdef _WIN32
	return(0);
#else
	return(NULL);
#endif
}

int main(int argc, char **argv) {
	struct sockaddr_in localaddr;
	pthread_t curr_thread;
	int masterfd, currfd, *currfd_copy;
	int bind_ret, listen_ret, pthcreate_ret;
	uint16_t parm_port = 8013;
#ifdef _WIN32
	/* Win32 Specific Crap */
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
		fprintf(stderr, "WSAStartup() failed.  Blame Microsoft.\n");
		return(EXIT_FAILURE);
	}
	if (wsaData.wVersion != MAKEWORD(2, 0)) {
		WSACleanup();
		fprintf(stderr, "WSAStartup() returned unexpected version.  Blame Microsoft.\n");
		return(EXIT_FAILURE);
	}
#endif

	/* Initialize session list mutex */
	pthread_mutex_init(&session_list_mut, NULL);


	/* Setup listening socket on appropriate port */
	if (argc == 2) {
		if (!argv[1]) {
			fprintf(stderr, "Invalid port specification\n");
			return(EXIT_FAILURE);
		}

		parm_port = atoi(argv[1]);
		if (parm_port == 0) {
			fprintf(stderr, "Invalid port specification %s (== %i), must be between 1 and 65535\n", argv[1], parm_port);
			return(EXIT_FAILURE);
		}
	}

	masterfd = socket(PF_INET, SOCK_STREAM, 0);
	if (masterfd < 0) {
		perror("socket");
		return(EXIT_FAILURE);
	}

	localaddr.sin_family = AF_INET;
	localaddr.sin_port = htons(parm_port);
	localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	while (1) {
		printf("Binding to 0.0.0.0:%i\n", parm_port);
		bind_ret = bind(masterfd, (struct sockaddr *) &localaddr, sizeof(localaddr));
		if (bind_ret != 0) {
			perror("bind");
#ifndef _WIN32
			sleep(5);
#endif
			continue;
		}

		break;
	}

	listen_ret = listen(masterfd, 5);
	if (listen_ret != 0) {
		perror("listen");
		return(EXIT_FAILURE);
	}

#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	/* Handle incoming connections and create worker threads to handle them */
	while (1) {
		cleanup_sessions(300);

		currfd = accept(masterfd, NULL, NULL);
		if (currfd < 0) {
			perror("accept");
			return(EXIT_FAILURE);
		}

		/* Duplicate child FD for worker thread -- it will be freed appropriately */
		currfd_copy = malloc(sizeof(*currfd_copy));
		if (!currfd_copy) {
			perror("malloc");
			return(EXIT_FAILURE);
		}
		*currfd_copy = currfd;

		/* Create worker thread for this request */
		pthcreate_ret = pthread_create(&curr_thread, NULL, handle_connection, currfd_copy);
		if (pthcreate_ret != 0) {
			fprintf(stderr, "Error creating thread, aborting. pthread_create() returned %i\n", pthcreate_ret);
			return(EXIT_FAILURE);
		}
	}

	return(EXIT_FAILURE);
}
