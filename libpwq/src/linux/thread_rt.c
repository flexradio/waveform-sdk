/*
 * Copyright (c) 2011, Joakim Johansson <jocke@tbricks.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "platform.h"
#include "private.h"

void ptwq_set_current_thread_priority(int priority)
{
    int ret = 0;
    struct sched_param thread_priority = {0};

    dbg_printf("reconfiguring thread for priority level=%u", priority);

    switch (priority)
    {
        case WORKQ_LOW_PRIOQUEUE:
            fprintf(stderr, "Setting priority to low\n");
            thread_priority.sched_priority = sched_get_priority_max(SCHED_IDLE);
            break;
        case WORKQ_DEFAULT_PRIOQUEUE:
            fprintf(stderr, "Setting priority to default\n");
            thread_priority.sched_priority = sched_get_priority_max(SCHED_OTHER);
            break;
        case WORKQ_HIGH_PRIOQUEUE:
            fprintf(stderr, "Setting realtime priority\n");
            thread_priority.sched_priority = sched_get_priority_max(SCHED_FIFO);
            break;
        default:
            fprintf(stderr, "Unknown priority level = %u", priority);
            break;
    }

    ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &thread_priority);
    if(ret) {
        fprintf(stderr, "Setting thread priority: %s\n", strerror(ret));
    }

    return;
}
