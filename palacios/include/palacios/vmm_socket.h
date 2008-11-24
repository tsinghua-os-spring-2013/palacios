/*
 * This file is part of the Palacios Virtual Machine Monitor developed
 * by the V3VEE Project with funding from the United States National 
 * Science Foundation and the Department of Energy.  
 *
 * The V3VEE Project is a joint project between Northwestern University
 * and the University of New Mexico.  You can find out more at 
 * http://www.v3vee.org
 *
 * Copyright (c) 2008, Jack Lange <jarusl@cs.northwestern.edu> 
 * Copyright (c) 2008, The V3VEE Project <http://www.v3vee.org> 
 * All rights reserved.
 *
 * Author: Jack Lange <jarusl@cs.northwestern.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the file "V3VEE_LICENSE".
 */


#ifndef __VMM_SOCKET_H__
#define __VMM_SOCKET_H__

#include <palacios/vmm.h>

#define V3_SOCK void *

#ifdef __V3VEE__

#define V3_Create_UDP_Socket() ({			\
      extern struct v3_socket_hooks * sock_hooks;	\
      V3_SOCK sock = 0;					\
      if ((sock_hooks) && (sock_hooks)->udp_socket) {	\
	sock = (sock_hooks)->udp_socket(0,0);		\
      }							\
      sock;						\
    })



#define V3_Create_TCP_Socket() ({			\
      extern struct v3_socket_hooks * sock_hooks;	\
      V3_SOCK sock = 0;					\
      if ((sock_hooks) && (sock_hooks)->tcp_socket) {	\
	sock = (sock_hooks)->tcp_socket(0,0);		\
      }							\
      sock;						\
    })


#define V3_Close_Socket(sock) \
  do {							\
    extern struct v3_socket_hooks * sock_hooks;		\
    if ((sock_hooks) && (sock_hooks)->close) {		\
      (sock_hooks)->close(sock);			\
    }							\
  } while (0);



#define V3_Bind_Socket(sock, port) ({				\
      extern struct v3_socket_hooks * sock_hooks;		\
      int ret = -1;						\
      if ((sock_hooks) && (sock_hooks)->bind) {			\
	ret = (sock_hooks)->bind(sock, port);			\
      }								\
      ret;							\
    })


#define V3_Accept_Socket() (-1)
#define V3_Select_Socket() (-1)



#define V3_Connect_To_IP(sock, ip, port) ({			\
      extern struct v3_socket_hooks * sock_hooks;		\
      int ret = -1;						\
      if ((sock_hooks) && (sock_hooks)->connect_to_ip) {	\
	ret = (sock_hooks)->connect_to_ip(sock, ip, port);	\
      }								\
      ret;							\
    })


#define V3_Connect_To_Host(sock, hostname, port) ({			\
      extern struct v3_socket_hooks * sock_hooks;			\
      int ret = -1;							\
      if ((sock_hooks) && (sock_hooks)->connect_to_host) {		\
	ret = (sock_hooks)->connect_to_host(sock, hostname, port);	\
      }									\
      ret;								\
    })


#define V3_Send(sock, buf, len) ({				\
      extern struct v3_socket_hooks * sock_hooks;		\
      int ret = -1;						\
      if ((sock_hooks) && (sock_hooks)->send) {			\
	ret = (sock_hooks)->send(sock, buf, len);		\
      }								\
      ret;							\
    })

#define V3_Recv(sock, buf, len) ({				\
      extern struct v3_socket_hooks * sock_hooks;		\
      int ret = -1;						\
      if ((sock_hooks) && (sock_hooks)->recv) {			\
	ret = (sock_hooks)->recv(sock, buf, len);		\
      }								\
      ret;							\
    })

#define V3_SendTo_Host(sock, hostname, port, buf, len) ({		\
      extern struct v3_socket_hooks * sock_hooks;			\
      int ret = -1;							\
      if ((sock_hooks) && (sock_hooks)->sendto_host) {			\
	ret = (sock_hooks)->sendto_host(sock, hostname, port, buf, len); \
      }									\
      ret;								\
    })


#define V3_SendTo_IP(sock, ip, port, buf, len) ({			\
      extern struct v3_socket_hooks * sock_hooks;			\
      int ret = -1;							\
      if ((sock_hooks) && (sock_hooks)->sendto_ip) {			\
	ret = (sock_hooks)->sendto_ip(sock, ip, port, buf, len);	\
      }									\
      ret;								\
    })


#define V3_RecvFrom_Host(sock, hostname, port, buf, len) ({		\
      extern struct v3_socket_hooks * sock_hooks;			\
      int ret = -1;							\
      if ((sock_hooks) && (sock_hooks)->recvfrom_host) {			\
	ret = (sock_hooks)->recvfrom_host(sock, hostname, port, buf, len); \
      }									\
      ret;								\
    })


#define V3_RecvFrom_IP(sock, ip, port, buf, len) ({			\
      extern struct v3_socket_hooks * sock_hooks;			\
      int ret = -1;							\
      if ((sock_hooks) && (sock_hooks)->recvfrom_ip) {			\
	ret = (sock_hooks)->recvfrom_ip(sock, ip, port, buf, len);	\
      }									\
      ret;								\
    })


#endif


struct v3_timeval {
  long    tv_sec;         /* seconds */
  long    tv_usec;        /* and microseconds */
};

struct v3_sock_set {
  V3_SOCK sock;
  unsigned int is_set;
  struct v3_sock_set * next;
};


#define v3_set_sock(n, p)  ((p)->fd_bits[(n)/8] |=  (1 << ((n) & 7)))
#define v3_clr_sock(n, p)  ((p)->fd_bits[(n)/8] &= ~(1 << ((n) & 7)))
#define v3_isset_sock(n,p) ((p)->fd_bits[(n)/8] &   (1 << ((n) & 7)))
#define v3_zero_sockset(p)    


struct v3_socket_hooks {
  // Socket creation routines
  V3_SOCK (*tcp_socket)(const int bufsize, const int nodelay, const int nonblocking);
  V3_SOCK (*udp_socket)(const int bufsize, const int nonblocking);

  // Socket Destruction
  void (*close)(V3_SOCK sock);

  // Network Server Calls
  int (*bind_socket)(const V3_SOCK sock, const int port);
  
  int (*accept)(const V3_SOCK const sock);
  // This going to suck
  int (*select)(struct v3_sock_set * rset, \
		struct v3_sock_set * wset, \
		struct v3_sock_set * eset, \
		struct v3_timeval tv);

  // Connect calls
  int (*connect_to_ip)(const V3_SOCK sock, const int hostip, const int port);
  int (*connect_to_host)(const V3_SOCK sock, const char * hostname, const int port);

  // TCP Data Transfer
  int (*send)(const V3_SOCK sock, const char * buf, const int len);
  int (*recv)(const V3_SOCK sock, char * buf, const int len);
  
  // UDP Data Transfer
  int (*sendto_host)(const V3_SOCK sock, const char * hostname, const int port, 
		    const char * buf, const int len);
  int (*sendto_ip)(const V3_SOCK sock, const int ip_addr, const int port, 
		  const char * buf, const int len);
  
  int (*recvfrom_host)(const V3_SOCK sock, const char * hostname, const int port, 
			 char * buf, const int len);
  int (*recvfrom_ip)(const V3_SOCK sock, const int ip_addr, const int port, 
			 char * buf, const int len);
};


void V3_Init_Socket(struct v3_socket_hooks * hooks);


#endif
