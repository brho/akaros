/* Copyright (C) 2002, 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */

#ifndef __ROS_KERN_HASHTABLE_ITR_H__
#define ___ROS_KERN_HASHTABLE_ITR_H__

#include <hashtable.h>

/*****************************************************************************/
/* This struct is only concrete here to allow the inlining of two of the
 * accessor functions. */
typedef struct hashtable_itr {
    hashtable_t *h;
    hash_entry_t *e;
    hash_entry_t *parent;
    size_t index;
} hashtable_itr_t;

/*****************************************************************************/
/* hashtable_iterator
 */

hashtable_itr_t *
hashtable_iterator(hashtable_t *h);

/*****************************************************************************/
/* hashtable_iterator_key
 * - return the value of the (key,value) pair at the current position */

extern inline void *
hashtable_iterator_key(hashtable_itr_t *i)
{
    return i->e->k;
}

/*****************************************************************************/
/* value - return the value of the (key,value) pair at the current position */

extern inline void *
hashtable_iterator_value(hashtable_itr_t *i)
{
    return i->e->v;
}

/*****************************************************************************/
/* advance - advance the iterator to the next element
 *           returns zero if advanced to end of table */

ssize_t
hashtable_iterator_advance(hashtable_itr_t *itr);

/*****************************************************************************/
/* remove - remove current element and advance the iterator to the next element
 *          NB: if you need the value to free it, read it before
 *          removing. ie: beware memory leaks!
 *          returns zero if advanced to end of table */

ssize_t
hashtable_iterator_remove(hashtable_itr_t *itr);

/*****************************************************************************/
/* search - overwrite the supplied iterator, to point to the entry
 *          matching the supplied key.
            h points to the hashtable to be searched.
 *          returns zero if not found. */
ssize_t
hashtable_iterator_search(hashtable_itr_t *itr,
                          hashtable_t *h, void *k);

#define DEFINE_HASHTABLE_ITERATOR_SEARCH(fnname, keytype) \
ssize_t fnname (hashtable_itr_t *i, hashtable_t *h, keytype *k) \
{ \
    return (hashtable_iterator_search(i,h,k)); \
}



#endif /* __ROS_KERN_HASHTABLE_ITR_H__*/

/*
 * Copyright (c) 2002, 2004, Christopher Clark
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
