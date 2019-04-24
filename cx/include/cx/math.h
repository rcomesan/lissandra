#ifndef CX_MATH_H_
#define CX_MATH_H_

#define cx_math_min(_x, _y) (((_x) < (_y)) ? (_x) : (_y))
#define cx_math_max(_x, _y) (((_x) > (_y)) ? (_x) : (_y))
#define cx_math_in_range(_x, _min, _max) ((_x) >= (_min) && (_x) <= (_max))
#define cx_math_signum(_x) ((0 < (_x)) - ((_x) < 0))

#endif // CX_MATH_