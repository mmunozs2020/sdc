#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
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
#include <semaphore.h>
#include <time.h>
#include <stdint.h>


#ifdef DEBUG
    #define DEBUG_PRINTF(...) fprintf(stdout, "DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif


// CONSTANT VALUES:
#define SOCKET_RUNNING      1
#define SOCKET_CLOSED       -1

#define F_FAILURE           -1  // function failed
#define F_SUCCESS           0   // function succeded
#define F_CONN_CLOSE        -3  // recv() returns 0 bytes read

#define STUB_EXIT_SIGINT    12  // exit status when terminating by sigint

#define MAX_SERVER_THREADS  600
#define MAX_BACKLOG         1024
#define RW_BUFFER_SIZE      32  // buffer size to readers and writers

#define WR_IN               2
#define WR_OUT              -2


// ENUMS AND STRUCTS:
enum operations {
    WRITE = 0,
    READ
};

struct request {
    enum operations action;
    unsigned int id;
};

struct response {
    enum operations action;
    unsigned int counter;
    long latency_time;
};

struct client_data {
    int conn_fd;
    unsigned int id;
};


// GLOBAL VARIABLES:
int holy_counter    = 0;    // server's internal counter. Has to be protected at all costs

    // options
int port            = 0;
char *serv_pri      = NULL;
char *ip            = NULL;
char *cli_mode      = NULL;
int cli_threads     = 0;

    // sockets & connections
int sock_status     = 0;
int sock_sfd        = 0;
int conn_cfds[MAX_SERVER_THREADS];  // (server only!) stores all connection fds with clients
int conn_index      = 0;            // protected by conn_index (ci!>)

    // semaphores, mutexes & cond variables
sem_t conn_sem;
pthread_mutex_t conn_mutex = PTHREAD_MUTEX_INITIALIZER; // to protect conn_index
pthread_mutex_t clid_mutex = PTHREAD_MUTEX_INITIALIZER; // to protect client_data 
pthread_mutex_t rwait_mutex = PTHREAD_MUTEX_INITIALIZER; // to protect readers_waiting
pthread_mutex_t wwait_mutex = PTHREAD_MUTEX_INITIALIZER; // to protect writers_waiting
pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;   // to protect read and write operations
pthread_cond_t r_cond = PTHREAD_COND_INITIALIZER;       //  -protected by rw_mutex
pthread_cond_t w_cond = PTHREAD_COND_INITIALIZER;       //  -protected by rw_mutex

    // thread 'queues'
int writers_waiting = 0;
int readers_waiting = 0;
int readers_in      = 0;
int writer_in       = 0;

// FUNCTION DEFINITION:

//-- prints an error message along the perror info and updates the socket status
void perror_msg(char *msg, int sockfd, int is_socket_running) {
    perror(msg);

    if (is_socket_running) {
        DEBUG_PRINTF("Closing socket after perror\n");
        close(sockfd);
        sock_status = SOCKET_CLOSED;
    }
}

#define perror_msg_sr(msg, sockfd) perror_msg(msg, sockfd, SOCKET_RUNNING)
#define perror_msg_cl(msg) perror_msg(msg, 0, SOCKET_CLOSED)

//-- handles sigint signals when received
void handle_sigint(int sig) {
    if (sock_status == SOCKET_RUNNING) {    // closes the socket if necessary
        close(sock_sfd);
        DEBUG_PRINTF("---------------- SOCKET CLOSED ----------------\n");
        sock_status = SOCKET_CLOSED;
    }
    exit(STUB_EXIT_SIGINT);
}


//-- tries to get a string parameter and convert it to int
int get_int_from_char(char *str_port) {
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

//-- gets client's necessary args
int get_cli_args(int argc, char **argv) {
    int op;
    int index = 0;
    struct option cli_options[] = {
        {"ip",      required_argument, 0, 'i'},
        {"port",    required_argument, 0, 'p'},
        {"mode",    required_argument, 0, 'm'},
        {"threads", required_argument, 0, 't'},
        {0, 0, 0, 0}
    };  // Required client arguments (ip, port, mode and num of threads)

    // Parse through all possible options 
    while ((op = getopt_long(argc, argv, "i:p:m:t:", cli_options, &index)) != -1) {
        switch (op) {
            case 'i':
                ip = strdup(optarg);
                break;
            case 'p':
                port = get_int_from_char(optarg);
                break;
            case 'm':
                cli_mode = strdup(optarg);
                break;
            case 't':
                cli_threads = get_int_from_char(optarg);
                break;
            default:
                // any other option causes failure after printing usage
                fprintf(stderr, 
                        "usage: %s --ip IP --port PORT --mode writer/reader --threads 100\n", argv[0]);
                return F_FAILURE;
        }
    }

    if (port == F_FAILURE || cli_threads == F_FAILURE) {
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- gets server's necessary args
int get_serv_args(int argc, char **argv) {
    int op;
    int index = 0;
    struct option serv_options[] = {
        {"port",        required_argument, 0, 'p'},
        {"priority",    required_argument, 0, 'q'},
        {0, 0, 0, 0}
    };  // Required server arguments (ip, port and priority)

    // Parse through all possible options 
    while ((op = getopt_long(argc, argv, "p:q:", serv_options, &index)) != -1) {
        switch (op) {
            case 'p':
                port = get_int_from_char(optarg);
                break;
            case 'q':
                serv_pri = strdup(optarg);
                break;
            default:
                fprintf(stderr, 
                        "usage: %s --port PORT --priority writer/reader\n", argv[0]);
                return F_FAILURE;
        }
    }

    if (port == F_FAILURE || cli_threads == F_FAILURE) {
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- frees all memory stored for the server / client args
void free_args() {
    free(ip);
    if (cli_threads != 0) {
        free(cli_mode);
    }
}


//-- starts up a new socket, returns its fd when success, F_FAILURE otherwise
int init_socket(struct sockaddr_in *servaddr, int serv_port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0) {
        perror_msg("Error creating socket", sock_fd, SOCKET_CLOSED);
        return F_FAILURE;
    }

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

    // high backlog to manage as many clients as possible
    if (listen(serv_sfd, MAX_BACKLOG) < 0) {
        perror_msg_sr("listen failed", serv_sfd);
        return F_FAILURE;
    }

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
    return F_SUCCESS;
}


//-- returns a new request struct
struct request create_req(enum operations action, unsigned int id) {
    struct request req;

    req.action = action;
    req.id = id;
    return req;
}

#define create_empty_req() create_req(0, 0)

//-- returns a new response
struct response create_resp(enum operations action, unsigned int count, long lat) {
    struct response resp;

    resp.action = action;
    resp.counter = count;
    resp.latency_time = lat;
    return resp;
}

#define create_empty_resp() create_resp(0, 0, 0)

//-- returns the action corresponding to the mode set for the client
enum operations get_action_from_mode() {
    if (strcmp(cli_mode, "reader") == 0) {
        return READ;
    }
    if (strcmp(cli_mode, "writer") == 0) {
        return WRITE;
    }
    return 0;
}

//-- converts an action to a readable string
char *action_to_str(enum operations action) {
    if (action == READ) {
        return "Lector";
    }
    if (action == WRITE) {
        return "Escritor";
    }
    return "Saboteador";
}

//-- returns the latency calculated from beginning to ending (in nanoseconds)
long get_latency(const struct timespec *beginning, const struct timespec *ending) {
    long lat_sec = ending->tv_sec - beginning->tv_sec;
    long lat_nano = ending->tv_nsec - beginning->tv_nsec;

    if (lat_nano < 0) {
        lat_sec--;
        lat_nano += 1000000000;
    }

    return lat_sec * 1000000000 + lat_nano;
}


//-- (threads!) sends a struct REQUEST via the connection indicated by conn_fd
int send_req_through(int conn_fd, struct request *req) {
    if (send(conn_fd, req, sizeof(struct request), 0) < 0) {
        perror("send failed");
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- (threads!) sends a struct RESPONSE via the connection indicated by conn_fd
int send_resp_through(int conn_fd, struct response *resp) {
    ssize_t sent;

    sent = send(conn_fd, resp, sizeof(struct response), 0);
    if (sent < 0) {
        perror("send failed");
        return F_FAILURE;
    }

    return F_SUCCESS;
}

//-- (threads!) blocks until a REQUEST is received
int receive_req(int conn_fd, struct request *req) {
    int bytes_received = recv(conn_fd, req, sizeof(struct request), 0);

    if (bytes_received < 0) {
        perror("[!] recv failed");
        return F_FAILURE;
    }

    if (bytes_received == 0) {
        return F_CONN_CLOSE;
    }

    return F_SUCCESS;
}

//-- (threads!) blocks until a RESPONSE is received
int receive_resp(int conn_fd, struct response *resp) {
    int bytes_received = recv(conn_fd, resp, sizeof(struct response), 0);

    if (bytes_received < 0) {
        perror("[!] recv failed");
        return F_FAILURE;
    }

    if (bytes_received == 0) {
        return F_CONN_CLOSE;
    }

    return F_SUCCESS;
}


//-- function called from a server thread to read a protected value
int do_read(int reader_id) {
    int loc_hc;
    char rbuff[RW_BUFFER_SIZE];
    FILE *server_output;
    struct timespec now;

    pthread_mutex_lock(&rw_mutex);      // crit reader entrance -{
    // wait if (there are writers inside OR there are writers waiting and they have priority)
    while (writer_in != 0 || (writers_waiting > 0 && strcmp(serv_pri, "writer") == 0)) {
        pthread_cond_wait(&r_cond, &rw_mutex);
    }
    readers_in++;
    pthread_mutex_unlock(&rw_mutex);    // }- crit reader entrance

    // ## CRITICAL REGION ##
    server_output = fopen("server_output.txt" ,"r");        // READ -r-{
    if (fgets(rbuff, sizeof(rbuff), server_output) != NULL) {
        loc_hc = strtol(rbuff, NULL, 10);
    } else {
        loc_hc = -1;
    }
    fclose(server_output);                                  // }-r- READ
    // ## EOCR ##
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    fprintf(stdout, "[%li.%li][LECTOR %i] lee contador con valor %i\n", now.tv_sec, now.tv_nsec, reader_id, loc_hc);
    usleep((rand() % (150000 - 75000 + 1)) + 75000);

    pthread_mutex_lock(&rw_mutex);      // }- crit reader exit
    readers_in--;
    if (readers_in == 0) {
        pthread_cond_signal(&w_cond);
    }
    pthread_mutex_unlock(&rw_mutex);    // }- crit reader entrance
    return loc_hc;
}

//-- function called from a server thread to overwrite a protected value
int do_write(int writer_id) {
    int loc_hc;
    char wbuff[RW_BUFFER_SIZE];
    FILE *server_output;
    struct timespec now;

    pthread_mutex_lock(&rw_mutex);      // crit writer entrance -{
    // wait if (there are writers or readers inside OR there are readers waiting and they have priority)
    while (writer_in != 0 || readers_in != 0 || (readers_waiting > 0 && strcmp(serv_pri, "reader") == 0)) {
        pthread_cond_wait(&w_cond, &rw_mutex);
    }
    writer_in++;
    pthread_mutex_unlock(&rw_mutex);    // }- crit writer entrance

    // ## CRITICAL REGION ##
    server_output = fopen("server_output.txt", "r+");       // WRITE -w-{
    if (fgets(wbuff, sizeof(wbuff), server_output) != NULL) {
        loc_hc = strtol(wbuff, NULL, 10);
    } else {
        loc_hc = -1;
    }

    loc_hc++;   // increment counter after reading it from file
    fseek(server_output, 0, SEEK_SET);
    fprintf(server_output, "%d\n", loc_hc); // write loc_hc to server_output
    fclose(server_output);                                  // }-w- WRITE
    // ## EOCR ##

    clock_gettime(CLOCK_MONOTONIC, &now);
    fprintf(stdout, "[%li.%li][ESCRITOR #%i] modifica contador con valor %i\n", now.tv_sec, now.tv_nsec, writer_id, loc_hc);
    usleep((rand() % (150000 - 75000 + 1)) + 75000);

    pthread_mutex_lock(&rw_mutex);      // crit writer exit -{
    writer_in = 0;
    pthread_cond_broadcast(&r_cond);
    pthread_cond_signal(&w_cond);
    pthread_mutex_unlock(&rw_mutex);    // }- crit writer exit
    return loc_hc;
}

//-- action determinates the waiter type (r/w) and io_wait the direction (to wait / to go)
void waiting_room(enum operations action, int io) {
    if (action == READ) {
        if (io == WR_IN) {              // reader enters the waiting room
            pthread_mutex_lock(&rwait_mutex);
            readers_waiting++;
            pthread_mutex_unlock(&rwait_mutex);
        } else if (io == WR_OUT) {      // reader leaves the waiting room
            pthread_mutex_lock(&rwait_mutex);
            readers_waiting--;
            pthread_mutex_unlock(&rwait_mutex);
        }
    } else if (action == WRITE) {
        if (io == WR_IN) {              // writer enters the waiting room
            pthread_mutex_lock(&wwait_mutex);
            writers_waiting++;
            pthread_mutex_unlock(&wwait_mutex);
        } else if (io == WR_OUT) {      // writer leaves the waiting room
            pthread_mutex_lock(&wwait_mutex);
            writers_waiting--;
            pthread_mutex_unlock(&wwait_mutex);
        }
    }
}

//-- (server only!) closes the server socket and terminates with indicated status
void terminate_server(int exit_status) {
    sock_status = SOCKET_CLOSED;
    close(sock_sfd);
    free_args();
    exit(exit_status);
}

//-- (server threads!) waits for a new connection, handles it and closes its fd afterwards
int *server_handler() {
    int ccfd, cci, hc;
    long clat;
    struct request creq;
    struct response cresp;
    struct timespec lat_beginning, lat_ending;

    while (sock_status == SOCKET_RUNNING) {
        sem_wait(&conn_sem);    // wait for a new connection
        DEBUG_PRINTF("_> SEM POST RECEIVED: NEW CONNECTION\n");

        pthread_mutex_lock(&conn_mutex);    // protects conn_cfds[] and conn_index
        cci = conn_index - 1;
        ccfd = conn_cfds[cci];
        conn_cfds[cci] = 0;
        conn_index--;
        pthread_mutex_unlock(&conn_mutex);

        creq = create_empty_req();
        if (receive_req(ccfd, &creq) == F_SUCCESS) {    // ignores 0-byte receives
            clock_gettime(CLOCK_MONOTONIC, &lat_beginning);     // <> clock beginning

            waiting_room(creq.action, WR_IN);

            // critical access here
            hc = -1; // if hc remains -1, an error ocurred
            if (creq.action == READ) {
                hc = do_read(creq.id);
            } else if (creq.action == WRITE) {
                hc = do_write(creq.id);
            }

            waiting_room(creq.action, WR_OUT);

            clock_gettime(CLOCK_MONOTONIC, &lat_ending);        // <> clock ending
            clat = get_latency(&lat_beginning, &lat_ending);

            cresp = create_resp(creq.action, hc, clat);
            send_resp_through(ccfd, &cresp);
        }

        close(ccfd);    // close connection after handling
    }
    return NULL;
}

//-- (server only!) creates a thread pool for the server
void create_server_thread_pool() {
    pthread_t threads[MAX_SERVER_THREADS];
    int pool_index = 0;

    sem_init(&conn_sem, 0, 0);  // initializes connection semaphore
    while (pool_index < MAX_SERVER_THREADS) {
        pthread_create(&threads[pool_index], NULL, 
                        (void*)server_handler, (void*)NULL);
        pool_index++;
    }
}

//-- (server only!) executes continuously, managing clients as they arrive
void server_control_loop() {
    struct sockaddr_in cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    int new_cfd;

    create_server_thread_pool();    // thread creation

    while (sock_status == SOCKET_RUNNING) {
        new_cfd = accept(sock_sfd, (struct sockaddr*)&cliaddr, &cliaddr_len);

        if (new_cfd < 0) {
            perror("accept failed");
            continue;
        } else {
            pthread_mutex_lock(&conn_mutex);    // L[ conn_mutex ]
            conn_cfds[conn_index] = new_cfd;
            conn_index++;
            pthread_mutex_unlock(&conn_mutex);  // U[ conn_mutex ]

            sem_post(&conn_sem);    // advertise new connection (1 thread manages it)
        }
    }
}

//-- (server only!) inits the server with its fd and creates one thread per client
void start_up_server(int argc, char *argv[]) {
    struct sockaddr_in servaddr;

    setbuf(stdout, NULL);
    srand(time(NULL));

    // does several verifications while initializing the server...
    if (get_serv_args(argc, argv) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    sock_sfd = init_socket(&servaddr, port); // establishes sock_sfd
    if (sock_sfd == F_FAILURE) {
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }

    enable_setsockopt(sock_sfd);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);

    if (bind_and_listen(sock_sfd, &servaddr) == F_FAILURE) {
        close(sock_sfd);
        sock_status = SOCKET_CLOSED;
        exit(EXIT_FAILURE);
    }
    
    signal(SIGINT, handle_sigint);

    // server logic inside server_control_loop()
    server_control_loop();

    terminate_server(EXIT_FAILURE);
}


//-- (client only!) loses the client fd and terminates
void terminate_client(int exit_status) {
    sock_status = SOCKET_CLOSED;
    close(sock_sfd);
    free_args();
    exit(exit_status);
}

//-- (client only!) configs servaddr struct before launching any client
int config_servaddr(struct sockaddr_in *servaddr) {
    servaddr->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip, &(servaddr->sin_addr)) <= 0) {
        perror_msg_cl("Invalid address / Address not supported");
        return F_FAILURE;
    }
    servaddr->sin_port = htons(port);

    return F_SUCCESS;
}

//-- (client only!) contains the inner code of 1 client managed by 1 thread
int *client_handler(struct client_data *cli_data) {
    int my_cfd = cli_data->conn_fd, recv_status;
    unsigned int my_id = cli_data->id;
    struct request creq;
    struct response cresp;

    pthread_mutex_unlock(&clid_mutex);

    creq = create_req(get_action_from_mode(), my_id);    
    send_req_through(my_cfd, &creq);

    cresp = create_empty_resp();
    recv_status = receive_resp(my_cfd, &cresp);
    while (recv_status != F_SUCCESS) {
        recv_status = receive_resp(my_cfd, &cresp);
    }

    fprintf(stdout, "[Cliente #%i] %s, contador=%i, tiempo=%ld ns\n", my_id, action_to_str(creq.action), cresp.counter, cresp.latency_time);

    close(my_cfd);
    return NULL;
}

//-- creates a thread pool and then launches 1 new client per thread
void launch_n_clients(struct sockaddr_in *servaddr) {
    int launched = 0, *cli_sfds = malloc(cli_threads * sizeof(int));
    pthread_t *clients = malloc(cli_threads * sizeof(pthread_t));
    struct client_data cli_data;

    DEBUG_PRINTF("_> INSIDE LAUNCH N CLIENTS...\n");
    if (cli_sfds == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    if (clients == NULL) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }

    while (launched < cli_threads) {
        cli_sfds[launched] = socket(AF_INET, SOCK_STREAM, 0);
        DEBUG_PRINTF("    _> INSIDE WHILE LOOP: [CLI_SFDS[%i] = %i] \n", launched, cli_sfds[launched]);
        
        if (connect_to_server(cli_sfds[launched], servaddr) == F_SUCCESS) {
            DEBUG_PRINTF("        _> CONNECTED TO SERVER...\n");
            pthread_mutex_lock(&clid_mutex);
            cli_data.conn_fd = cli_sfds[launched];
            cli_data.id = launched + 1;
            pthread_create(&clients[launched], NULL, 
                            (void*)client_handler, (void*)&cli_data);
            DEBUG_PRINTF("        _> PTHREAD CREATE CLEAR WITH [LAUNCHED=%i++]\n", launched);
            launched++;
        } else {
            DEBUG_PRINTF("        _> -- CONTINUE -- \n");
            continue;
        }
    }

    // wait for all threads
    while (launched > 0) {
        launched--;
		pthread_join(clients[launched], NULL);
    }
    DEBUG_PRINTF("_> ...END OF LAUNCH N CLIENTS\n");
}

//-- (client only!) inits the client with its fd and creates a receiver thread
void start_up_client(int argc, char *argv[]) {
    struct sockaddr_in servaddr;

    setbuf(stdout, NULL);

    // does few verifications while initializing the client...
    if (get_cli_args(argc, argv) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    if (config_servaddr(&servaddr) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    launch_n_clients(&servaddr);

    terminate_client(EXIT_SUCCESS);
}
