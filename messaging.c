#include "messaging.h"

static node_t* nodes[MAX_PID-1] = {0};

err_t inbox_init(inbox_t* inbox) {
  inbox->count = 0;
  inbox->head = 0;
  inbox->tail = 0;
  
  if (pthread_mutex_init(&inbox->mutex, NULL) != 0)
    return ERR_FATAL;
  if (pthread_cond_init(&inbox->update, NULL) != 0)
    return ERR_FATAL;

  return ERR_OK;
}

err_t inbox_destroy(inbox_t* inbox) {
  pthread_mutex_destroy(&inbox->mutex);
  pthread_cond_destroy(&inbox->update);

  // Remove all messages that were left in the inbox
  message_t* message = inbox->head;

  while (message != 0) {
    message_t* tmp = message->next;
    free(message);
    message = tmp;
  }

  return ERR_OK;
}

err_t inbox_put(inbox_t* inbox, message_t* message) {
  pthread_mutex_lock(&inbox->mutex);
  message->next = 0;

  if (inbox->head != 0 && inbox->head->next == 0)
    inbox->head->next = message;
  
  if (inbox->head == 0)
    inbox->head = message;

  if (inbox->tail != 0)
    inbox->tail->next = message;

  inbox->tail = message;

  inbox->count++;
  pthread_cond_broadcast(&inbox->update);
  pthread_mutex_unlock(&inbox->mutex);

  return ERR_OK;
}

// unsafe version, no threading
static err_t inbox_get_unsafe(inbox_t* inbox, message_t** message) {
  if (inbox->head == 0)
    return ERR_NOMSG;

  inbox->count--;

  if (inbox->count == 0)
    inbox->tail = 0;

  *message = inbox->head;

  inbox->head = inbox->head->next;

  return ERR_OK;
}

err_t inbox_get(inbox_t* inbox, message_t** message) {
  if (pthread_mutex_lock(&inbox->mutex) != 0) {
    pthread_mutex_unlock(&inbox->mutex);
    return ERR_FATAL;
  }
  
  if (inbox->count == 0 && pthread_cond_wait(&inbox->update, &inbox->mutex) != 0) {
    if (pthread_mutex_unlock(&inbox->mutex) != 0)
      return ERR_FATAL;
    return ERR_FATAL;
  }
  
  err_t e = inbox_get_unsafe(inbox, message);
  if (pthread_mutex_unlock(&inbox->mutex) != 0)
    return ERR_FATAL;
  return e;
}


message_t* message_make_string(pid_t sender, message_type_t type, int len, char* str) {
  message_t* msg = malloc(sizeof(message_t) + sizeof(message_body_string_t) + sizeof(char)*len);
  msg->type = type;
  msg->next = NULL;
  msg->sender = sender;
  message_body_string_t* body = (message_body_string_t*)msg->body;
  body->len = len;
  memcpy(body->str, str, len);
  return msg;
} 

err_t message_send(pid_t pid, message_t* message) {
  if (pid > MAX_PID - 1 || nodes[pid] == 0)
    return ERR_FATAL;

  return inbox_put(&nodes[pid]->inbox, message);
}


// not thread safe. use only from main thread at the beginning to register nodes
err_t node_register(pid_t pid, node_t* node) {
  if (pid > MAX_PID - 1)
    return ERR_FATAL;
  
  nodes[pid] = node;
  return ERR_OK;
}

err_t node_kill(pid_t pid) {
  message_t* kill = malloc(sizeof(message_t));
  kill->type = msg_kill;
  kill->sender = -1;
  return message_send(pid, kill);
}

#ifdef TEST
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define Assert(a) assert(a); printf("."); c++;

void test_messaging() {
  printf("messaging.c test_messaging():\n");
  int c = 0;

  node_t node;
  
  Assert(inbox_init(&node.inbox) == 0);

  node_register(1, &node);

  message_t* message = malloc(sizeof(message_t));

  message->next = NULL;
  message->type = msg_ping;
  message->sender = 0;
  
  //  Assert(inbox_put(&inbox, message) == 0);
  Assert(message_send(1, message) == 0);

  message_t* ptr;

  Assert(inbox_get(&node.inbox, &ptr) == 0);
  Assert(message == ptr);
  Assert(node.inbox.count == 0);

  Assert(inbox_put(&node.inbox, message) == 0);

  char str[] = "abcde";
  message_t* msg_long = malloc(sizeof(message_t) + sizeof(message_body_string_t) + sizeof(str));
  msg_long->next = NULL;
  msg_long->type = msg_read;
  msg_long->sender = 0;
  message_body_string_t* body = (message_body_string_t*)msg_long->body;
  body->len = sizeof(str);
  memcpy(body->str, str, sizeof(str));

  Assert(strcmp(((message_body_string_t*)msg_long->body)->str, str) == 0);

  Assert(inbox_put(&node.inbox, msg_long) == 0);
  Assert(node.inbox.count == 2);

  Assert(inbox_get(&node.inbox, &ptr) == 0);
  Assert(ptr == message);

  Assert(inbox_get(&node.inbox, &ptr) == 0);
  Assert(ptr == msg_long);

  message_t* msg_str = message_make_string(0, msg_read, sizeof(str), str);
  // bytes are lost here, but that's just for testing, so no worries
  Assert(0 == memcmp(msg_long, msg_str,
		     sizeof(message_t) + sizeof(message_body_string_t) + sizeof(str)));

  free(msg_str);

  // If you uncomment following lines, the calling thread should block!
  //  assert(inbox_get(&inbox, &ptr) == 0);
  //  assert(ptr == message);

  Assert(inbox_get_unsafe(&node.inbox, &ptr) == ERR_NOMSG);
  
  
  
  Assert(inbox_destroy(&node.inbox) == 0);

  free(msg_long);
  free(message);

  printf("\n%d tests ok.\n", c);
}

void* test_reader_mt(void* arg) {
  printf("messaging.c test_reader_mt():\n");
  inbox_t* inbox = (inbox_t*)arg;
  message_t* message;
  long sum = 0;

  while (inbox_get(inbox, &message) == ERR_OK) {
    if (message->type == msg_kill) {
      printf("test_reader_mt(): kill, %ld\n", sum);
      return (void*)sum;
    }
		
    printf("test_reader_mt(): %s from %d\n", ((message_body_string_t*)message->body)->str, message->sender);

    int num = 0;
    sscanf(((message_body_string_t*)message->body)->str, "%d", &num);
    sum += num;
    
    free(message);
  }

  return 0;
}

void* test_writer_mt(void* arg) {
  printf("messaging.c test_writer_mt(%ld):\n", (long)arg);

  char str[16] = {0};

  for (int i = 0; i < 500; i++) {
    snprintf(str, 16, "%d", i);
    message_t* msg = message_make_string((int)(long)arg, msg_print, strlen(str)+1, str);
    message_send(1, msg);
    sched_yield();
  }

  return 0;
}

void test_messaging_mt() {
  printf("messaging.c test_messaging_mt():\n");

  node_t node1;

  pthread_t thread2;
  pthread_t thread3;
  pthread_t thread4;

  inbox_init(&node1.inbox);

  node_register(1, &node1);

  assert(pthread_create(&node1.thread, NULL, test_reader_mt, &node1.inbox) == 0);
  pthread_create(&thread2, NULL, test_writer_mt, (void*)1);
  pthread_create(&thread3, NULL, test_writer_mt, (void*)2);
  pthread_create(&thread4, NULL, test_writer_mt, (void*)3);

  void* ret;
  pthread_join(thread2, &ret);
  pthread_join(thread3, &ret);
  pthread_join(thread4, &ret);

  message_t* kill_msg = malloc(sizeof(message_t));
  kill_msg->next = 0;
  kill_msg->type = msg_kill;
  kill_msg->sender = 0;
  message_send(1, kill_msg);
  pthread_join(node1.thread, &ret);
  assert((long)ret == 499*250*3); // sum of all the numbers sent

  inbox_destroy(&node1.inbox);
  free(kill_msg);

  pthread_detach(node1.thread);
  pthread_detach(thread2);
  pthread_detach(thread3);
  pthread_detach(thread4);
}
#endif
