#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include "state.h"
#include "liburing.h"

#define MAX_CONNECTIONS     1000
#define BACKLOG             512
#define MAX_MESSAGE_LEN     1024
#define BUFFERS_COUNT       MAX_CONNECTIONS

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags);
void add_socket_read(struct io_uring *ring, int fd, unsigned gid, int size, unsigned flags);
void add_socket_update(struct io_uring *ring, int fd, __u16 bid, int size, unsigned flags);
void add_provide_buf(struct io_uring *ring, __u16 bid, unsigned gid);

enum {
    ACCEPT,
    READ,
    UPDATE,
    PROV_BUF,
};

typedef struct conn_info {
    __u32 fd;
    __u16 type;
    __u16 bid;
} conn_info;

typedef struct {
    state_t state;
    int curfd; // current file descriptor (client socket)
    int num;
} client_state_t;

client_state_t clients[MAX_CONNECTIONS]; // array of clients

void initClients() {
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    clients[i].state = init(); // it returns the initial state 'a'
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
    perror_die("Sorry, maximum number of clients reached"); // utility function in files utils.c						                            // used to terminate the program
}

client_state_t *findClient(int fd) { // return pointer to the memory location of the client
				                    // else return null
  for (int i = 0; i < MAX_CONNECTIONS; i++) {
    if (clients[i].curfd == fd)
      return &clients[i];
  }
  return NULL;
}

uint8_t bufs[BUFFERS_COUNT][MAX_MESSAGE_LEN] = {0};
int group_id = 1337;

int main(int argc, char *argv[]) {

    int sock_listen_fd = srv_init();
    initClients();
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // initialize io_uring
    struct io_uring_params params;
    struct io_uring ring;
    memset(&params, 0, sizeof(params)); // initialize params memory area

    if (io_uring_queue_init_params(2048, &ring, &params) < 0) {
        perror("io_uring_init_failed...\n");
        exit(1);
    }

    // check if IORING_FEAT_FAST_POLL is supported
    if (!(params.features & IORING_FEAT_FAST_POLL)) {
        printf("IORING_FEAT_FAST_POLL not available in the kernel, quitting...\n");
        exit(0);
    }

    // check if buffer selection is supported
    struct io_uring_probe *probe; 
    probe = io_uring_get_probe_ring(&ring); // function that allow to check for supported operations and capabilities

    // to determine of an io_uring operation is supported by your kernel. 
    // Returns 0 if the operation is not supported and a non-zero value if support is present
    if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
        printf("Buffer select not supported, skipping...\n");
        exit(0);
    }
    free(probe);

    // register buffers for buffer selection
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(&ring);
    // register buffers with group_id
    io_uring_prep_provide_buffers(sqe, bufs, MAX_MESSAGE_LEN, BUFFERS_COUNT, group_id, 0);

    io_uring_submit(&ring);
    io_uring_wait_cqe(&ring, &cqe);
    if (cqe->res < 0) {
        printf("cqe->res = %d\n", cqe->res);
        exit(1);
    }
    io_uring_cqe_seen(&ring, cqe);

    // add first accept SQE to monitor for new incoming connections
    add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);

    // start event loop
    while (1) {
        // submits the SQEs acquired via io_uring_get_sqe() and waits unitl one request is not processed by the kernel and its details
        // placed in the completion queue
        io_uring_submit_and_wait(&ring, 1);
        struct io_uring_cqe *cqe;
        unsigned head;
        unsigned count = 0;

        // go through all CQEs starting from the head of the CQE
        io_uring_for_each_cqe(&ring, head, cqe) {
            ++count;
            struct conn_info conn_i;
            memcpy(&conn_i, &cqe->user_data, sizeof(conn_i));

            int type = conn_i.type;
            if (cqe->res == -ENOBUFS) {
                fprintf(stdout, "bufs in automatic buffer selection empty, this should not happen...\n");
                fflush(stdout);
                exit(1);
            } else if (type == PROV_BUF) {
                if (cqe->res < 0) {
                    printf("cqe->res = %d\n", cqe->res);
                    exit(1);
                }
            } else if (type == ACCEPT) {
                int sock_conn_fd = cqe->res;
                // only read when there is no error, >= 0
                if (sock_conn_fd >= 0) {
                    addClient(sock_conn_fd);
                    add_socket_read(&ring, sock_conn_fd, group_id, MAX_MESSAGE_LEN, IOSQE_BUFFER_SELECT);
                }

                // new connected client; read data from socket and re-add accept to monitor for new connections
                add_accept(&ring, sock_listen_fd, (struct sockaddr *)&client_addr, &client_len, 0);
            } else if (type == READ) {
                int bytes_read = cqe->res;
                // buffer ID
                int bid = cqe->flags >> 16;
                if (cqe->res <= 0) {
                    // read failed, re-add the buffer
                    add_provide_buf(&ring, bid, group_id);
                    // connection closed or error
                    close(conn_i.fd);
                } else {
                    // bytes have been read into bufs, now add write to socket sqe
                    add_socket_update(&ring, conn_i.fd, bid, bytes_read, 0);
                }
            } else if (type == UPDATE) {
                // write has been completed, first re-add the buffer
                add_provide_buf(&ring, conn_i.bid, group_id);
                // add a new read for the existing connection
                add_socket_read(&ring, conn_i.fd, group_id, MAX_MESSAGE_LEN, IOSQE_BUFFER_SELECT);
            }
        }

        // advance in the looping through the CQEs
        io_uring_cq_advance(&ring, count);
    }
}

void add_accept(struct io_uring *ring, int fd, struct sockaddr *client_addr, socklen_t *client_len, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sets up the submission queue entry pointed to by sqe with a accept like operation. 
    io_uring_prep_accept(sqe, fd, client_addr, client_len, 0);
    io_uring_sqe_set_flags(sqe, flags);

    conn_info conn_i = {
        .fd = fd,
        .type = ACCEPT,
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));

}

void add_socket_read(struct io_uring *ring, int fd, unsigned gid, int message_size, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    // sets up the submission queue entry pointed to by sqe with a recv(2) like operation
    io_uring_prep_recv(sqe, fd, NULL, message_size, 0);
    io_uring_sqe_set_flags(sqe, flags);
    sqe->buf_group = gid;

    conn_info conn_i = {
        .fd = fd,
        .type = READ,
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

void add_socket_update(struct io_uring *ring, int fd, __u16 bid, int message_size, unsigned flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_sqe_set_flags(sqe, flags);

    
    message_t msg;
    msg.len = message_size;
    // copy content of the buffer in the msg.actions array
    memcpy(msg.actions, bufs[bid], message_size);


    // update the client state
    findClient(fd)-> state = compute_state(findClient(fd)->num, findClient(fd)->state, msg);    

    // send back an ack for benchmarking purposes
    char buf[1] = "";
    io_uring_prep_send(sqe, fd, &buf,1 , 0);

    if (isExitState(findClient(fd)->state)) {
          printf("peer done");
          sendDone(findClient(fd)->curfd);
    }


    conn_info conn_i = {
        .fd = fd,
        .type = UPDATE,
        .bid = bid,
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}

void add_provide_buf(struct io_uring *ring, __u16 bid, unsigned gid) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    io_uring_prep_provide_buffers(sqe, bufs[bid], MAX_MESSAGE_LEN, 1, gid, bid);

    conn_info conn_i = {
        .fd = 0,
        .type = PROV_BUF, 
    };
    memcpy(&sqe->user_data, &conn_i, sizeof(conn_i));
}
