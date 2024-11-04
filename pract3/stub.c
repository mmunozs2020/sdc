#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <pthread.h> 


#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf("DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif


#define SOCKET_RUNNING      1
#define SOCKET_CLOSED       -1

#define F_FAILURE           -1  // returned when a function failed
#define F_SUCCESS           0   // returned when a function succeded
#define F_CONN_CLOSE        -3  // returned when recv() returns 0 bytes read

#define STUB_EXIT_SIGINT    12  // exit status when terminating by sigint signal

#define NUM_OF_CLIENTS      2


enum operations {
    READY_TO_SHUTDOWN = 0,
    SHUTDOWN_NOW,
    SHUTDOWN_ACK
};

struct message {
    char origin[20];
    enum operations action;
    unsigned int clock_lamport;
};


// GLOBAL VARIABLES:
char *stub_whoami;          // copy from whoami (see P1, P2 or P3...)
int sock_status     = 0;    // can take SOCKET_RUNNING or SOCKET_CLOSED as value
int l_clock         = 0;    // global Lamport clock

int sock_sfd        = 0;    // Socket file descriptor (NOT CONNECTION, SOCKET)
int cli_1_cfd       = 0;    // (server only!) stores the fd of connection P2-P1
int cli_3_cfd       = 0;    // (server only!) stores the fd of connection P2-P3

int conn_count      = 0;    // counts connections established with threads
pthread_t conn_threads[NUM_OF_CLIENTS]; // (server only!) threads to receive from clients
pthread_t client_thread;                // (clients only!) thread to receive from server

int shutdown_acks   = 0;    // counts the SHUTDOWN_ACK's received by the server


// MUTEXES:
pthread_mutex_t mutex_lclock    = PTHREAD_MUTEX_INITIALIZER; // protects l_clock
pthread_mutex_t mutex_shutack   = PTHREAD_MUTEX_INITIALIZER; // protects SHUTDOWN_ACKs


//-- handles sigint signals when received
void handle_sigint(int sig) {
    printf("\nSocket shutdown received (CTRL+C)...\n");

    if (sock_status == SOCKET_RUNNING) {    // closes the socket if necessary
        close(sock_sfd);
        DEBUG_PRINTF("---------------- SOCKET CLOSED ----------------\n");
        sock_status = SOCKET_CLOSED;
    }

    exit(STUB_EXIT_SIGINT);
}

//-- prints an error message along the perror info and updates the socket status
void perror_msg(char *msg, int sockfd, int is_socket_running) {
    perror(msg);

    if (is_socket_running) {
        DEBUG_PRINTF("Closing socket after perror\n");
        close(sockfd);
        sock_status = SOCKET_CLOSED;
    }
}

//-- calls perror_msg with SOCKET_RUNNING as third parameter
#define perror_msg_sr(msg, sockfd) perror_msg(msg, sockfd, SOCKET_RUNNING)

//-- returns F_SUCCESS in case argc is 3, and F_FAILURE otherwise
int check_argnum(int argc) {
    if (argc != 3) {
        fprintf(stderr, "usage: ./%s <ip_address> <port>\n", stub_whoami);
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- tries to get the port (string parameter) and convert it to int
int try_get_port(char *str_port) {
    char *endptr;
    long int li_port;

    // Get long int from str and look for possible failures (bad format)
    li_port = strtol(str_port, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0') {
        // terminates with F_FAILURE when bad format is detected
        fprintf(stderr, "error: non-valid port (bad format)\n");
        return F_FAILURE;
    }

    return (int)li_port;
}

//-- starts up a new socket, returns its fd when success, F_FAILURE otherwise
int init_socket(struct sockaddr_in *servaddr, char *serv_ip, int serv_port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0) {
        perror_msg("Error creating socket", sock_fd, SOCKET_CLOSED);
        return F_FAILURE;
    }
    printf("Socket successfully created...\n");

    // port and IP configuration for the socket
    servaddr->sin_family = AF_INET;
    if (inet_pton(AF_INET, serv_ip, &(servaddr->sin_addr)) <= 0) {
        perror_msg_sr("Invalid address / Address not supported", sock_fd);
        return F_FAILURE;
    }
    servaddr->sin_port = htons(serv_port);

    // sock_status is set to SOCKET_RUNNING when success
    sock_status = SOCKET_RUNNING;
    return sock_fd;
}

//-- (server only!) forces near-instant reuse of ports
void enable_setsockopt(int serv_sfd) {
    const int ENABLE_SSOPT = 1;

    if (setsockopt(serv_sfd, SOL_SOCKET, SO_REUSEADDR, &ENABLE_SSOPT, sizeof(int)) < 0) {
        perror_msg_sr("setsockopt(SO_REUSEADDR) failed\n", serv_sfd);
    }
}

//-- (server only!) bind and listen
int bind_and_listen(int serv_sfd, struct sockaddr_in *servaddr) {
    if (bind(serv_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_msg_sr("bind failed", serv_sfd);
        return F_FAILURE;
    }
    printf("Socket successfully binded...\n");

    // backlog=1 so the server has no trouble listening both clients
    if (listen(serv_sfd, 1) < 0) {
        perror_msg_sr("listen failed", serv_sfd);
        return F_FAILURE;
    }

    printf("Server listening...\n");
    return F_SUCCESS;
}

//-- (server only!) aceepts a client and returns the connection fd related to it
int accept_client(int serv_sfd, struct sockaddr_in *cliaddr) {
    socklen_t cliaddr_len = sizeof(*cliaddr);
    int conn_fd = accept(serv_sfd, (struct sockaddr * ) cliaddr, &cliaddr_len);

    if (conn_fd < 0) {
        perror_msg_sr("accept failed", serv_sfd);
        return F_FAILURE;
    }

    return conn_fd;
}

//-- (client only!) tries to connect the client to the server
int connect_to_server(int cli_sfd, struct sockaddr_in *servaddr) {
    if (connect(cli_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_msg_sr("connect error", cli_sfd);
        return F_FAILURE;
    }

    printf("connected to the server...\n");
    return F_SUCCESS;
}

//-- updates the global Lamport clock (l_clock) after the current local value
void update_clock_lamport(int *l_clock_loc) {
    pthread_mutex_lock(&mutex_lclock);      // lock (X)

    // UPDATE: global_clock = max(global_clock, local_clock) + 1
    if (*l_clock_loc > l_clock) {
        l_clock = *l_clock_loc;
    }
    l_clock++;

    *l_clock_loc = l_clock; // also updates the local clock received
    pthread_mutex_unlock(&mutex_lclock);    // unlock (o)
}

//-- returns the value of the global Lamport clock (l_clock)
int get_clock_lamport() {
    int l_clock_copy;

    pthread_mutex_lock(&mutex_lclock);      // lock (X)
    l_clock_copy = l_clock;                 // copy the l_clock value
    pthread_mutex_unlock(&mutex_lclock);    // unlock (o)

    return l_clock_copy; // return de la copia
}

//-- returns an empty message (allocates memory)
struct message* create_empty_msg() {
    DEBUG_PRINTF("[!] creating empty msg...\n");
    struct message *msg = malloc(sizeof(struct message));
    return msg;
}

//-- creates a message with the corresponding origin, action and clock values
struct message* create_msg(const char *origin, enum operations action, unsigned int clock) {
    struct message *msg = create_empty_msg();

    memset(msg->origin, 0, sizeof(msg->origin));

    strncpy(msg->origin, origin, sizeof(msg->origin) - 1);  // copy the origin
    msg->action = action;                                   // add the action
    msg->clock_lamport = clock;                             // set the clock

    DEBUG_PRINTF("[!] MALLOC OF <%s, %u> MSG\n", msg->origin, msg->clock_lamport);

    return msg;
}

//-- frees the memory allocated for the msg it receives as parameter
void free_msg(struct message* msg) {
    DEBUG_PRINTF("[!] FREE OF <%s, %u> MSG\n", msg->origin, msg->clock_lamport);
    free(msg);
}

//-- sends a struct message via the connection indicated by conn_fd
int send_through_socket(int conn_fd, struct message *msg) {
    DEBUG_PRINTF("[sts] inside send_through_socket(), conn_fd = %i\n", conn_fd);
    DEBUG_PRINTF("[sts] socket status is %i and should be %i\n", sock_status, SOCKET_RUNNING);

    if (send(conn_fd, msg, sizeof(struct message), 0) < 0) {
        perror("send failed");
        return F_FAILURE;
    }

    return F_SUCCESS;
}

//-- converts an action to a readable string
char *action_to_str(enum operations action) {
    if (action == READY_TO_SHUTDOWN) {
        return "READY_TO_SHUTDOWN";
    }
    if (action == SHUTDOWN_NOW) {
        return "SHUTDOWN_NOW";
    }
    if (action == SHUTDOWN_ACK) {
        return "SHUTDOWN_ACK";
    }
    return "UNKNOWN OPERATION";
}

//-- sends a message with an action from PX to PY using sockets underneath
int send_msg(const char *from, const char *to, enum operations action) {
    int sts_status, l_clock_loc, connection_fd = sock_sfd;
    char *action_string;

    // get lamport clock and update it BEFORE sending the message
    l_clock_loc = get_clock_lamport();
    update_clock_lamport(&l_clock_loc);

    // create the message to send with the proper origin, action and clock
    struct message *msg = create_msg(from, action, get_clock_lamport());

    // if server is the sender, updates connection_fd to the addressee (receiver)
    if (strcmp(to, "P1") == 0) {
        connection_fd = cli_1_cfd;
    }
    if (strcmp(to, "P3") == 0) {
        connection_fd = cli_3_cfd;
    }

    // print send trace as: "PX, contador_lamport, SEND, operations"
    action_string = action_to_str(action);
    printf("%s, %i, SEND, %s\n", from, l_clock_loc, action_string);
    
    // in case of error, send_through_socket() prints the error message
    sts_status = send_through_socket(connection_fd, msg);
    if (sts_status == F_FAILURE) {
        return F_FAILURE;
    }

    return F_SUCCESS;
}

//-- updates cli_1_cfd or cli_3_cfd when necessary
void associate_if_server(int conn_fd, char *origin) {
    DEBUG_PRINTF("SERVER ASOCIATING:\n");
    // associates "P1" or "P3" to its connection fd as needed

    if (strcmp(origin, "P1") == 0 && cli_1_cfd != conn_fd) {
        DEBUG_PRINTF(">>> ASOCIATION DONE: P1 is connection fd %i\n", conn_fd);
        cli_1_cfd = conn_fd;        // P3 association
    } else {
        if (strcmp(origin, "P3") == 0 && cli_3_cfd != conn_fd) {
            DEBUG_PRINTF(">>> ASOCIATION DONE: P3 is connection fd %i\n", conn_fd);
            cli_3_cfd = conn_fd;    // P3 association
        }
    }
}

//-- (used by threads!) blocks until a message is received
int receive_msg(int conn_fd, struct message *msg) {
    DEBUG_PRINTF("[rcvm] inside receive_msg() function:\n");

    int bytes_received = recv(conn_fd, msg, sizeof(struct message), 0);
    if (bytes_received < 0) {
        perror("[!] recv failed");
        return F_FAILURE;
    }

    if (bytes_received == 0) {
        sock_status = SOCKET_CLOSED;
        DEBUG_PRINTF("[!] connection with %i was closed by SHUTDOWN\n", conn_fd);
        return F_CONN_CLOSE;    // return F_CONN_CLOSE when connection is closed
    }
    
    associate_if_server(conn_fd, msg->origin); // only server really uses this

    DEBUG_PRINTF("[!] SUCCESS: received from %s with clock %u\n", msg->origin, msg->clock_lamport);

    return F_SUCCESS;
}

//-- (server only!) closes the server socket and terminates with indicated status
void terminate_server(int exit_status) {
    int final_clock = get_clock_lamport();
    printf("Los clientes fueron correctamente apagados en t(lamport) = %i\n", final_clock);

    // waits for all threads with pthread join
    while (conn_count > 0) {
        pthread_join(conn_threads[conn_count - 1], NULL);
        conn_count--;
    }

    sock_status = SOCKET_CLOSED;
    close(sock_sfd);

    exit(exit_status);
}

//-- (server only!) function called by a thread, receives in loop
int server_listening(int *cfd) {
    char *action_string;
    int l_clock_loc, recv_status = F_SUCCESS, conn_fd = *((int*)cfd);
    struct message *buffer_msg = create_empty_msg();

    free(cfd);

    DEBUG_PRINTF(" (!thread) SERVER LISTENING\n");

    // while server has not received 1 SHUTDOWN_ACK for each client and socket is RUNNING
    while (shutdown_acks < NUM_OF_CLIENTS && sock_status == SOCKET_RUNNING) {
        recv_status = receive_msg(conn_fd, buffer_msg);
        DEBUG_PRINTF(" (!thread) recv() clear with status %i and it should be %i\n", recv_status, F_SUCCESS);

        // in case recv_status is not F_SUCCESS
        if (recv_status < 0) {
            DEBUG_PRINTF(" (!thread) receive_msg() terminated with status %i, exiting loop", recv_status);
            break;
        }

        // update Lamport clock after the receive
        l_clock_loc = buffer_msg->clock_lamport;
        update_clock_lamport(&l_clock_loc);

        // print recv trace as: "PX, contador_lamport, RECV (PY), operations"
        action_string = action_to_str(buffer_msg->action);
        printf("%s, %i, RECV (%s), %s\n", stub_whoami, l_clock_loc, buffer_msg->origin, action_string);

        if (buffer_msg->action == SHUTDOWN_ACK) {
            pthread_mutex_lock(&mutex_shutack);     // lock (X)
            shutdown_acks++;                        // update shutdown_acks
            pthread_mutex_unlock(&mutex_shutack);   // unlock (o)
        }
        DEBUG_PRINTF(" (!thread) loop...\n...\n");
    }

    free_msg(buffer_msg);   // free the message struct reserved previously
    close(conn_fd);         // close the connection (of this thread)
    return recv_status;     // return
}

//-- (server only!) inits the server with its fd and creates one thread per client
void start_up_server(int argc, char *argv[], char *whoami) {
    int port, conn_fd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    char *server_ip;

    stub_whoami = whoami;   // as it enters, updates global stub_whoami

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // does several verifications while initializing the server...
    if (check_argnum(argc) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    port        = try_get_port(argv[2]);
    server_ip   = argv[1];
    if (port == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    sock_sfd = init_socket(&servaddr, server_ip, port); // establishes sock_sfd
    if (sock_sfd == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINTF("INIT CLEAR\n");
    DEBUG_PRINTF("-> socket has sock_sfd = %i\n", sock_sfd);

    enable_setsockopt(sock_sfd);

    if (bind_and_listen(sock_sfd, &servaddr) == F_FAILURE) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINTF("BIND AND LISTEN CLEAR\n");
    DEBUG_PRINTF("-> conn_count  = %i\n", conn_count);
    DEBUG_PRINTF("-> sock_status = %i\n", sock_status);
    
    signal(SIGINT, handle_sigint);

    // creates new threads until it is 1 per client specified (NUM_OF_CLIENTS)
    while (conn_count < NUM_OF_CLIENTS && sock_status == SOCKET_RUNNING) {

        // accept new client 
        conn_fd = accept(sock_sfd, (struct sockaddr*)&cliaddr, &cliaddr_len);
        if (conn_fd < 0) {
            perror("accept failed");
            continue;
        }
        conn_count++;   // update connection counter

        DEBUG_PRINTF("NEW CONNECTION ACCEPTED: %i\n", conn_fd);

        // copy the connection fd to a pointer (malloc needed)
        int *conn_fd_ptr = malloc(sizeof(int));
        if (conn_fd_ptr == NULL) {
            perror("malloc failed");
            close(conn_fd);
            conn_count--;
            continue;
        }
        *conn_fd_ptr = conn_fd;

        // new thread to handle the accepted connection
        if (pthread_create(&conn_threads[conn_count - 1], NULL, 
                            (void*)server_listening, (void*)conn_fd_ptr) != 0) {
            free(conn_fd_ptr);
            perror("pthread_create failed");
            close(conn_fd);
            continue;
        }
    }
}

//-- (client only!) loses the client fd and terminates
void terminate_client(int exit_status) {
    pthread_join(client_thread, NULL);  // waits first for the receiver thread

    sock_status = SOCKET_CLOSED;
    close(sock_sfd);

    exit(exit_status);
}

//-- (server only!) function called by a thread, receives until SHUTDOWN_NOW
int client_listening() {
    char *action_string;
    int l_clock_loc, recv_status;
    struct message *buffer_msg = create_empty_msg();

    DEBUG_PRINTF(" (!thread) CLIENT LISTENING with sock_status = %i and should be %i\n", sock_status, SOCKET_RUNNING);

    recv_status = F_SUCCESS;
    // While receive is succeeding, keeps repeating this action
    while (recv_status == F_SUCCESS && sock_status == SOCKET_RUNNING) {
        recv_status = receive_msg(sock_sfd, buffer_msg);
        DEBUG_PRINTF(" (!thread) recv() clear with status %i and it should be %i\n", recv_status, F_SUCCESS);

        // update Lamport clock after the receive
        l_clock_loc = buffer_msg->clock_lamport;
        update_clock_lamport(&l_clock_loc);

        // print recv trace as: "PX, contador_lamport, RECV (PY), operations"
        action_string = action_to_str(buffer_msg->action);
        printf("%s, %i, RECV (%s), %s\n", stub_whoami, l_clock_loc, buffer_msg->origin, action_string);

        // when SHUTDOWN_NOT is received, terminate thread execution (break loop)
        if (buffer_msg->action == SHUTDOWN_NOW) {
            break;
        }
    }

    free_msg(buffer_msg);   // free the message struct reserved previously
    return recv_status;     // return (do not close sock_sfd before this!)
}

//-- (client only!) inits the client with its fd and creates a receiver thread
void start_up_client(int argc, char *argv[], char *whoami) {
    int port;
    struct sockaddr_in servaddr;
    char *server_ip;

    stub_whoami = whoami;   // as it enters, updates global stub_whoami

    // disable buffering when printing messages
    setbuf(stdout, NULL);

    // does several verifications while initializing the client...
    if (check_argnum(argc) == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    port        = try_get_port(argv[2]);
    server_ip   = argv[1];
    if (port == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    sock_sfd = init_socket(&servaddr, server_ip, port); // establishes sock_sfd
    if (sock_sfd == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINTF("INIT CLEAR\n");
    DEBUG_PRINTF("-> socket has sock_sfd = %i\n", sock_sfd);

    if (connect_to_server(sock_sfd, &servaddr) == F_FAILURE) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    DEBUG_PRINTF("CONNECT TO SERVER CLEAR\n");
    
    signal(SIGINT, handle_sigint);

    // Create a SINGLE thread for listening the messages sent from the server
    if (pthread_create(&client_thread, NULL, 
                        (void*)client_listening, (void*)NULL) != 0) {
        perror("pthread_create failed");
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }
}
