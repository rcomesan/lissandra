#ifndef CX_MATH_H_
#define CX_MATH_H_

#define cx_math_min(x, y) (((x) < (y)) ? (x) : (y))
#define cx_math_max(x, y) (((x) > (y)) ? (x) : (y))
#define cx_math_in_range(x, min, max) ((x) >= (min) && (x) <= (max))

#endif // CX_MATH_