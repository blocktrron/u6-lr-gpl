/* ipset.c is Copyright (c) 2013 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 dated June, 1991, or
   (at your option) version 3 dated 29 June, 2007.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
     
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dnsmasq.h"

#if defined(HAVE_IPSET)

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/version.h>
#include <linux/netlink.h>

static void add_to_ipset_logged(char *name, char *set, union all_addr *addr,
        int flags, int remove)
{
    log_query(flags | F_IPSET, name, addr, set);
    add_to_ipset(set, addr, flags, remove);
}

static void add_serv_or_host_to_all_ipsets(char **ipsets_cur, struct server *serv,
        struct host_record *hr, char *hr_name)
{
    for ( ; *ipsets_cur; ipsets_cur++) {
        char *set = *ipsets_cur;
        if (serv) {
            if (serv->addr.sa.sa_family == AF_INET) {
                add_to_ipset_logged(serv->domain, set,
                        (union all_addr *)&serv->addr.in.sin_addr, F_IPV4, 0);
#ifdef HAVE_IPV6
            } else {
                add_to_ipset_logged(serv->domain, set,
                        (union all_addr *)&serv->addr.in6.sin6_addr, F_IPV6, 0);
#endif
            }
        }
        if (hr) {
            if (hr->addr.s_addr != 0) {
                add_to_ipset_logged(hr_name, set,
                        (union all_addr *)&hr->addr, F_IPV4, 0);
            }
#ifdef HAVE_IPV6
            if (!IN6_IS_ADDR_UNSPECIFIED(&hr->addr6)) {
                add_to_ipset_logged(hr_name, set,
                        (union all_addr *)&hr->addr6, F_IPV6, 0);
            }
#endif
        }
    }
}

// Add all address and host-record local entries to their ipsets
void add_local_to_ipsets(void)
{
    struct ipsets *ipset_pos;
    for (ipset_pos = daemon->ipsets; ipset_pos; ipset_pos = ipset_pos->next) {
        struct server *serv;
        struct host_record *hr;
        for (serv = daemon->servers; serv; serv = serv->next) {
            if (!serv->domain || !(serv->flags & SERV_LITERAL_ADDRESS)) {
                continue;
            }
            if (!hostname_isequal(serv->domain, ipset_pos->domain)) {
                continue;
            }
            add_serv_or_host_to_all_ipsets(ipset_pos->sets, serv, NULL, NULL);
        }
        for (hr = daemon->host_records; hr; hr = hr->next) {
            struct name_list *nl;
            for (nl = hr->names; nl; nl = nl->next) {
                if (!hostname_isequal(nl->name, ipset_pos->domain)) {
                    continue;
                }
                add_serv_or_host_to_all_ipsets(ipset_pos->sets, NULL, hr, nl->name);
            }
        }
    }
}

#if defined(HAVE_LINUX_NETWORK)
/* We want to be able to compile against old header files
   Kernel version is handled at run-time. */

#define NFNL_SUBSYS_IPSET 6

#define IPSET_ATTR_DATA 7
#define IPSET_ATTR_IP 1
#define IPSET_ATTR_IPADDR_IPV4 1
#define IPSET_ATTR_IPADDR_IPV6 2
#define IPSET_ATTR_PROTOCOL 1
#define IPSET_ATTR_SETNAME 2
#define IPSET_CMD_ADD 9
#define IPSET_CMD_DEL 10
#define IPSET_MAXNAMELEN 32
#define IPSET_PROTOCOL 6

#ifndef NFNETLINK_V0
#define NFNETLINK_V0    0
#endif

#ifndef NLA_F_NESTED
#define NLA_F_NESTED		(1 << 15)
#endif

#ifndef NLA_F_NET_BYTEORDER
#define NLA_F_NET_BYTEORDER	(1 << 14)
#endif

struct my_nlattr {
        __u16           nla_len;
        __u16           nla_type;
};

struct my_nfgenmsg {
        __u8  nfgen_family;             /* AF_xxx */
        __u8  version;          /* nfnetlink version */
        __be16    res_id;               /* resource id */
};


/* data structure size in here is fixed */
#define BUFF_SZ 256

#define NL_ALIGN(len) (((len)+3) & ~(3))
static const struct sockaddr_nl snl = { .nl_family = AF_NETLINK };
static int ipset_sock;
static char *buffer;

static inline void add_attr(struct nlmsghdr *nlh, uint16_t type, size_t len, const void *data)
{
  struct my_nlattr *attr = (void *)nlh + NL_ALIGN(nlh->nlmsg_len);
  uint16_t payload_len = NL_ALIGN(sizeof(struct my_nlattr)) + len;
  attr->nla_type = type;
  attr->nla_len = payload_len;
  memcpy((void *)attr + NL_ALIGN(sizeof(struct my_nlattr)), data, len);
  nlh->nlmsg_len += NL_ALIGN(payload_len);
}

void ipset_init(void)
{
  if ( 
      (buffer = safe_malloc(BUFF_SZ)) &&
      (ipset_sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_NETFILTER)) != -1 &&
      (bind(ipset_sock, (struct sockaddr *)&snl, sizeof(snl)) != -1))
    return;
  
  die (_("failed to create IPset control socket: %s"), NULL, EC_MISC);
}

static int new_add_to_ipset(const char *setname, const union all_addr *ipaddr, int af, int remove)
{
  struct nlmsghdr *nlh;
  struct my_nfgenmsg *nfg;
  struct my_nlattr *nested[2];
  uint8_t proto;
  int addrsz = (af == AF_INET6) ? IN6ADDRSZ : INADDRSZ;

  if (strlen(setname) >= IPSET_MAXNAMELEN) 
    {
      errno = ENAMETOOLONG;
      return -1;
    }
  
  memset(buffer, 0, BUFF_SZ);

  nlh = (struct nlmsghdr *)buffer;
  nlh->nlmsg_len = NL_ALIGN(sizeof(struct nlmsghdr));
  nlh->nlmsg_type = (remove ? IPSET_CMD_DEL : IPSET_CMD_ADD) | (NFNL_SUBSYS_IPSET << 8);
  nlh->nlmsg_flags = NLM_F_REQUEST;
  
  nfg = (struct my_nfgenmsg *)(buffer + nlh->nlmsg_len);
  nlh->nlmsg_len += NL_ALIGN(sizeof(struct my_nfgenmsg));
  nfg->nfgen_family = af;
  nfg->version = NFNETLINK_V0;
  nfg->res_id = htons(0);
  
  proto = IPSET_PROTOCOL;
  add_attr(nlh, IPSET_ATTR_PROTOCOL, sizeof(proto), &proto);
  add_attr(nlh, IPSET_ATTR_SETNAME, strlen(setname) + 1, setname);
  nested[0] = (struct my_nlattr *)(buffer + NL_ALIGN(nlh->nlmsg_len));
  nlh->nlmsg_len += NL_ALIGN(sizeof(struct my_nlattr));
  nested[0]->nla_type = NLA_F_NESTED | IPSET_ATTR_DATA;
  nested[1] = (struct my_nlattr *)(buffer + NL_ALIGN(nlh->nlmsg_len));
  nlh->nlmsg_len += NL_ALIGN(sizeof(struct my_nlattr));
  nested[1]->nla_type = NLA_F_NESTED | IPSET_ATTR_IP;
  add_attr(nlh, 
	   (af == AF_INET ? IPSET_ATTR_IPADDR_IPV4 : IPSET_ATTR_IPADDR_IPV6) | NLA_F_NET_BYTEORDER,
	   addrsz, ipaddr);
  nested[1]->nla_len = (void *)buffer + NL_ALIGN(nlh->nlmsg_len) - (void *)nested[1];
  nested[0]->nla_len = (void *)buffer + NL_ALIGN(nlh->nlmsg_len) - (void *)nested[0];
	
  while (retry_send(sendto(ipset_sock, buffer, nlh->nlmsg_len, 0,
			   (struct sockaddr *)&snl, sizeof(snl))));
								    
  return errno == 0 ? 0 : -1;
}


static int old_add_to_ipset(const char *setname, const union all_addr *ipaddr, int remove)
{
  socklen_t size;
  struct ip_set_req_adt_get {
    unsigned op;
    unsigned version;
    union {
      char name[IPSET_MAXNAMELEN];
      uint16_t index;
    } set;
    char typename[IPSET_MAXNAMELEN];
  } req_adt_get;
  struct ip_set_req_adt {
    unsigned op;
    uint16_t index;
    uint32_t ip;
  } req_adt;
  
  if (strlen(setname) >= sizeof(req_adt_get.set.name)) 
    {
      errno = ENAMETOOLONG;
      return -1;
    }
  
  req_adt_get.op = 0x10;
  req_adt_get.version = 3;
  strcpy(req_adt_get.set.name, setname);
  size = sizeof(req_adt_get);
  if (getsockopt(ipset_sock, SOL_IP, 83, &req_adt_get, &size) < 0)
    return -1;
  req_adt.op = remove ? 0x102 : 0x101;
  req_adt.index = req_adt_get.set.index;
  req_adt.ip = ntohl(ipaddr->addr4.s_addr);
  if (setsockopt(ipset_sock, SOL_IP, 83, &req_adt, sizeof(req_adt)) < 0)
    return -1;
  
  return 0;
}



int add_to_ipset(const char *setname, const union all_addr *ipaddr, int flags, int remove)
{
  int ret = 0, af = AF_INET;

  if (flags & F_IPV6)
    {
      af = AF_INET6;
    }
  
    ret = new_add_to_ipset(setname, ipaddr, af, remove);

  if (ret == -1)
     my_syslog(LOG_ERR, _("failed to update ipset %s: %s"), setname, strerror(errno));

  return ret;
}

#endif /* HAVE_LINUX_NETWORK */
#endif /* HAVE_IPSET */