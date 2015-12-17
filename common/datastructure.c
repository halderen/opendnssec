/*
 * Copyright (c) 2015 NLNet Labs. All rights reserved.
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
    void* cargo;
    pthread_mutex_t mutex;
    int (*member_destroy)(void* cargo, void* member);
    int (*member_dispose)(void* cargo, void* member, FILE*);
    int (*member_restore)(void* cargo, void* member, FILE*);
    struct collection_instance_struct* first;
    struct collection_instance_struct* last;
    int count;
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
    if(fseek(collection->method->store, collection->location, SEEK_SET))
        return 1;
    for(i=0; i<collection->count; i++) {
        if(collection->method->member_restore(collection->method->cargo,
                collection->array + collection->size * i, collection->method->store))
            return 1;
    }
    return 0;
}

static int
swapout(collection_t collection)
{
    int i;
    if(fseek(collection->method->store, 0, SEEK_END))
        return 1;
    collection->location = ftell(collection->method->store);
    for(i=0; i<collection->count; i++) {
        if(collection->method->member_dispose(collection->method->cargo,
                collection->array + collection->size * i, collection->method->store))
            return 1;
    }
    return 0;
}

static void
swapinassert(collection_t collection)
{
    int needsswapin = 1;
    struct collection_instance_struct* least;
    if(!collection->method->store)
        /* no backing store, item always in memory */
        return;
    if(collection->count == 0)
        needsswapin = 0;
    if(collection->method->first == collection)
        /* most recent item optimization, nothing to do */
        return;
    pthread_mutex_lock(collection->method->mutex);
    if(collection->next != collection->prev) {
        /* item in contained in chain, remove from current position */
        collection->method->count--;
        if(collection->next == NULL) {
            assert(collection->method->last == collection);
            collection->method->last = collection->prev;
        } else
            collection->next->prev = collection->prev;
        if(collection->prev == NULL) {
            assert(collection->method->first == collection);
            collection->method->first = collection->next;
        } else
            collection->prev->next = collection->next;
        needsswapin = 0;
    }
    /* insert item in front of LRU chain */
    collection->next = collection->method->first;
    if(collection->method->first != NULL)
        collection->method->first->prev = collection;
    collection->method->first = collection;
    if(collection->method->last == NULL)
        collection->method->last = collection;
    collection->prev = NULL;
    collection->method->count++;
    /* look whether threshold is exceeded */
    if(collection->method->count > 100000) {
        least = collection->method->last;
        swapout(least);
        collection->method->count--;
        least->prev->next = NULL;
        collection->method->last = least->prev;
        least->prev = least->next = least;
    }
    pthread_mutex_unlock(collection->method->mutex);
    if(needsswapin) {
        swapin(collection);
    }
}

void
collection_class_allocated(collection_class* klass, void *cargo,
        int (*member_destroy)(void* cargo, void* member))
{
    CHECKALLOC(*klass = malloc(sizeof(struct collection_class_struct)));
    (*klass)->cargo = cargo;
    (*klass)->member_destroy = member_destroy;
    (*klass)->member_dispose = NULL;
    (*klass)->member_restore = NULL;
    (*klass)->store = NULL;
}

void
collection_class_backed(collection_class* klass, char* fname, void *cargo,
        int (*member_destroy)(void* cargo, void* member),
        int (*member_dispose)(void* cargo, void* member, FILE*),
        int (*member_restore)(void* cargo, void* member, FILE*))
{
    CHECKALLOC(*klass = malloc(sizeof(struct collection_class_struct)));
    (*klass)->cargo = cargo;
    (*klass)->member_destroy = member_destroy;
    (*klass)->member_dispose = member_dispose;
    (*klass)->member_restore = member_restore;
    (*klass)->first = NULL;
    (*klass)->last = NULL;
    (*klass)->store = fopen(fname, "w+");
    pthread_mutex_init(&(*klass)->mutex, NULL);
}

void
collection_class_destroy(collection_class* klass)
{
    if (klass == NULL)
        return;
    if((*klass)->store != NULL) {
        fclose((*klass)->store);
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
        (*collection)->method->member_destroy((*collection)->method->cargo,
                &(*collection)->array[(*collection)->size * i]);
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
    swapinassert(collection);
    CHECKALLOC(ptr = realloc(collection->array, (collection->count+1)*collection->size));
    collection->array = ptr;
    memcpy(collection->array + collection->size * collection->count, data, collection->size);
    collection->count += 1;
}

void
collection_del_index(collection_t collection, int index)
{
    void* ptr;
    if (index<0 || index >= collection->count)
        return;
    swapinassert(collection);
    collection->method->member_destroy(&collection->array[collection->size * index]);
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
}

void
collection_del_cursor(collection_t collection)
{
    collection_del_index(collection, collection->iterator);
}

void*
collection_iterator(collection_t collection)
{
    if(collection->iterator < 0) {
        swapinassert(collection);
        collection->iterator = collection->count;
    }
    collection->iterator -= 1;
    if(collection->iterator >= 0) {
        return &collection->array[collection->iterator * collection->size];
    } else {
        return NULL;
    }
}
