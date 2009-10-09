/* Copyright (C) 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */

#include <ros/common.h>
#include <hashtable.h>
#include <stdio.h>
#include <string.h>
#include <kmalloc.h>

// File was not annotated and caused ivy based compilation to fail
// Someone should really annotate it.
#ifdef __DEPUTY__
#pragma nodeputy
#endif

#ifdef __SHARC__
#pragma nosharc
#endif

/*
Credit for primes table: Aaron Krowne
 http://br.endernet.org/~akrowne/
 http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
*/
static const unsigned int primes[] = {
53, 97, 193, 389,
769, 1543, 3079, 6151,
12289, 24593, 49157, 98317,
196613, 393241, 786433, 1572869,
3145739, 6291469, 12582917, 25165843,
50331653, 100663319, 201326611, 402653189,
805306457, 1610612741
};
const unsigned int prime_table_length = sizeof(primes)/sizeof(primes[0]);
#define APPLY_MAX_LOAD_FACTOR(size) \
    ((size * 13)/20)
//const float max_load_factor = 0.65;

/*****************************************************************************/
hashtable_t *
create_hashtable(size_t minsize,
                 size_t (*hashf) (void*),
                 ssize_t (*eqf) (void*,void*))
{
    hashtable_t *h;
    size_t pindex, size = primes[0];
    /* Check requested hashtable isn't too large */
    if (minsize > (1u << 30)) return NULL;
    /* Enforce size as prime */
    for (pindex=0; pindex < prime_table_length; pindex++) {
        if (primes[pindex] > minsize) { size = primes[pindex]; break; }
    }
    h = (hashtable_t *)kmalloc(sizeof(hashtable_t), 0);
    if (NULL == h) return NULL; /*oom*/
    h->table = (hash_entry_t **)kmalloc(sizeof(hash_entry_t*) * size, 0);
    if (NULL == h->table) { kfree(h); return NULL; } /*oom*/
    memset(h->table, 0, size * sizeof(hash_entry_t *));
    h->tablelength  = size;
    h->primeindex   = pindex;
    h->entrycount   = 0;
    h->hashfn       = hashf;
    h->eqfn         = eqf;
    h->loadlimit    = APPLY_MAX_LOAD_FACTOR(size);
    return h;
}

/*****************************************************************************/
size_t
hash(hashtable_t *h, void *k)
{
    /* Aim to protect against poor hash functions by adding logic here
     * - logic taken from java 1.4 hashtable source */
    size_t i = h->hashfn(k);
    i += ~(i << 9);
    i ^=  ((i >> 14) | (i << 18)); /* >>> */
    i +=  (i << 4);
    i ^=  ((i >> 10) | (i << 22)); /* >>> */
    return i;
}

/*****************************************************************************/
static ssize_t
hashtable_expand(hashtable_t *h)
{
    /* Double the size of the table to accomodate more entries */
    hash_entry_t **newtable;
    hash_entry_t *e;
    hash_entry_t **pE;
    size_t newsize, i, index;
    /* Check we're not hitting max capacity */
    if (h->primeindex == (prime_table_length - 1)) return 0;
    newsize = primes[++(h->primeindex)];

    newtable = (hash_entry_t **)kmalloc(sizeof(hash_entry_t*) * newsize, 0);
    if (NULL != newtable)
    {
        memset(newtable, 0, newsize * sizeof(hash_entry_t*));
        /* This algorithm is not 'stable'. ie. it reverses the list
         * when it transfers entries between the tables */
        for (i = 0; i < h->tablelength; i++) {
            while (NULL != (e = h->table[i])) {
                h->table[i] = e->next;
                index = indexFor(newsize,e->h);
                e->next = newtable[index];
                newtable[index] = e;
            }
        }
        kfree(h->table);
        h->table = newtable;
    }
    /* Plan B: realloc instead */
    else 
    {
        newtable = (hash_entry_t**)
                   krealloc(h->table, newsize*sizeof(hash_entry_t*), 0);
        if (NULL == newtable) { (h->primeindex)--; return 0; }
        h->table = newtable;
        memset(newtable[h->tablelength], 0, newsize - h->tablelength);
        for (i = 0; i < h->tablelength; i++) {
            for (pE = &(newtable[i]), e = *pE; e != NULL; e = *pE) {
                index = indexFor(newsize,e->h);
                if (index == i)
                {
                    pE = &(e->next);
                }
                else
                {
                    *pE = e->next;
                    e->next = newtable[index];
                    newtable[index] = e;
                }
            }
        }
    }
    h->tablelength = newsize;
    h->loadlimit   = APPLY_MAX_LOAD_FACTOR(newsize);
    return -1;
}

/*****************************************************************************/
size_t
hashtable_count(hashtable_t *h)
{
    return h->entrycount;
}

/*****************************************************************************/
ssize_t
hashtable_insert(hashtable_t *h, void *k, void *v)
{
    /* This method allows duplicate keys - but they shouldn't be used */
    size_t index;
    hash_entry_t *e;
    if (++(h->entrycount) > h->loadlimit)
    {
        /* Ignore the return value. If expand fails, we should
         * still try cramming just this value into the existing table
         * -- we may not have memory for a larger table, but one more
         * element may be ok. Next time we insert, we'll try expanding again.*/
        hashtable_expand(h);
    }
    e = (hash_entry_t *)kmalloc(sizeof(hash_entry_t), 0);
    if (NULL == e) { --(h->entrycount); return 0; } /*oom*/
    e->h = hash(h,k);
    index = indexFor(h->tablelength,e->h);
    e->k = k;
    e->v = v;
    e->next = h->table[index];
    h->table[index] = e;
    return -1;
}

/*****************************************************************************/
void * /* returns value associated with key */
hashtable_search(hashtable_t *h, void *k)
{
    hash_entry_t *e;
    size_t hashvalue, index;
    hashvalue = hash(h,k);
    index = indexFor(h->tablelength,hashvalue);
    e = h->table[index];
    while (NULL != e)
    {
        /* Check hash value to short circuit heavier comparison */
        if ((hashvalue == e->h) && (h->eqfn(k, e->k))) return e->v;
        e = e->next;
    }
    return NULL;
}

/*****************************************************************************/
void * /* returns value associated with key */
hashtable_remove(hashtable_t *h, void *k)
{
    /* TODO: consider compacting the table when the load factor drops enough,
     *       or provide a 'compact' method. */

    hash_entry_t *e;
    hash_entry_t **pE;
    void *v;
    size_t hashvalue, index;

    hashvalue = hash(h,k);
    index = indexFor(h->tablelength,hash(h,k));
    pE = &(h->table[index]);
    e = *pE;
    while (NULL != e)
    {
        /* Check hash value to short circuit heavier comparison */
        if ((hashvalue == e->h) && (h->eqfn(k, e->k)))
        {
            *pE = e->next;
            h->entrycount--;
            v = e->v;
            freekey(e->k);
            kfree(e);
            return v;
        }
        pE = &(e->next);
        e = e->next;
    }
    return NULL;
}

/*****************************************************************************/
/* destroy */
void
hashtable_destroy(hashtable_t *h, int free_values)
{
    size_t i;
    hash_entry_t *e, *f;
    hash_entry_t **table = h->table;
    if (free_values)
    {
        for (i = 0; i < h->tablelength; i++)
        {
            e = table[i];
            while (NULL != e)
            { f = e; e = e->next; freekey(f->k); kfree(f->v); kfree(f); }
        }
    }
    else
    {
        for (i = 0; i < h->tablelength; i++)
        {
            e = table[i];
            while (NULL != e)
            { f = e; e = e->next; freekey(f->k); kfree(f); }
        }
    }
    kfree(h->table);
    kfree(h);
}

/*
 * Copyright (c) 2002, Christopher Clark
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
