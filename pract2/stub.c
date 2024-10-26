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


#define PORT                8073

#define SOCKET_RUNNING      1
#define SOCKET_CLOSED       -1

#define F_FAILURE           -1
#define F_SUCCESS           0
#define F_CONN_CLOSE        -3

// WR stands for wait_receive (first words of a function below)
#define WR_SUCCESS          0
#define WR_FAILURE          -1
#define WR_NTR              -2  // NTR stands for Nothing To Read

#define SOCK_EXIT_SUCCESS   10
#define SOCK_EXIT_FAILURE   11
#define SOCK_EXIT_SIGINT    12

#define NUM_OF_CLIENTS      1


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


// global variables
int sock_status     = 0;    // when socket is created, it's set to SOCKET_RUNNING
                            // and when socket is closed, set to SOCKET_CLOSED
int sock_exit       = 0;
int l_clock         = 0;

int sock_sfd        = 0;

int shutdown_acks   = 0;


//-- Handles SIGINT signals changing the value of sock_status when received
void 
handle_sigint(int sig)
{
    printf("\nSocket shutdown received (CTRL+C)...\n");

    if (sock_status == SOCKET_RUNNING) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
    }
    exit(SOCK_EXIT_SIGINT);
}

//-- Prints an error message along the perror information and ends the execution
void 
perror_msg(char *msg, int sockfd, int is_socket_running)
{
    perror(msg);

    if (is_socket_running) {
        DEBUG_PRINTF("Closing socket after perror\n");
        close(sockfd);
    }
}

//-- Calls perror_msg with SOCKET_RUNNING as third parameter
#define perror_msg_sr(msg, sockfd) perror_msg(msg, sockfd, SOCKET_RUNNING)

//-- Returns F_SUCCESS in case argn is 3, and F_FAILURE otherwise
int
check_argnum(int argn)
{
    if(argn != 3) {
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- Tries to get the port (string parameter) and convert it to int
// (fails when detecting bad format)
int
try_get_port(char *str_port)
{
    char *endptr;
    long int li_port;

    // Get long int from str and look for possible failures (bad format)
    li_port = strtol(str_port, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0') {
        fprintf(stderr, "error: non-valid port (bad format)\n");
        return F_FAILURE;
    }
    return (int)li_port;
}

//-- Starts up a new socket, returns its fd when success, F_FAILURE otherwise
// (receives server IP and port as 2nd and 3rd parameter)
int 
init_socket(struct sockaddr_in *servaddr, char *serv_ip, int serv_port)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror_msg("Error creating socket", sock_fd, SOCKET_CLOSED);
        return F_FAILURE;
    }
    printf("Socket successfully created...\n");

    servaddr->sin_family = AF_INET;
    if (inet_pton(AF_INET, serv_ip, &(servaddr->sin_addr)) <= 0) {
        perror_msg_sr("Invalid address / Address not supported", sock_fd);
        return F_FAILURE;
    }
    servaddr->sin_port = htons(serv_port);

    return sock_fd;
}

//-- forces near-instant reuse of ports [FOR THE SERVER ONLY !]
void
enable_setsockopt(int serv_sfd)
{
    const int ENABLE_SSOPT = 1;

    if (setsockopt(serv_sfd, SOL_SOCKET, SO_REUSEADDR, &ENABLE_SSOPT, sizeof(int)) < 0) {
        perror_msg_sr("setsockopt(SO_REUSEADDR) failed\n", serv_sfd);
    }
}

//-- Bind and listen on the server socket
int
bind_and_listen(int serv_sfd, struct sockaddr_in *servaddr) 
{
    if (bind(serv_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_msg_sr("bind failed", serv_sfd);
        return F_FAILURE;
    }
    printf("Socket successfully binded...\n");

    if (listen(serv_sfd, 1) < 0) {
        perror_msg_sr("listen failed", serv_sfd);
        return F_FAILURE;
    }

    printf("Server listening...\n");
    return F_SUCCESS;
}

//-- Aceepts a client and returns the connection fd created for the server
int
accept_client(int serv_sfd, struct sockaddr_in *cliaddr)
{
    socklen_t cliaddr_len = sizeof(*cliaddr);

    int conn_fd = accept(serv_sfd, (struct sockaddr * ) cliaddr, &cliaddr_len);

    if (conn_fd < 0) {
        perror_msg_sr("accept failed", serv_sfd);
        return F_FAILURE;
    }

    return conn_fd;
}

//-- Tries to connect the client to the server (fails unless the server is up)
int
connect_to_server(int cli_sfd, struct sockaddr_in *servaddr)
{
    if (connect(cli_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_msg_sr("connect error", cli_sfd);
        return F_FAILURE;
    }

    printf("connected to the server...\n");
    return F_SUCCESS;
}

//-- Returns an empty message (allocates memory)
struct message*
create_empty_msg()
{
    DEBUG_PRINTF("[!] creating empty msg...\n");
    struct message *msg = malloc(sizeof(struct message));
    return msg;
}

//-- Creates a message with the corresponding origin, action and clock values
struct message*
create_msg(const char *origin, enum operations action, unsigned int clock)
{
    struct message *msg = create_empty_msg();

    memset(msg->origin, 0, sizeof(msg->origin));

    strncpy(msg->origin, origin, sizeof(msg->origin) - 1);   // copy the origin
    msg->action = action;                                    // add the action
    msg->clock_lamport = clock;                            // set the clock

    DEBUG_PRINTF("[!] MALLOC OF <%s, %u> MSG\n", msg->origin, msg->clock_lamport);

    return msg;
}

//-- Sends a message (msg) via the connection indicated by conn_fd
int
send_through_socket(int conn_fd, struct message *msg)
{
    if (send(conn_fd, msg, sizeof(struct message), 0) < 0) {
        perror("send failed");
        return F_FAILURE;
    }
    return F_SUCCESS;
}

int
send_msg(const char *origin, enum operations action)
{
    struct message *msg = create_msg(origin, action, get_clock_lamport());
    return 0;
}

//-- Blocks until a message is received
int
receive_msg(int conn_fd, struct message *msg) 
{
    DEBUG_PRINTF("[SOCKET RECEIVING]\n");

    int bytes_received = recv(conn_fd, msg, sizeof(struct message), 0);
    if (bytes_received < 0) {
        perror("recv failed");
        return F_FAILURE;
    }

    if (bytes_received == 0) {
        // if the connection is closed by the other peer, update status
        sock_status = SOCKET_CLOSED;
        return F_CONN_CLOSE;    // return F_CONN_CLOSE when connection is closed
    }
    
    DEBUG_PRINTF("[!] RECEIVED <%s, %u> MSG\n", msg->origin, msg->clock_lamport);

    return F_SUCCESS;
}

// Prints a struct message with the format: < PX, operation, clock >
// [tocara sustituir esta funcion ****]
void
print_msg(struct message *msg)
{
    char *action_str = malloc(20 * sizeof(char));

    switch (msg->action) {
        case READY_TO_SHUTDOWN: strncpy(action_str, "READY TO SHUTDOWN", 19); break;
        case SHUTDOWN_NOW:      strncpy(action_str, "SHUTDOWN NOW", 19); break;
        case SHUTDOWN_ACK:      strncpy(action_str, "SHUTDOWN ACK", 19); break;
        default:                strncpy(action_str, "-- unknown --", 19); break;
    }

    printf("%s, %s, %u\n", msg->origin, action_str, msg->clock_lamport);
    free(action_str);
}

// Frees the memory allocated for the parameter message (struct)
void
free_msg(struct message* msg)
{
    DEBUG_PRINTF("[!] FREE OF <%s, %u> MSG\n", msg->origin, msg->clock_lamport);
    free(msg);
}

//-- Updates the global lamport clock with the current local value received
// UPDATE FORMULA: global_clock = max(global_clock, local_clock) + 1
void
update_clock_lamport(int *l_clock_loc)
{
    // mutex lock
    if(*l_clock_loc > l_clock) {
        l_clock = *l_clock_loc;
    }
    l_clock++;

    *l_clock_loc = l_clock;
    // mutex unlock
}

//-- Returns the value of the lamport clock (global variable)
int
get_clock_lamport()
{
    // lock
    // copiar valor
    // unlock 
    return l_clock; // return de la copia
}


//-- Last function to call from the server (p2), closes the socket and exits
void
terminate_server(int exit_status)
{
    sock_status = SOCKET_CLOSED;
    close(sock_sfd);

    exit(exit_status);
}

//-- Executes one receive after another, until sock_status is SOCKET_CLOSED,
// recv() returns F_FAILURE, or server has received one SHUTDOWN_ACK per client
int
server_listening(int *cfd)
{
    int l_clock_loc, recv_status, conn_fd = *((int*)cfd);
    struct message *buffer_msg = create_empty_msg();

    free(cfd);

    DEBUG_PRINTF("(!thread) SERVER LISTENING\n");

    recv_status = F_SUCCESS;
    // while the server has not received 1 SHUTDOWN_ACK for each client
    while (shutdown_acks < NUM_OF_CLIENTS && sock_status == SOCKET_RUNNING) {
        DEBUG_PRINTF("(!thread) before recv() call...\n");
        recv_status = receive_msg(conn_fd, buffer_msg);
        DEBUG_PRINTF("(!thread) recv() clear with status %i\n", recv_status);
        if (recv_status == F_FAILURE) {
            break;
        }

        l_clock_loc = buffer_msg->clock_lamport;
        update_clock_lamport(&l_clock_loc);
        DEBUG_PRINTF("(!thread) clock lamport (global l_clock) currently is <%i>\n", l_clock);

        if (buffer_msg->action == SHUTDOWN_ACK) {
            shutdown_acks++;
        }
        DEBUG_PRINTF("(!thread) loop\n...\n");
    }

    close(conn_fd);
    return recv_status;
}

//-- Initializes the server with its sock_fd (global) and creates 
// one thread per client, calling server_listening() for each thread
int
start_up_server(int argc, char *argv[])
{
    int port, conn_fd, conn_count;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    char *server_ip;
    pthread_t conn_threads[NUM_OF_CLIENTS];

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // does several verifications while initializing the server
    if (check_argnum(argc) == F_FAILURE) {
        return SOCK_EXIT_FAILURE;
    }

    port        = try_get_port(argv[2]);
    server_ip   = argv[1];
    if (port == F_FAILURE) {
        return SOCK_EXIT_FAILURE;
    }

    sock_sfd = init_socket(&servaddr, server_ip, port);
    if (sock_sfd == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    enable_setsockopt(sock_sfd);

    if (bind_and_listen(sock_sfd, &servaddr) == F_FAILURE) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    signal(SIGINT, handle_sigint);

    DEBUG_PRINTF("BIND AND LISTEN CLEAR, now entering while loop\n")

    // Creates new threads while conn_count is below 100 (max specified)
    while (conn_count < NUM_OF_CLIENTS && sock_status == SOCKET_RUNNING) {

        // Accept a new client 
        conn_fd = accept(sock_sfd, (struct sockaddr*)&cliaddr, &cliaddr_len);
        if (conn_fd < 0) {
            perror("accept failed");
            continue;
        }

        conn_count++;   // update connection counter
        DEBUG_PRINTF("NEW CONNECTION ACCEPTED: %i\n", conn_fd);

        // Copy the connection fd to a pointer (malloc needed)
        int *conn_fd_ptr = malloc(sizeof(int));
        if (conn_fd_ptr == NULL) {
            perror("malloc failed");
            close(conn_fd);
            conn_count--;
            continue;
        }

        *conn_fd_ptr = conn_fd;

        // New thread to handle the accepted connection
        if (pthread_create(&conn_threads[conn_count - 1], NULL, 
                            (void*)server_listening, (void*)conn_fd_ptr) != 0) {
            free(conn_fd_ptr);
            perror("pthread_create failed");
            close(conn_fd);
            continue;
        }

    }

    // When count reaches its max, wait for all threads with pthread join
    while (conn_count > 0) {
        pthread_join(conn_threads[conn_count - 1], NULL);
        conn_count--;
    }

    close(sock_sfd);
    return SOCK_EXIT_SUCCESS;
}

//-- Executes one receive after another, until sock_status is SOCKET_CLOSED,
// recv() returns F_FAILURE or F_CONN_CLOSED or a SHUTDOWN_NOW is received
int
client_listening(int *cfd)
{
    int l_clock_loc, recv_status, conn_fd = *((int*)cfd);
    struct message *buffer_msg = create_empty_msg();

    free(cfd);

    DEBUG_PRINTF("(!thread) CLIENT LISTENING\n");

    recv_status = F_SUCCESS;
    // While receive is succeeding, keeps repeating this action
    while (recv_status > 0 && sock_status == SOCKET_RUNNING) {
        DEBUG_PRINTF("(!thread) before recv() call...\n");
        recv_status = receive_msg(conn_fd, buffer_msg);
        DEBUG_PRINTF("(!thread) recv() clear with status %i\n", recv_status);

        l_clock_loc = buffer_msg->clock_lamport;
        update_clock_lamport(&l_clock_loc);
        DEBUG_PRINTF("(!thread) clock lamport (global l_clock) currently is <%i>\n", l_clock);

        if (buffer_msg->action == SHUTDOWN_NOW) {
            break;
        }
    }

    return recv_status;
}

//-- Initializes the client with its sock_fd (global) and creates 
// a listening thread for it, calling client_listening()
int
start_up_client(int argc, char *argv[])
{
    int port;
    struct sockaddr_in servaddr;
    char *server_ip;
    pthread_t client_thread;

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // does several verifications while initializing the client
    if (check_argnum(argc) == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    port        = try_get_port(argv[2]);
    server_ip   = argv[1];
    if (port == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    sock_sfd = init_socket(&servaddr, server_ip, port);
    if (sock_sfd == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    if (connect_to_server(sock_sfd, &servaddr) == F_FAILURE) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    DEBUG_PRINTF("CONNECT TO SERVER CLEAR, now proceeding to malloc and pthread\n");
    
    signal(SIGINT, handle_sigint);

    // Copy the connection fd to a pointer (malloc needed)
    int *conn_fd_ptr = malloc(sizeof(int));
    if (conn_fd_ptr == NULL) {
        perror("malloc failed");
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    *conn_fd_ptr = sock_sfd;

    // Create a SINGLE thread for listening the messages sent from the server
    if (pthread_create(&client_thread, NULL, 
                        (void*)client_listening, (void*)conn_fd_ptr) != 0) {
        free(conn_fd_ptr);
        perror("pthread_create failed");
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        return SOCK_EXIT_FAILURE;
    }

    pthread_join(client_thread, NULL);

    // Terminates the client socket execution (closes the socket fd)
    close(sock_sfd);
    sock_status = SOCKET_CLOSED;

    free(conn_fd_ptr);
    return SOCK_EXIT_SUCCESS;
}



//--
//--- EOF