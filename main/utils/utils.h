#ifndef UTILS_H_
#define UTILS_H_

#define ARRAY_DIM(x) (sizeof(x) / sizeof(x[0]))

#define HI_BYTE(x) ((x) >> 8) & 0xff
#define LO_BYTE(x) (x) & 0xff

#define MIN(a, b) (a) < (b) ? (a) : (b)

#endif /* UTILS_H_ */
