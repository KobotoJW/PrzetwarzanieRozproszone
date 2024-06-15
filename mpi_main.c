#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define REQ_TRZCINA 1
#define CONF_TRZCINA 2
#define FREE_TRZCINA 3
#define REQ_KWIAT 4
#define CONF_KWIAT 5
#define FREE_KWIAT 6
#define DELETE_LAST 7

#define WAITING_TRZCINA 0
#define IN_TRZCINA 1

typedef struct {
    int type;
    int sender_id;
    int clock;
    int timestamp;
} Message;

typedef struct {
    int id;
    int bee_id;
    int num_eggs;
} Trzcina;

int P, K, T, N, Tlimit;
int local_clock;
int *other_clocks;
int flowers_occupied;
int state;
Trzcina *trzciny;
MPI_Status status;
int rank, size;
Message *request_queue;
int request_count;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void send_message(int type, int receiver, int clock, int timestamp) {
    Message msg;
    msg.type = type;
    msg.sender_id = rank;
    msg.clock = clock;
    msg.timestamp = timestamp;
    MPI_Send(&msg, sizeof(Message), MPI_BYTE, receiver, 0, MPI_COMM_WORLD);
}

void broadcast_message(int type, int clock, int timestamp) {
    for (int i = 0; i < P; i++) {
        if (i != rank) {
            send_message(type, i, clock, timestamp);
        }
    }
    printf("Broadcasted message %d from %d\n", type, rank);
}

void add_request(Message msg) {
    pthread_mutex_lock(&mutex);
    request_queue[request_count++] = msg;
    // Sort requests by clock -> timestamp -> sender_id
    for (int i = 0; i < request_count - 1; i++) {
        for (int j = 0; j < request_count - i - 1; j++) {
            if (request_queue[j].clock > request_queue[j + 1].clock ||
                (request_queue[j].clock == request_queue[j + 1].clock &&
                 request_queue[j].timestamp > request_queue[j + 1].timestamp) ||
                (request_queue[j].clock == request_queue[j + 1].clock &&
                 request_queue[j].timestamp == request_queue[j + 1].timestamp &&
                 request_queue[j].sender_id > request_queue[j + 1].sender_id)) {
                Message temp = request_queue[j];
                request_queue[j] = request_queue[j + 1];
                request_queue[j + 1] = temp;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
}

void remove_oldest_request(int sender_id) {
    pthread_mutex_lock(&mutex);
    int oldest_index = -1;
    for (int i = 0; i < request_count; i++) {
        if (request_queue[i].sender_id == sender_id) {
            oldest_index = i;
            break;
        }
    }
    if (oldest_index != -1) {
        for (int i = oldest_index; i < request_count - 1; i++) {
            request_queue[i] = request_queue[i + 1];
        }
        request_count--;
    }
    pthread_mutex_unlock(&mutex);
}

void handle_message(Message msg) {
    switch (msg.type) {
        case REQ_TRZCINA:
            add_request(msg);
            break;
        case CONF_TRZCINA:
            for (int i = 0; i < T; i++) {
                if (trzciny[i].id == msg.timestamp) {
                    trzciny[i].bee_id = msg.sender_id;
                    break;
                }
            }
            break;
        case FREE_TRZCINA:
            for (int i = 0; i < T; i++) {
                if (trzciny[i].id == msg.timestamp) {
                    trzciny[i].bee_id = -1;
                    break;
                }
            }
            break;
        case REQ_KWIAT:
            add_request(msg);
            break;
        case CONF_KWIAT:
            flowers_occupied++;
            break;
        case FREE_KWIAT:
            flowers_occupied--;
            break;
        case DELETE_LAST:
            remove_oldest_request(msg.sender_id);
            break;
    }
}

void *receive_messages(void *arg) {
    while (1) {
        Message msg;
        MPI_Recv(&msg, sizeof(Message), MPI_BYTE, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        handle_message(msg);
    }
    return NULL;
}

void bee_activity() {
    state = WAITING_TRZCINA;
    local_clock = rand() % P;

    while (N > 0) {
        if (state == WAITING_TRZCINA) {
            broadcast_message(REQ_TRZCINA, local_clock, time(NULL));
            local_clock++;

            while (1) {
                pthread_mutex_lock(&mutex);
                int top_request_index = 0;
                for (int i = 0; i < request_count; i++) {
                    if (request_queue[i].sender_id == rank && request_queue[i].type == REQ_TRZCINA) {
                        top_request_index = i;
                        break;
                    }
                }

                int trzcina_id = -1;
                for (int i = 0; i < T; i++) {
                    if (trzciny[i].bee_id == -1) {
                        trzcina_id = trzciny[i].id;
                        break;
                    }
                }

                if (top_request_index < request_count && request_queue[top_request_index].sender_id == rank && trzcina_id != -1) {
                    state = IN_TRZCINA;
                    broadcast_message(CONF_TRZCINA, local_clock, trzcina_id);
                    pthread_mutex_unlock(&mutex);
                    break;
                }
                pthread_mutex_unlock(&mutex);
            }
        }

        if (state == IN_TRZCINA) {
            broadcast_message(REQ_KWIAT, local_clock, time(NULL));
            local_clock++;

            while (1) {
                pthread_mutex_lock(&mutex);
                int top_request_index = 0;
                for (int i = 0; i < request_count; i++) {
                    if (request_queue[i].sender_id == rank && request_queue[i].type == REQ_KWIAT) {
                        top_request_index = i;
                        break;
                    }
                }

                if (top_request_index < request_count && request_queue[top_request_index].sender_id == rank && flowers_occupied < K) {
                    flowers_occupied++;
                    N--;
                    broadcast_message(DELETE_LAST, local_clock, time(NULL));
                    if (N > 0) {
                        broadcast_message(FREE_KWIAT, local_clock, time(NULL));
                        state = WAITING_TRZCINA;
                    } else {
                        broadcast_message(FREE_TRZCINA, local_clock, time(NULL));
                        return;
                    }
                    pthread_mutex_unlock(&mutex);
                    break;
                }
                pthread_mutex_unlock(&mutex);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    printf("MPI_Init: rank %d, size %d\n", rank, size);

    P = size;
    K = 10; // Number of flowers
    T = 5; // Number of reeds
    N = 100; // Number of eggs
    Tlimit = 10; // Max number of eggs per reed

    trzciny = (Trzcina *)malloc(T * sizeof(Trzcina));
    other_clocks = (int *)malloc(P * sizeof(int));
    request_queue = (Message *)malloc(1000 * sizeof(Message));
    request_count = 0;

    if (trzciny == NULL || other_clocks == NULL || request_queue == NULL) {
        fprintf(stderr, "Error allocating memory\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    for (int i = 0; i < T; i++) {
        trzciny[i].id = i;
        trzciny[i].bee_id = -1;
        trzciny[i].num_eggs = 0;
    }

    srand(time(NULL) + rank);

    pthread_t receiver_thread;
    if (pthread_create(&receiver_thread, NULL, receive_messages, NULL) != 0) {
        fprintf(stderr, "Error creating thread\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    printf("I am bee number: %d\n", rank);
    bee_activity();
    printf("I am no longer bee number: %d\n", rank);

    pthread_cancel(receiver_thread);
    pthread_join(receiver_thread, NULL);

    free(trzciny);
    free(other_clocks);
    free(request_queue);
    MPI_Finalize();
    return 0;
}
