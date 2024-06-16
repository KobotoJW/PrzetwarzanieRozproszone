#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define REQ_TRZCINA 0
#define FREE_TRZCINA 1
#define CONF_TRZCINA 2
#define REQ_KWIAT 3
#define CONF_KWIAT 4
#define FREE_KWIAT 5
#define DELETE_LAST 6
#define ACK 7

typedef struct {
    int type;
    int sender_id;
    int bee_clock;
    int timestamp;
} Message;

typedef struct {
    int id_trzciny;
    int id_pszczoly;
    int liczba_jaj;
} Trzcina;

int P, K, T, N, Tlimit, rank;
Trzcina *trzciny;
Message *request_queue;
int local_clock;
int flowers_occupied = 0;
int acks_received = 0;
Message process_message;

MPI_Datatype MPI_MyMessage;

pthread_mutex_t request_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clock_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t flowers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t trzciny_mutex = PTHREAD_MUTEX_INITIALIZER;

void create_mpi_message_type() {
    int lengths[4] = {1, 1, 1, 1};
    const MPI_Aint offsets[4] = {
        offsetof(Message, type),
        offsetof(Message, sender_id),
        offsetof(Message, bee_clock),
        offsetof(Message, timestamp)
    };
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    MPI_Type_create_struct(4, lengths, offsets, types, &MPI_MyMessage);
    MPI_Type_commit(&MPI_MyMessage);
}

void send_message(int dest, Message msg) {
    pthread_mutex_lock(&clock_mutex);
    msg.bee_clock = ++local_clock;
    pthread_mutex_unlock(&clock_mutex);
    MPI_Send(&msg, 1, MPI_MyMessage, dest, 0, MPI_COMM_WORLD);
}

int compare_messages(const void *a, const void *b) {
    Message *msgA = (Message *)a;
    Message *msgB = (Message *)b;

    if (msgA->bee_clock != msgB->bee_clock) {
        return msgA->bee_clock - msgB->bee_clock;
    }

    if (msgA->timestamp != msgB->timestamp) {
        return msgA->timestamp - msgB->timestamp;
    }

    return msgA->sender_id - msgB->sender_id;
}

void sort_request_queue() {
    qsort(request_queue, P, sizeof(Message), compare_messages);
}

void remove_oldest_request(int given_id) {
    pthread_mutex_lock(&request_queue_mutex);
    for (int i = 0; i < P; i++) {
        if (request_queue[i].sender_id == given_id) {
            request_queue[i].bee_clock = __INT_MAX__;
        }
    }
    sort_request_queue();
    pthread_mutex_unlock(&request_queue_mutex);
}

void initialize(int size) {
    printf("Initializing process %d\n", rank);
    P = size;
    K = 2;
    T = 3;
    N = 5;
    Tlimit = 15;

    trzciny = (Trzcina *)malloc(T * sizeof(Trzcina));
    if (trzciny == NULL) {
        fprintf(stderr, "Error allocating memory for trzciny\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (int i = 0; i < T; i++) {
        trzciny[i].id_trzciny = i;
        trzciny[i].id_pszczoly = -1;
        trzciny[i].liczba_jaj = 0;
    }

    request_queue = (Message *)malloc(P * sizeof(Message));
    if (request_queue == NULL) {
        fprintf(stderr, "Error allocating memory for request_queue\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    for (int i = 0; i < P; i++) {
        request_queue[i].sender_id = i;
        request_queue[i].type = -1;
        request_queue[i].bee_clock = 123456;
        request_queue[i].timestamp = 0;
    }

    local_clock = 0;
    local_clock = clock()%P;

    Message process_message = {-1, -1, -1, -1};
}

void send_ack(int dest) {
    Message ack_message = {ACK, rank, local_clock, 0};
    MPI_Send(&ack_message, 1, MPI_MyMessage, dest, 0, MPI_COMM_WORLD);
}

void handle_message(Message msg) {
    //printf("Process %d received message from process %d of type: %d and clock: %d\n", rank, msg.sender_id, msg.type, msg.bee_clock);
    switch (msg.type) {
        case REQ_TRZCINA:
            pthread_mutex_lock(&request_queue_mutex);
            //printf("Process %d received request for a reed from process %d\n", rank, msg.sender_id);
            for (int i = 0; i < P; i++) {
                if (request_queue[i].sender_id == msg.sender_id) {
                    request_queue[i] = msg;
                
                }
            }
            sort_request_queue();
            pthread_mutex_unlock(&request_queue_mutex);
            send_ack(msg.sender_id);
            break;
        case DELETE_LAST:
            remove_oldest_request(msg.sender_id);
            break;
        case FREE_TRZCINA:
            pthread_mutex_lock(&trzciny_mutex);
            for (int i = 0; i < T; i++) {
                if (trzciny[i].id_pszczoly == msg.sender_id) {
                    trzciny[i].id_pszczoly = -1;
                    trzciny[i].liczba_jaj += N;
                    break;
                }
            }
            pthread_mutex_unlock(&trzciny_mutex);
            P--;
            break;
        case CONF_TRZCINA:
            pthread_mutex_lock(&trzciny_mutex);
            trzciny[msg.timestamp].id_pszczoly = msg.sender_id;
            pthread_mutex_unlock(&trzciny_mutex);
            break;
        case REQ_KWIAT:
            //printf("pre req kwiat\n");
            pthread_mutex_lock(&request_queue_mutex);
            for (int i = 0; i < P; i++) {
                if (request_queue[i].sender_id == msg.sender_id) {
                    request_queue[i] = msg;
                }
            }
            sort_request_queue();
            pthread_mutex_unlock(&request_queue_mutex);
            send_ack(msg.sender_id);
            break;
        case CONF_KWIAT:
            //printf("%d: Process %d received access to a flower\n", rank, msg.sender_id);
            pthread_mutex_lock(&flowers_mutex);
            flowers_occupied++;
            pthread_mutex_unlock(&flowers_mutex);
            break;
        case FREE_KWIAT:
            pthread_mutex_lock(&flowers_mutex);
            flowers_occupied--;
            pthread_mutex_unlock(&flowers_mutex);
            break;
        case ACK:
            acks_received++;
            break;
    }
}

void receive_message(int source, Message *msg) {
    MPI_Recv(msg, 1, MPI_MyMessage, source, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    pthread_mutex_lock(&clock_mutex);
    local_clock++;
    pthread_mutex_unlock(&clock_mutex);
}

void *receive_messages(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while (1) {
        Message msg;
        receive_message(MPI_ANY_SOURCE, &msg);
        handle_message(msg);
    }
    return NULL;
}

void print_current_queue() {
    printf("-------------------\n");
    for (int i = 0; i < P; i++) {
        printf("Request queue %d: type: %d, sender: %d, clock: %d, time: %d\n", i, request_queue[i].type, request_queue[i].sender_id, request_queue[i].bee_clock, request_queue[i].timestamp);
    }
    printf("-------------------\n");
}

void bee_process2(int bee_id) {
    printf("I'm bee %d with clock: %d\n", bee_id, local_clock);
    
    // Initialize and send a reed request message
    process_message.type = REQ_TRZCINA;
    process_message.sender_id = bee_id;
    process_message.bee_clock = local_clock;
    process_message.timestamp = 0;

    acks_received = 0;
    for (int i = 0; i < P; i++) {
        if (i != bee_id) {
            send_message(i, process_message);
        }
    }
    sleep(1);

    pthread_mutex_lock(&request_queue_mutex);
    for (int i = 0; i < P; i++) {
        if (request_queue[i].sender_id == bee_id) {
            request_queue[i] = process_message;
        }
    }
    sort_request_queue();
    pthread_mutex_unlock(&request_queue_mutex);

    int inreed = 0;
    int reed_request_counter = 0;

    while (!inreed) {
        sleep(1);
        // printf("Process %d is waiting for a reed\n", bee_id);
        // if (bee_id == 2) {
        //     print_current_queue();
        // }
        pthread_mutex_lock(&request_queue_mutex);
        //printf("Bee %d past mutex\n", bee_id);
        sort_request_queue();
        for (int i = 0; i < P; i++) {
            if (request_queue[i].type == REQ_TRZCINA && !inreed) {
                //printf("Process %d is waiting for a reed\n", bee_id);
                if (request_queue[i].sender_id == bee_id && reed_request_counter <= T && acks_received >= P - 1) {
                    acks_received = 0;
                    pthread_mutex_lock(&trzciny_mutex);
                    if (trzciny[reed_request_counter].id_pszczoly == -1) {
                        trzciny[reed_request_counter].id_pszczoly = bee_id;
                        pthread_mutex_unlock(&trzciny_mutex);
                        inreed = 1;

                        process_message.type = CONF_TRZCINA;
                        process_message.sender_id = bee_id;
                        process_message.bee_clock = local_clock;
                        process_message.timestamp = reed_request_counter;

                        for (int i = 0; i < P; i++) {
                            if (i != bee_id) {
                                send_message(i, process_message);
                            }
                        }
                        pthread_mutex_unlock(&request_queue_mutex);

                        break;
                    } else {
                        printf("Process %d cant find a free reed\n", bee_id);
                        for (int i = 0; i < T; i++) {
                            printf("%d: Reed %d: pszczola %d\n", bee_id, i, trzciny[i].id_pszczoly);
                        }
                        pthread_mutex_unlock(&trzciny_mutex);
                        pthread_mutex_unlock(&request_queue_mutex);
                    }
                    pthread_mutex_unlock(&trzciny_mutex);
                }
                reed_request_counter++;
            }
        }
        pthread_mutex_unlock(&request_queue_mutex);
        reed_request_counter = 0;
    }
    printf("Process %d received access to a reed\n", bee_id);

    process_message.type = DELETE_LAST;
    process_message.sender_id = bee_id;
    process_message.bee_clock = local_clock;
    process_message.timestamp = 0;

    pthread_mutex_lock(&request_queue_mutex);
    for (int i = 0; i < P; i++) {
        if (request_queue[i].sender_id == bee_id) {
            request_queue[i].bee_clock = __INT_MAX__;
        }
    }
    sort_request_queue();
    pthread_mutex_unlock(&request_queue_mutex);

    for (int i = 0; i < P; i++) {
        if (i != bee_id) {
            send_message(i, process_message);
        }
    }

    while (N > 0) {
        sleep(1);
        printf("Process %d is waiting for a flower with %d eggs\n", bee_id, N);
        process_message.type = REQ_KWIAT;
        process_message.sender_id = bee_id;
        process_message.bee_clock = local_clock;
        process_message.timestamp = 0;

        for (int i = 0; i < P; i++) {
            if (i != bee_id) {
                send_message(i, process_message);
            }
        }

        pthread_mutex_lock(&request_queue_mutex);
        for (int i = 0; i < P; i++) {
            if (request_queue[i].sender_id == bee_id) {
                request_queue[i] = process_message;
            }
        }
        sort_request_queue();
        pthread_mutex_unlock(&request_queue_mutex);

        int inflower = 0;
        int flower_request_counter = 0;

        while (!inflower) {
            sleep(1);
            // printf("Process %d is waiting for a flower\n", bee_id);
            // printf("Process %d thinks there are %d free flowers\n", bee_id, K - flowers_occupied);

            pthread_mutex_lock(&request_queue_mutex);
            for (int i = 0; i < P; i++) {
                if (request_queue[i].type == REQ_KWIAT) {
                    if (request_queue[i].sender_id == bee_id && flower_request_counter <= K && acks_received >= P - 1) {
                        inflower = 1;
                        pthread_mutex_unlock(&request_queue_mutex);
                        acks_received = 0;
                        break;
                    }
                    flower_request_counter++;
                }
            }
            pthread_mutex_unlock(&request_queue_mutex);
            flower_request_counter = 0;
        }

        process_message.type = DELETE_LAST;
        process_message.sender_id = bee_id;
        process_message.bee_clock = local_clock;
        process_message.timestamp = 0;

        for (int i = 0; i < P; i++) {
            if (i != bee_id) {
                send_message(i, process_message);
            }
        }

        pthread_mutex_lock(&request_queue_mutex);
        for (int i = 0; i < P; i++) {
            if (request_queue[i].sender_id == bee_id) {
                request_queue[i].bee_clock = __INT_MAX__;
            }
        }
        sort_request_queue();
        pthread_mutex_unlock(&request_queue_mutex);

        pthread_mutex_lock(&flowers_mutex);
        flowers_occupied++;
        pthread_mutex_unlock(&flowers_mutex);

        process_message.type = CONF_KWIAT;
        process_message.sender_id = bee_id;
        process_message.bee_clock = local_clock;
        process_message.timestamp = 0;
        printf("Process %d received access to a flower\n", bee_id);
        
        for (int i = 0; i < P; i++) {
            if (i != bee_id) {
                send_message(i, process_message);
            }
        }

        process_message.type = FREE_KWIAT;
        process_message.sender_id = bee_id;
        process_message.bee_clock = local_clock;
        process_message.timestamp = 0;

        for (int i = 0; i < P; i++) {
            if (i != bee_id) {
                send_message(i, process_message);
            }
        }

        pthread_mutex_lock(&flowers_mutex);
        flowers_occupied--;
        pthread_mutex_unlock(&flowers_mutex);

        for (int i = 0; i < T; i++) {
            pthread_mutex_unlock(&trzciny_mutex);
            if (trzciny[i].id_pszczoly == bee_id) {
                trzciny[i].liczba_jaj++;
                break;
            }
            pthread_mutex_unlock(&trzciny_mutex);
        }

        N--;
        //printf("Process %d laid an egg: %d\n", bee_id, 5 - N);
    }

    // process_message.type = DELETE_LAST;
    // process_message.sender_id = bee_id;
    // process_message.bee_clock = local_clock;
    // process_message.timestamp = 0;

    // for (int i = 0; i < P; i++) {
    //     if (i != bee_id) {
    //         send_message(i, process_message);
    //     }
    // }

    // pthread_mutex_lock(&request_queue_mutex);
    // for (int i = 0; i < P; i++) {
    //     if (request_queue[i].sender_id == bee_id) {
    //         request_queue[i].bee_clock = __INT_MAX__;
    //     }
    // }
    // sort_request_queue();
    // pthread_mutex_unlock(&request_queue_mutex);

    for (int i = 0; i < T; i++) {
        if (trzciny[i].id_pszczoly == bee_id) {
            trzciny[i].id_pszczoly = -1;
        }
    }

    process_message.type = FREE_TRZCINA;
    process_message.sender_id = bee_id;
    process_message.bee_clock = local_clock;
    process_message.timestamp = 0;

    for (int i = 0; i < P; i++) {
        if (i != bee_id) {
            send_message(i, process_message);
        }
    }
}

int main(int argc, char **argv) {
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

    if (provided != MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "Error initializing MPI\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    create_mpi_message_type();

    initialize(size);

    pthread_t receiver_thread;
    if (pthread_create(&receiver_thread, NULL, receive_messages, NULL) != 0) {
        fprintf(stderr, "Error creating thread\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (rank < P) {
        //bee_process(rank);
        bee_process2(rank);
        printf("Process %d is a bee no more\n", rank);
    }

    pthread_cancel(receiver_thread);

    if (pthread_join(receiver_thread, NULL) != 0) {
        fprintf(stderr, "Error joining thread\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    MPI_Type_free(&MPI_MyMessage);
    MPI_Finalize();

    free(trzciny);
    free(request_queue);

    return 0;
}