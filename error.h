#ifndef _ERROR_H
#define _ERROR_H
typedef enum error {
		    ERR_OK = 0,
		    ERR_FATAL = 1,


		    ERR_NOMSG,
		    ERR_TIMEOUT,
		    
		    ERR_STATS_NOT_CPU,
		    ERR_STATS_TOO_LITTLE,
		    ERR_STATS_SKIP
} err_t;
#endif
