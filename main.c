#include <stdio.h>
#include <time.h>

#include "messaging.h"

#define LOGGER_PID 0

void* logger_proc(void* arg) {
  inbox_t* inbox = (inbox_t*)arg;
  message_t* message;

  while (inbox_get(inbox, &message) == ERR_OK) {
    switch (message->type) {
    case msg_kill:
      return (void*)(long)ERR_OK;
    case msg_log: {
      message_body_string_t* body = (message_body_string_t*)message->body;
      struct timespec tp;

      clock_gettime(CLOCK_MONOTONIC, &tp);
      
      printf("%ld, from %d: %s\n", tp.tv_sec, message->sender, body->str);
    }
      break;
    default:
      return (void*)(long)ERR_FATAL;
      break;
    }
  }

  return (void*)(long)ERR_FATAL;
}

int node_start(node_t* node, void* (*func)(void*)) {
  inbox_init(&node->inbox);
  return pthread_create(&node->thread, NULL, func, &node->inbox);
}

int node_join(node_t* node) {
  void* ret;
  int res = pthread_join(node->thread, &ret);
  return (int)(long)ret | res;
}

int main() {
  #ifdef TEST
  void test_messaging();
  void test_messaging_mt();
  
  test_messaging();
  test_messaging_mt();

  printf("---\ntests finished\n");
  #endif

  printf("booting...\n");

  node_t logger;
  node_start(&logger, logger_proc);
  node_register(LOGGER_PID, &logger);


  message_t* test_log = message_make_string(1, msg_log, 4, "owo");
  message_send(LOGGER_PID, test_log);
  message_send(LOGGER_PID, test_log);
  message_send(LOGGER_PID, test_log);
  message_send(LOGGER_PID, test_log);
  message_send(LOGGER_PID, test_log);

  void* ret;
  pthread_join(logger.thread, &ret);
  
  return 0;
}
