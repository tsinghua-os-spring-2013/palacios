/* (c) 2008, Jack Lange <jarusl@cs.northwestern.edu> */
/* (c) 2008, The V3VEE Project <http://www.v3vee.org> */

#ifndef __VMM_TYPES_H
#define __VMM_TYPES_H

#ifdef __V3VEE__
//#include <geekos/ktypes.h>


typedef signed char schar_t;
typedef unsigned char uchar_t;

typedef signed short sshort_t;
typedef unsigned short ushort_t;

typedef signed int sint_t;
typedef unsigned int uint_t;

typedef signed long long sllong_t;
typedef unsigned long long ullong_t;

typedef signed long slong_t;
typedef unsigned long ulong_t;

typedef unsigned long size_t;






typedef unsigned long long uint64_t;
typedef long long sint64_t;

typedef unsigned int uint32_t;
typedef int sint32_t;

typedef ulong_t addr_t;

#endif // ! __V3VEE__

#endif
