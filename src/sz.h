// Copyright (c) 2024 Thomas Mikalsen. Subject to the MIT License 
#ifndef __SZ_H__
#define __SZ_H__

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*! \brief Determine if a string starts with a given prefix.*/
bool sz_starts_with(const char * sz, const char * prefix);

/*! \brief Determine if a string starts with a given prefix. */
bool sz_starts_with_case(const char * sz, const char * prefix, bool ignore_case);

/*! \brief Determine if a string contains a given substring. */
bool sz_contains(const char * sz, const char * substr);

/*! \brief Determine if a string contains a given substring. */
bool sz_contains_case(const char * sz, const char * substr, bool ignore_case);

/*! \brief Transforms the string to lower-cas. */
char * sz_to_lower(char * sz);

/*! \brief Determine if two strings are equal. */
bool sz_equal(const char * sz1, const char * sz2);

/*! \brief Determine if two strings are equal, ignoring case. */
bool sz_equal_ignore_case(const char * sz1, const char * sz2);

/*! \brief Trim leading and trailing white space.
 */
char * sz_trim(char * sz);

/*! \brief Concatenate two strings */
char * sz_cat(const char * sz1, const char * sz2);

/*! \brief Determine if the given string exists in the given array of strings */
bool sz_is_in_szv(const char * sz, size_t szc, const char ** szv);

/*! \brief A pool of strings on the heap */
typedef struct Sz_Pool_S * Sz_Pool;

/*! \brief Create a new pool of strings */
Sz_Pool szp_create(size_t init_cap);
/* !\brief Add a duplicate of a string to the pool.
 */
char * szp_strdup(Sz_Pool pool, const char * sz);
/*! \brief Return the number of strings in the pool */
size_t szp_size(Sz_Pool pool);
/*! \brief Get he string associated with the given index */
char * szp_get(Sz_Pool pool, size_t i);
/*! \brief Write the strings to the given file */
void szp_dump(Sz_Pool pool, FILE * fp);
/*! \brief Remove (and free) all strings from the pool. */
void szp_clear(Sz_Pool pool);
/*! \brief Remove (and free) all strings from the pool, and free the pool itself.  */
void szp_free(Sz_Pool pool);
/*! \brief Create a pool of strings from the given file */
Sz_Pool szp_from_file(char * file);


#endif // __SZ_H__
