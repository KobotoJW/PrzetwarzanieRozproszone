#include <mpi.h>
#include <stdio.h>
#include <time.h>

#define T 3
#define Tlimit 15
#define K 3
#define N 5
#define REQ_TRZCINA 0
#define FREE_TRCINA 1
#define REQ_KWIAT 2
#define FREE_KWIAT 3
#define DELETE_LAST 4

typedef struct {
    int type;
    int id;
    int clk;
    long int timestamp;
} Message;

typedef struct {
    int id;
    int eggs;
    int occupant_id;
} Trzcina;

MPI_Datatype mpi_message_type;

void create_type() {
    const int nitems = 4;
    int blocklengths[4] = {1, 1, 1, 1};
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_LONG};
    MPI_Datatype mpi_message_type;
    MPI_Aint offsets[4];
    offsets[0] = offsetof(Message, type);
    offsets[1] = offsetof(Message, id);
    offsets[2] = offsetof(Message, clk);
    offsets[3] = offsetof(Message, timestamp);
    MPI_Type_create_struct(nitems, blocklengths, offsets, types, &mpi_message_type);
    MPI_Type_commit(&mpi_message_type);
}

Message send_message(int type, int id, int clk){
    Message message;
    message.type = type;
    message.id = id;    
    message.clk = clk;
    message.timestamp = (long int) time(NULL);

    MPI_Bcast(&message, 1, mpi_message_type, id, MPI_COMM_WORLD);

    return message;
}

Message recieve_message(int id){
    Message message;
    MPI_Bcast(&message, 1, mpi_message_type, id, MPI_COMM_WORLD);
    return message;
}

int compare(const void *a, const void *b) {
    Message *messageA = (Message *)a;
    Message *messageB = (Message *)b;

    if (messageA->clk != messageB->clk) {
        return (messageA->clk - messageB->clk);
    }
    if (messageA->timestamp != messageB->timestamp) {
        return (messageA->timestamp - messageB->timestamp);
    }
    return (messageA->id - messageB->id);
}


int main(int argc, char** argv) {
    MPI_Init(NULL, NULL);
    int last_filled = 0;

    int bees_quant;
    MPI_Comm_size(MPI_COMM_WORLD, &bees_quant);

    int my_id;
    MPI_Comm_rank(MPI_COMM_WORLD, &my_id);

    int my_clock = randint(0, bees_quant);

    int clocks_vector[bees_quant];
    Message* message_board = malloc(bees_quant * sizeof(Message) * 2);
    Trzcina* trzciny = malloc(T * sizeof(Trzcina));

    //create MPI message type
    create_type();

    for (int i = 0; i < T; i++) {
        trzciny[i].id = i;
        trzciny[i].eggs = 0;
        trzciny[i].occupant_id = -1;
    }

    Message message = send_message(REQ_TRZCINA, my_id, my_clock++);
    clocks_vector[my_id] = my_clock;
    message_board[last_filled++] = message;
    
    while (1) {
        Message message = recieve_message(my_id);
        clocks_vector[message.id] = message.clk;
        //append message to the end of message board
        message_board[last_filled++] = message;
        qsort(message_board, last_filled, sizeof(Message), compare);

        if (message.type == DELETE_LAST) {
            int i;
            for (i = 0; i < last_filled; i++) {
                if (message_board[i].id == message.id) {
                    break;
                }
            }
            if (i < last_filled) { // if found
                for (int j = i; j < last_filled - 1; j++) {
                    message_board[j] = message_board[j + 1];
                }
                last_filled--;
            }
        }

        if (message_board[0].id == my_id && message_board[0].type == REQ_TRZCINA) {
            for (int i = 0; i < T; i++) {
                if (trzciny[i].occupant_id == -1) {
                    trzciny[i].occupant_id = my_id;
                    send_message(DELETE_LAST, my_id, my_clock);
                    break;
                }
            }
            break;
        }
    }

    MPI_Finalize();
}