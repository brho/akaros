/* See COPYRIGHT for copyright information. */

#pragma once

#include <ros/errno.h>

typedef int error_t;

#define MAX_ERRNO		4095

#define ERR_PTR(err)  ((void *)((intptr_t)(err)))
#define PTR_ERR(ptr)  ((intptr_t)(ptr))
#define IS_ERR(ptr)   (-(intptr_t)(ptr) <= MAX_ERRNO)
#define IS_ERR_OR_NULL(ptr) (!(ptr) || IS_ERR(ptr))
