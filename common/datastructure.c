/*
 * Copyright (c) 2015-2016 NLNet Labs. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "status.h"
#include "datastructure.h"

struct collection_class_struct {
    FILE* store;
    pthread_mutex_t mutex;
    int (*member_destroy)(void* member);
    int (*member_dispose)(void* member, FILE*);
    int (*member_restore)(void* member, FILE*);
    int (*obtain)(collection_t);
    int (*release)(collection_t);
    struct collection_instance_struct* first;
    struct collection_instance_struct* last;
    int count;
    int cachesize;
};

struct collection_instance_struct {
    struct collection_class_struct* method;
    char* array; /** array with members */
    size_t size; /** member size */
    int iterator;
    int count; /** number of members in array */
    long location;
    struct collection_instance_struct* next; 
    struct collection_instance_struct* prev;
};

static int
swapin(collection_t collection)
{
    int i;
    collection_class method = collection->method;
    if(fseek(method->store, collection->location, SEEK_SET))
        return 1;
    for(i=0; i<collection->count; i++) {
        if(method->member_restore(&collection->array[collection->size * i], method->store))
            return 1;
    }
    return 0;
}

static int
swapout(collection_t collection)
{
    int i;
    collection_class method = collection->method;
    if(fseek(method->store, 0, SEEK_END)) {
        return 1;
    }
    collection->location = ftell(method->store);
    for(i=0; i<collection->count; i++) {
        if(method->member_dispose(&collection->array[collection->size * i], method->store)) {
            return 1;
        }
    }
    return 0;
}

static void
assure(collection_t collection)
{
    int needsswapin = 1;
    struct collection_instance_struct* least;
    collection_class method = collection->method;
    if(collection->count == 0)
        needsswapin = 0;
    if(method->first == collection) {
        /* most recent item optimization, nothing to do */
        return;
    }
    pthread_mutex_lock(&method->mutex);
    if(collection != collection->next && collection->prev != NULL && collection->next != NULL) {
        /* item in contained in chain, remove from current position */
        method->count--;
        if(collection->next == NULL) {
            assert(method->last == collection);
            method->last = collection->prev;
        } else
            collection->next->prev = collection->prev;
        if(collection->prev == NULL) {
            assert(method->first == collection);
            method->first = collection->next;
        } else
            collection->prev->next = collection->next;
        needsswapin = 0;
    }
    /* insert item in front of LRU chain */
    collection->next = method->first;
    if(method->first != NULL)
        method->first->prev = collection;
    method->first = collection;
    if(method->last == NULL)
        method->last = collection;
    collection->prev = NULL;
    method->count++;
    /* look whether threshold is exceeded */
    while(method->count > method->cachesize) {
        least = method->last;
        swapout(least);
        method->count--;
        if(least->prev == NULL) {
            assert(method->first == least);
            method->first = NULL;
        } else
            least->prev->next = NULL;
        method->last = least->prev;
        least->prev = least;
        least->next = least;
    }
    pthread_mutex_unlock(&method->mutex);
    if(needsswapin) {
        swapin(collection);
    }
}

static int
obtain(collection_t collection)
{
    assure(collection);
    return 0;
}

static int
release(collection_t collection)
{
    (void)collection;
    return 0;
}

static int
noop(collection_t collection)
{
    (void)collection;
    return 0;
}

void
collection_class_create(collection_class* method, char* fname,
        int (*member_destroy)(void* member),
        int (*member_dispose)(void* member, FILE*),
        int (*member_restore)(void* member, FILE*))
{
    char* configoption;
    char* endptr;
    long cachesize;
    CHECKALLOC(*method = malloc(sizeof(struct collection_class_struct)));
    (*method)->member_destroy = member_destroy;
    (*method)->member_dispose = member_dispose;
    (*method)->member_restore = member_restore;
    (*method)->obtain = noop;
    (*method)->release = noop;
    (*method)->cachesize = -1;
    (*method)->count = 0;
    (*method)->first = NULL;
    (*method)->last = NULL;
    (*method)->store = NULL;
    configoption = getenv("OPENDNSSEC_OPTION_sigstore");
    if(configoption != NULL) {
        cachesize = strtol(configoption, &endptr, 0);
        if(endptr != configoption && cachesize > 0) {
            (*method)->store = fopen(fname, "w+");
            (*method)->cachesize = cachesize;
            pthread_mutex_init(&(*method)->mutex, NULL);
            (*method)->obtain = obtain;
            (*method)->release = release;
        }
    }
}

void
collection_class_destroy(collection_class* klass)
{
    if (klass == NULL)
        return;
    if((*klass)->store != NULL) {
        fclose((*klass)->store);
        if((*klass)->cachesize > 0)
            pthread_mutex_destroy(&(*klass)->mutex);
    }
    free(*klass);
    *klass = NULL;
}

void
collection_create_array(collection_t* collection, size_t membsize,
        collection_class klass)
{
    CHECKALLOC(*collection = malloc(sizeof(struct collection_instance_struct)));
    (*collection)->size = membsize;
    (*collection)->count = 0;
    (*collection)->array = NULL;
    (*collection)->iterator = -1;
    (*collection)->method = klass;
    (*collection)->next = (*collection)->prev = *collection;
}

void
collection_destroy(collection_t* collection)
{
    int i;
    if(collection == NULL)
        return;
    for (i=0; i < (*collection)->count; i++) {
        (*collection)->method->member_destroy(&(*collection)->array[(*collection)->size * i]);
    }
    if((*collection)->array)
        free((*collection)->array);
    free(*collection);
    *collection = NULL;
}

void
collection_add(collection_t collection, void *data)
{
    void* ptr;
    collection_class method = collection->method;
    method->obtain(collection);
    CHECKALLOC(ptr = realloc(collection->array, (collection->count+1)*collection->size));
    collection->array = ptr;
    memcpy(collection->array + collection->size * collection->count, data, collection->size);
    collection->count += 1;
    method->release(collection);
}

void
collection_del_index(collection_t collection, int index)
{
    void* ptr;
    collection_class method = collection->method;
    if (index<0 || index >= collection->count)
        return;
    method->obtain(collection);
    method->member_destroy(&collection->array[collection->size * index]);
    memmove(collection->array + collection->size * index, &collection->array + collection->size * (index + 1), (collection->count - index) * collection->size);
    collection->count -= 1;
    memmove(&collection->array[collection->size * index], &collection->array[collection->size * (index + 1)], (collection->count - index) * collection->size);
    if (collection->count > 0) {
        CHECKALLOC(ptr = realloc(collection->array, collection->count * collection->size));
        collection->array = ptr;
    } else {
        free(collection->array);
        collection->array = NULL;
    }
    method->release(collection);
}

void
collection_del_cursor(collection_t collection)
{
    collection_del_index(collection, collection->iterator);
}

void*
collection_iterator(collection_t collection)
{
    collection_class method = collection->method;
    if(collection->iterator < 0) {
        method->obtain(collection);
        collection->iterator = collection->count;
    }
    collection->iterator -= 1;
    if(collection->iterator >= 0) {
        return &collection->array[collection->iterator * collection->size];
    } else {
        method->release(collection);
        return NULL;
    }
}
