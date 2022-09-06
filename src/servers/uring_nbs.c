#include <liburing.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include "state.h"

#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/poll.h>

#define MAX_CONNECTIONS 1000
#define BACKLOG 512
#define MAX_MESSAGE_LEN 2048
#define IORING_FEAT_FAST_POLL (1U << 5)

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags);
void add_socket_read(struct io_uring* ring, int fd, size_t size, unsigned flags);
void add_socket_update(struct io_uring* ring, int fd, int message_size, unsigned flags);

enum {
    ACCEPT,
    POLL_LISTEN,
    POLL_NEW_CONNECTION,
    READ,
    UPDATE,
};

typedef struct conn_info
{
    unsigned fd;
    unsigned type;
} conn_info;

conn_info conns[MAX_CONNECTIONS];
uint8_t bufs[MAX_CONNECTIONS][MAX_MESSAGE_LEN];

typedef struct {
    state_t state;
    int curfd;
    int num;
} client_state_t;

client_state_t clients[MAX_CONNECTIONS];

void initClients() {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    clients[i].state = init();
    clients[i].curfd = 0;
    clients[i].num = i;
  }
}

void addClient(curfd) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (clients[i].curfd == 0) {
            clients[i].state = init();
            clients[i].curfd = curfd;
            return;
        }        
    }
    perror_die("Sorry, maximum number of clients reached");
}

client_state_t *findClient(int fd) { 
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (clients[i].curfd == fd)
      return &clients[i];
  }
  return NULL;
}

int main(int argc, char *argv[])
{
    int sock_listen_fd = srv_init();
    initClients();
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);


    // initialize io_uring
    struct io_uring_params params;
    struct io_uring ring;
    memset(&params, 0, sizeof(params));

    if (io_uring_queue_init_params(4096, &ring, &params) < 0)
    {
        perror("io_uring_init_failed...\n");
        exit(1);
    }

    if (!(params.features & IORING_FEAT_FAST_POLL))
    {
        printf("IORING_FEAT_FAST_POLL not available in the kernel, quiting...\n");
        exit(0);
    }


    // add first accept sqe to monitor for new incoming connections
    add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);


    // start event loop
    while (1)
    {
        struct io_uring_cqe *cqe;
        int ret;

        // tell kernel we have put a sqe on the submission ring
        io_uring_submit(&ring);

        // wait for new cqe to become available
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret != 0)
        {
            perror("Error io_uring_wait_cqe\n");
            exit(1);
        }

        // check how many cqe's are on the cqe ring at this moment
        struct io_uring_cqe *cqes[BACKLOG];
        int cqe_count = io_uring_peek_batch_cqe(&ring, cqes, sizeof(cqes) / sizeof(cqes[0]));

        // go through all the cqe's
        for (int i = 0; i < cqe_count; ++i)
        {
            struct io_uring_cqe *cqe = cqes[i];
            struct conn_info *user_data = (struct conn_info *)io_uring_cqe_get_data(cqe);
            int type = user_data->type;

            if (type == ACCEPT)
            {
                int sock_conn_fd = cqe->res;
                io_uring_cqe_seen(&ring, cqe);

                // new connected client; read data from socket and re-add accept to monitor for new connections
                addClient(sock_conn_fd);
                add_socket_read(&ring, sock_conn_fd, MAX_MESSAGE_LEN, 0);
                add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
            }
            else if (type == READ)
            {
                int bytes_read = cqe->res;
                if (bytes_read <= 0)
                {
                    // no bytes available on socket, client must be disconnected
                    io_uring_cqe_seen(&ring, cqe);
                    shutdown(user_data->fd, SHUT_RDWR);
                }
                else
                {
                    // bytes have been read into bufs, now add write to socket sqe
                    io_uring_cqe_seen(&ring, cqe);
                    add_socket_update(&ring, user_data->fd, bytes_read, 0);
                }
            }
            else if (type == UPDATE)
            {
                // write to socket completed, re-add socket read
                io_uring_cqe_seen(&ring, cqe);
                add_socket_read(&ring, user_data->fd, MAX_MESSAGE_LEN, 0);
            }
        }
    }
}

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = ACCEPT;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_read(struct io_uring *ring, int fd, size_t size, unsigned flags)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_recv(sqe, fd, &bufs[fd], size, 0);
    io_uring_sqe_set_flags(sqe, flags);

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = READ;

    io_uring_sqe_set_data(sqe, conn_i);
}

void add_socket_update(struct io_uring *ring, int fd, int message_size, unsigned flags)
{
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);

    message_t msg;
    msg.len = message_size;
    // copy content of the buffer in the msg.actions array
    memcpy(msg.actions, bufs[fd], message_size);


    // update the client state
    findClient(fd)-> state = compute_state(findClient(fd)->num, findClient(fd)->state, msg);    
    if (isExitState(findClient(fd)->state)) {
          printf("peer done");
          sendDone(findClient(fd)->curfd);
    }

    char buf[1] = "";
    io_uring_prep_send(sqe, fd, &buf,1 , 0);
    io_uring_sqe_set_flags(sqe, flags);

    conn_info *conn_i = &conns[fd];
    conn_i->fd = fd;
    conn_i->type = UPDATE;

    io_uring_sqe_set_data(sqe, conn_i);
}
