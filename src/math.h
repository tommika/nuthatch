// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __MATH_H__
#define __MATH_H__


// See https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html#Statement-Exprs
#define min(a,b) ({ __typeof__ (a) _a = (a); \
                    __typeof__ (b) _b = (b); \
                    _a < _b ? _a : _b; })

#endif // __MATH_H__
