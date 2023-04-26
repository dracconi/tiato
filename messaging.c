#include "messaging.h"


#ifdef TEST
#include <assert.h>
int test_messaging() {
  inbox_t inbox; 
  
  assert(inbox_init(&inbox) == 0);
  assert(inbox_destroy(&inbox) == 0);
  return 0;
}
#endif
