#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include "stats.h"
#include "error.h"

#define MAX_PID 5

/*
  Communication between threads is handled through 
  a simple messaging system, akin to that seen in Go
  or Erlang. 

  Each thread is a node.

  Each node has an inbox, to which messages are
  sent.

  Then that node processes those messages in FIFO order.
 */

typedef struct message_body_string {
  int len;
  char str[];
} message_body_string_t;

typedef struct message_body_stats {
  int count;
  float load[];
} message_body_stats_t;

typedef enum message_type {
			   // following first 4 types have empty bodies
			   msg_kill,
			   
			   msg_ping,
			   msg_pong,

			   msg_query,

			   // body = message_body_string_t
			   msg_read,

			   // body = message_body_stats_t
			   msg_print,

			   // body = message_body_string_t
			   msg_log
} message_type_t;

typedef int pid_t; // ``process'' identifier

typedef struct message message_t;

struct message {
  // linked list, filled when adding a message to the inbox
  message_t* next;

  message_type_t type;
  pid_t sender;

  // body of the message embedded in the struct
  char body[];
};

typedef struct inbox {
  int count; 
  message_t* head;
  message_t* tail;

  // semaphore for waiting on if all messages are exhausted
  pthread_cond_t update;

  // mutex for accessing head/tail/count
  pthread_mutex_t mutex;
} inbox_t;


// initialize mutex and cond, set ptrs to NULL
err_t inbox_init(inbox_t* inbox);

// destroy mutex and cond, free all messages
err_t inbox_destroy(inbox_t* inbox);


// both put and get use mutex to lock the state globally

// place the message in the inbox and send a signal on .update
// place it on .tail
err_t inbox_put(inbox_t* inbox, message_t* message);

// get the message from the inbox
// get it from .head until exhausted and wait on .update
err_t inbox_get(inbox_t* inbox, message_t** message);

// helper function for making string type messages
message_t* message_make_string(pid_t sender, message_type_t type, int len, char* str);

// send the message to pid
err_t message_send(pid_t pid, message_t* message);


typedef struct node {
  pthread_t thread;

  inbox_t inbox;
} node_t;

err_t node_register(pid_t pid, node_t* node);
err_t node_kill(pid_t pid);
