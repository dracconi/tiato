#include "error.h"

typedef struct stats {
  unsigned short core; // cpuN
  
  unsigned long user;
  unsigned long nice;
  unsigned long system;
  unsigned long idle;
  unsigned long irq;
  unsigned long softirq;
  unsigned long steal;
  unsigned long guest;
  unsigned long guest_nice;
} stats_t;

// parse one line into stats_t
err_t stats_parse_line(int len, char* str, stats_t* stats);

// get the average load from two samples of /proc/stats
float stats_avg_load(stats_t* previous, stats_t* current);
