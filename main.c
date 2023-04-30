#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <limits.h>

#include "stats.h"
#include "messaging.h"

#define PRINTER_PID 4
#define PARSER_PID 3
#define READER_PID 2
#define LOGGER_PID 1
#define WATCHDOG_PID 0
#define LAST_PID PRINTER_PID

#define WATCHDOG_EXPIRATION_S 2

#define GET_MS(tp) ((long)tp.tv_sec*1000 + tp.tv_nsec / 1000000)

void* logger_proc(void* arg) {
  inbox_t* inbox = (inbox_t*)arg;
  message_t* message;

  while (inbox_get(inbox, &message) == ERR_OK) {
    switch (message->type) {
    case msg_kill:
      free(message);
      return (void*)(long)ERR_OK;
    case msg_log: {
      message_body_string_t* body = (message_body_string_t*)message->body;
      struct timespec tp;

      clock_gettime(CLOCK_MONOTONIC, &tp);
      
      printf("%ld, from %d: %.*s\n", GET_MS(tp), message->sender, body->len, body->str);
      free(message);
    }
      break;
    case msg_ping: {
      message_t *pong = malloc(sizeof(message_t));
      *pong = (message_t){};
      pong->sender = LOGGER_PID;
      pong->type = msg_pong;
      message_send(WATCHDOG_PID, pong);
      free(message);
    }
      break;
    default:
      free(message);
      return (void*)(long)ERR_FATAL;
      break;
    }
  }
  
  return (void*)(long)ERR_FATAL;
}


void watchdog_dummy(union sigval sig) {
  message_t* msg = malloc(sizeof(message_t));

  msg->next = NULL;
  msg->type = msg_pong;
  msg->sender = WATCHDOG_PID;
  
  message_send(WATCHDOG_PID, msg);
}

void* watchdog_proc(void* arg) {
  inbox_t* inbox = (inbox_t*)arg;
  message_t* message;

  timer_t timer_id;

  struct sigevent sevp = {.sigev_notify = SIGEV_THREAD,
			  .sigev_value = {.sival_int = 0},
			  .sigev_notify_function = watchdog_dummy,
			  .sigev_notify_attributes = NULL};
  
  timer_create(CLOCK_MONOTONIC, &sevp, &timer_id);

  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);

  struct itimerspec interval = {.it_value = {.tv_sec = 0, .tv_nsec = 100},
				.it_interval = {.tv_sec = 0, .tv_nsec = 0}};
  timer_settime(timer_id, 0, &interval, NULL);

  long last_seen[MAX_PID] = {GET_MS(tp)};
  for (int i = 0; i < MAX_PID; i++)
    last_seen[i] = GET_MS(tp);

  while (inbox_get(inbox, &message) == ERR_OK) {
    long first_expiration = GET_MS(tp);
    
    clock_gettime(CLOCK_MONOTONIC, &tp);

    if (message->sender < MAX_PID+1)
      last_seen[message->sender] = GET_MS(tp);

    for (int i = WATCHDOG_PID; i < LAST_PID+1; i++) {
      if (last_seen[i] < first_expiration && last_seen[i] != 0)
	first_expiration = last_seen[i];
	
      if (GET_MS(tp) - last_seen[i] > 2000L && last_seen[i] != 0) {
	char str[32];
	snprintf(str, 32, "proc %d is stale, since %ld!", i, last_seen[i]*1000);
        message_t* log = message_make_string(WATCHDOG_PID, msg_log, strlen(str), str);
	message_send(LOGGER_PID, log);

	for (int j = WATCHDOG_PID+1; j < LAST_PID+1; j++) {
	  node_kill(j);
	}

	timer_delete(timer_id);
	free(message);
	return (void*)(long)ERR_TIMEOUT;
      }
    }

    // first expiration is in ms
    interval.it_value.tv_sec = first_expiration/1000 + WATCHDOG_EXPIRATION_S;
    interval.it_value.tv_nsec = (first_expiration % 1000) * 1000000; // make ns part the same

    timer_settime(timer_id, TIMER_ABSTIME, &interval, NULL);

    free(message);
  }

  timer_delete(timer_id);
  return 0;
}

void* gen_proc(pid_t self, void* p, err_t (*fun)(message_t*)) {
  inbox_t* inbox = (inbox_t*)p;
  message_t *message;

  while (inbox_get(inbox, &message) == ERR_OK) {
    switch (message->type) {
    case msg_kill:
      free(message);
      return (void*)(long)ERR_OK;
    case msg_ping: {
      message_t *pong = malloc(sizeof(message_t));
      *pong = (message_t){};
      pong->sender = self;
      pong->type = msg_pong;
      message_send(WATCHDOG_PID, pong);
      free(message);
    }
      break;
    default: {
      err_t err = fun(message);
      free(message);
      if (err != ERR_OK)
	return (void*)(long)err;
    }
      break;
    }
  }
  
  return 0;
}

#define PROC_LAMBDA(s, fun) void* fun ## _wrap(void* p) { return gen_proc(s, p, fun); }

err_t reader_recv(message_t* message) {
  if (message->type != msg_query)
    return ERR_FATAL;

  size_t current_size = sizeof(message_t) + sizeof(message_body_string_t);
  message_t* read = malloc(current_size);
  message_body_string_t* body = (message_body_string_t*)read->body;
  body->len = 0;
  read->type = msg_read;
  read->sender = READER_PID;

  FILE* fd = fopen("/proc/stat", "r");
  
  char buf[128];
  while (fgets(buf, 128, fd) != 0) {
    int len = strlen(buf);
    read = realloc(read, current_size + body->len + len); // not optimized, but cool enough
    body = (message_body_string_t*)read->body;
    memcpy(body->str+body->len, buf, len);
    body->len += len;
  }

  fclose(fd);

  message_send(PARSER_PID, read);
  
  return ERR_OK;
}

err_t printer_recv(message_t* message) {
  if (message->type != msg_print)
    return ERR_FATAL;

  message_body_stats_t* body = (message_body_stats_t*)message->body;

  for (int i = 0; i < body->count; i++) {
    printf("core %d: %f%% ", i, body->load[i]);
  }
  printf("\n");
  
  return ERR_OK;
}

err_t parser_recv(message_t *message) {
  static stats_t* old_stats = 0;
  static int old_size;
  
  if (message->type != msg_read)
    return ERR_FATAL;

  message_body_string_t* body = (message_body_string_t*)message->body;

  stats_t* stats;
  int size = 0;
  stats_parse_lines(body->len, body->str, &stats, &size);

  message_t* print = malloc(sizeof(message_t)+sizeof(message_body_stats_t)+sizeof(float)*size);
  message_body_stats_t* print_body = (message_body_stats_t*)print->body;
  print->type = msg_print;
  print_body->count = size;

  for (int i = 0; i < size; i++) {
    if (stats[i].core < size && i < old_size)
      print_body->load[stats[i].core] = stats_avg_load(old_stats+i, stats+i);
  }
  
  message_send(PRINTER_PID, print);


  free(old_stats);
  
  old_size = size;
  old_stats = stats;
  
  return ERR_OK;
}

PROC_LAMBDA(PRINTER_PID, printer_recv);
PROC_LAMBDA(READER_PID, reader_recv);
PROC_LAMBDA(PARSER_PID, parser_recv);

void periodic_query(union sigval sig) {
  for (int i = WATCHDOG_PID+1; i < LAST_PID+1; i++) {
    message_t* ping = malloc(sizeof(message_t));
    *ping = (message_t){};
    ping->sender = -1;
    ping->type = msg_ping;
    message_send(i, ping);
  }
  
  message_t* query = malloc(sizeof(message_t));
  *query = (message_t){};
  query->sender = -1;
  query->type = msg_query;
  message_send(READER_PID, query);
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

void sigterm(int signum) {
  node_kill(LOGGER_PID);
}

int main() {
  #ifdef TEST
  void test_messaging();
  void test_messaging_mt();
  
  test_messaging();
  test_messaging_mt();

  void test_stats();
  test_stats();

  printf("---\ntests finished\n");
  #endif

  struct sigaction action;
  action = (struct sigaction){};
  action.sa_handler = sigterm;
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
 

  printf("booting...\n");

  node_t logger;
  node_register(LOGGER_PID, &logger);
  node_start(&logger, logger_proc);

  node_t printer;
  node_register(PRINTER_PID, &printer);
  node_start(&printer, printer_recv_wrap);

  node_t parser;
  node_register(PARSER_PID, &parser);
  node_start(&parser, parser_recv_wrap);

  node_t reader;
  node_register(READER_PID, &reader);
  node_start(&reader, reader_recv_wrap);


  timer_t timer_id;

  struct sigevent periodic_query_sigevent = {.sigev_notify = SIGEV_THREAD,
					     .sigev_value = {.sival_int = 0},
					     .sigev_notify_function = periodic_query,
					     .sigev_notify_attributes = NULL};

  
  if (timer_create(CLOCK_MONOTONIC, &periodic_query_sigevent, &timer_id) != 0)
    printf("err: timer create");

  const struct itimerspec interval = {
				       .it_value = {.tv_sec = 0, .tv_nsec = 1},
				       .it_interval = {.tv_sec = 1, .tv_nsec = 0}
				       
  };
  
  if (timer_settime(timer_id, 0, &interval, NULL) != 0)
    printf("err: timer set");



  node_t watchdog;
  node_register(WATCHDOG_PID, &watchdog);
  node_start(&watchdog, watchdog_proc);

  message_t* test_log = message_make_string(LOGGER_PID, msg_log, sizeof("booted up"), "booted up");
  message_send(LOGGER_PID, test_log);
  

  void* ret;
  pthread_join(logger.thread, &ret);
  printf("logger: exited\n");
  pthread_join(watchdog.thread, &ret);
  printf("watchdog: exited\n");
  pthread_join(printer.thread, &ret);
  pthread_join(reader.thread, &ret);
  pthread_join(parser.thread, &ret);

  timer_delete(timer_id);

  inbox_destroy(&logger.inbox);
  printf("logger: inbox freed\n");
  inbox_destroy(&watchdog.inbox);
  printf("watchdog: inbox freed\n");
  inbox_destroy(&printer.inbox);
  inbox_destroy(&reader.inbox);
  inbox_destroy(&parser.inbox);
  
  return 0;
}
