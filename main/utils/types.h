
#ifndef TYPES_H_
#define TYPES_H_

#include "stdint.h"
//typedef unsigned char uint8_t;
//typedef unsigned short uint16_t;
//typedef unsigned int uint32_t;
//typedef int int32_t;

#define TYPEDEF(x) typedef int##x##_t int##x
#define TYPEDEF_U(x) typedef uint##x##_t uint##x

TYPEDEF(32);

TYPEDEF_U(8);
TYPEDEF_U(16);
TYPEDEF_U(32);

#endif /* TYPES_H_ */
