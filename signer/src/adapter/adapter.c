/*
 * Copyright (c) 2009 NLNet Labs. All rights reserved.
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

/**
 * Inbound and Outbound Adapters.
 *
 */

#include "adapter/adapter.h"
#include "file.h"
#include "log.h"
#include "status.h"
#include "signer/zone.h"

#include <stdlib.h>

static const char* adapter_str = "adapter";


/**
 * Create a new adapter.
 *
 */
adapter_type*
adapter_create(const char* str, adapter_mode type, unsigned in)
{
    adapter_type* adapter = NULL;
    CHECKALLOC(adapter = (adapter_type*) malloc(sizeof(adapter_type)));
    adapter->type = type;
    adapter->inbound = in;
    adapter->error = 0;
    adapter->config = NULL;
    adapter->config_last_modified = 0;
    adapter->configstr = strdup(str);
    if (!adapter->configstr) {
        ods_log_error("[%s] unable to create adapter: allocator_strdup() "
            "failed", adapter_str);
        adapter_cleanup(adapter);
        return NULL;
    }
    /* type specific */
    switch(adapter->type) {
        case ADAPTER_FILE:
            break;
        default:
            break;
    }
    return adapter;
}


/*
 * Read zone from input adapter.
 *
 */
ods_status
adapter_read(zone_type* zone)
{
            return adfile_read(zone);
}


/**
 * Write zone to output adapter.
 *
 */
ods_status
adapter_write(zone_type* zone)
{
            return adfile_write(zone, "BERRY");
}


/**
 * Clean up adapter.
 *
 */
void
adapter_cleanup(adapter_type* adapter)
{
    if (!adapter) {
        return;
    }
    free(adapter);
}
