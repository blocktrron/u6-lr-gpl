#define _BSD_SOURCE

#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <resolv.h>

void resolv_init(void) {
	res_init();
}
