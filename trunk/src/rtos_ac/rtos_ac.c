/*
 * RTOS application: test an RTOS
 *
 *    Copyright (C) 2009 Louis Caron
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rtos_ac/rtos_ac.h"
#include "common/Uart1.h"

#include "reg_gpio.h"
#include "reg_crm.h"

/***************************************************************************************/

char task_stack[TASK_CNT][TASK_STACK_SIZE];



struct task_desc task_desc_tab[TASK_CNT + 1] = {
    { {0}, 1, 0, {NULL,          NULL,                NULL},  NULL, NULL, NULL },
    { {0}, 0, 0, {task_stack[1], task_ending_handler, task1}, NULL, NULL, NULL },
    { {0}, 0, 0, {task_stack[2], task_ending_handler, task2}, NULL, NULL, NULL },
    { {0}, 0, 0, {task_stack[3], task_ending_handler, task3}, NULL, NULL, NULL },
};

int task_current = 0;
//struct task_desc    *task_queue = NULL;

struct task_msg *context_start(struct task_msg *req, struct task_reg_init *ctx_init, uint32_t *ctx_save);
struct task_msg *context_start2(struct task_msg *req, struct task_reg_init *ctx_init);
struct task_msg *context_switch(struct task_msg *req, uint32_t *ctx_restore, uint32_t *ctx_save);
struct task_msg *context_switch2(struct task_msg *req, uint32_t *ctx_restore);

/***************************************************************************************/


struct task_msg *task_malloc(uint32_t id, uint32_t param)
{
    struct task_msg *msg = (struct task_msg*) mem_alloc(sizeof(struct task_msg));
    msg->next = NULL;
    msg->sender_id = task_current;
    msg->id = id;
    msg->param = param;

    return msg;
}

struct task_msg *task_start(struct task_msg *req, int task_id)
{
    ASSERT (1 <= task_id && task_id <= TASK_CNT);
    ASSERT(!task_desc_tab[task_id].started);

    uint32_t * ctx_save = task_desc_tab[task_current].reg_save;

    task_desc_tab[task_id].started = 1;

    task_current = task_id;

    return context_start(req,
                         &task_desc_tab[task_current].reg_init,
                         ctx_save);
}



void task_send_ind(struct task_msg *ind, int task_id) {
    // must have 1 <= task_id <= TASK_CNT

    ASSERT (1 <= task_id && task_id <= TASK_CNT);
    ASSERT(ind); // should be possible to wake up task without ind

    //Uart1PutS(" - send_ind\n");

    //if (task_desc_tab[task_id].blocked) {
    //    // find end of msg sleep queue and add msg
    //    struct task_msg *msg_ptr = (struct task_msg*) &task_desc_tab[task_id].msg_sleep;
    //    while (msg_ptr->next) msg_ptr = msg_ptr->next;
    //    msg_ptr->next = msg;
    //
    //} else
    if (task_desc_tab[task_id].started)
    {
        // find end of msg queue and add msg
        struct task_msg *msg_ptr = (struct task_msg*) &task_desc_tab[task_id].msg_ind;
        while (msg_ptr->next) msg_ptr = msg_ptr->next;
        msg_ptr->next = ind;
        ind->next = NULL;

        // if sleeping msg, add them
        //if (task_desc_tab[task_id].msg_sleep) {
        //    msg->next = task_desc_tab[task_id].msg_sleep;
        //    task_desc_tab[task_id].msg_sleep = NULL;
        //}
    }
    else
    {
        mem_free(ind);
    }

    //task_desc_tab[task_id].flag |= TASK_PROG;
}

struct task_msg *task_send_req(struct task_msg *req, int task_id) {

    ASSERT (1 <= task_id && task_id <= TASK_CNT);

    //Uart1PutS(" - send_req\n");

    struct task_msg *msg_ptr = (struct task_msg*) &task_desc_tab[task_id].msg_req;
    while (msg_ptr->next) msg_ptr = msg_ptr->next;
    msg_ptr->next = req;
    req->next = NULL;

    task_desc_tab[task_current].blocked = 1;

    struct task_msg *ret;

    if (task_desc_tab[task_id].started == 0)
    {
        // return will be the called task!
        ret = task_start(req, task_id);
    }
    else
    {
        // run any other task
        // XXXXXX
        ret = task_schedule();
    }

    return ret;
}



struct task_msg *task_schedule(void)
{
    int task_new = task_current;
    struct task_msg *msg = NULL;

    for (;;) {
        task_new++;

        if (task_new == TASK_CNT + 1) {
            if (task_current == 0)
                return NULL;

            task_new = 0;
            break;
        }

        if (!task_desc_tab[task_new].blocked) {

            if (task_desc_tab[task_new].started) {
                if (task_desc_tab[task_new].msg_ind) {
                    msg = task_desc_tab[task_new].msg_ind;
                    task_desc_tab[task_new].msg_ind = msg->next;

                    ASSERT(!task_desc_tab[task_new].msg_ind_proc);
                    task_desc_tab[task_new].msg_ind_proc = msg;

                    break;
                }
            } else if (task_desc_tab[task_new].msg_req) {
                msg = task_desc_tab[task_new].msg_req;
                task_desc_tab[task_new].msg_req = msg->next;

                return task_start(msg, task_new);
            }
        }
    }

    uint32_t * ctx_save = task_desc_tab[task_current].reg_save;
    uint32_t * ctx_restore = task_desc_tab[task_new].reg_save;

    task_current = task_new;

    return context_switch(msg, ctx_restore, ctx_save);
}

void task_ending_handler(void * rsp)
{
    //Uart1PutS(" - task end\n");

    if (task_desc_tab[task_current].msg_ind_proc) {
        mem_free(task_desc_tab[task_current].msg_ind_proc);
        task_desc_tab[task_current].msg_ind_proc = NULL;
    }

    struct task_msg *msg = task_desc_tab[task_current].msg_req;
    if (msg) {
        task_desc_tab[task_current].msg_req = msg->next;
    }
    task_desc_tab[task_current].started = 0;

    if (msg && msg->sender_id) {
        struct task_desc *sender = task_desc_tab + msg->sender_id;
        ASSERT(sender->blocked && sender->started);
        sender->blocked = 0;

        //Uart1PutU32((uint32_t)return_value);
        //Uart1PutS("end!\n");

        task_current = msg->sender_id;
        context_switch2(rsp, task_desc_tab[task_current].reg_save);

    } else {
        task_schedule();
    }

    ASSERT(0);
}


void *task_wait(void)
{
    //Uart1PutS(" - task wait\n");

    if (task_desc_tab[task_current].msg_ind_proc) {
        mem_free(task_desc_tab[task_current].msg_ind_proc);
    }

    struct task_msg *msg = task_desc_tab[task_current].msg_ind;

    task_desc_tab[task_current].msg_ind_proc = msg;

    if (msg) {
        task_desc_tab[task_current].msg_ind = msg->next;
        return msg;
    }

    return task_schedule();
}

/***************************************************************************************/
/***************************************************************************************/
/***************************************************************************************/


/// Free memory block delimiter structure (size must be word multiple)
struct rtos_mem_free
{
    struct rtos_mem_free *next;     ///< Pointer to the next block
    size_t size;                    ///< Size of the current block (including delimiter)
};

/// Used memory block delimiter structure (size must be word multiple)
struct rtos_mem_used
{
    size_t size;                    ///< Size of the current block (including delimiter)
};

struct rtos_mem_free *mfree;

//uint32_t mem_cnt = 0;


void mem_init(void* heap_bottom, void* heap_top)
{
    // align address of heap bottom on word boundary
    mfree = (struct rtos_mem_free*)heap_bottom;

    // initialize the first block
    mfree->size = (size_t)heap_top - (size_t)mfree;
    mfree->next = NULL;
}


void *mem_alloc(size_t size)
{
    struct rtos_mem_free *node, *found;
    struct rtos_mem_used *alloc;
    size_t totalsize;

    // initialize the pointers
    found = NULL;

    // compute total block size: requested size PLUS used descriptor size
    totalsize = ((size + 3) & (~3)) + sizeof(struct rtos_mem_used);

    // sanity check: the totalsize should be large enough to hold free block descriptor
    ASSERT(totalsize >= sizeof(struct rtos_mem_free));

    // point to the first free block in the memory
    node = mfree;

    // go through free memory blocks list
    while (node != NULL)
    {
        // check if there is enough room in this free block
        if (node->size >= (totalsize + sizeof(struct rtos_mem_free)))
        {
            // if a match was already found, check if this one is smaller
            if ((found == NULL) || (found->size > node->size))
            {
                found = node;
            }
        }
        // move to next block
        node = node->next;
    }

    // found a free block that matches, subtract the allocation size from the
    // free block size. If equal, the free block will be kept with 0 size... but
    // moving it out of the linked list is too much check for little improvement.
    found->size -= totalsize;

    // compute the pointer to the beginning of the free space
    alloc = (struct rtos_mem_used*) ((uint32_t)found + found->size);

    // sanity check: allocation should always succeed
    ASSERT(found != NULL);

    // save the size of the allocated block (use low bit to indicate mem type)
    alloc->size = totalsize;

    // move to the user memory space
    alloc++;

    //Uart1PutS(" - mem alloc ");
    //Uart1PutU32(++mem_cnt);
    //Uart1PutS("\n");

    return (void*)alloc;
}

void mem_free(void *pointer)
{
    struct rtos_mem_used *freed;
    struct rtos_mem_free *node, *prev_node, *next_node;
    uint32_t size;

    // point to the block descriptor (before user memory so decrement)
    freed = ((struct rtos_mem_used *)pointer) - 1;

    // point to the first node of the free elements linked list
    size = freed->size;
    node = mfree;
    prev_node = NULL;

    // sanity checks
    ASSERT(pointer != NULL);
    ASSERT((uint32_t)pointer > (uint32_t)node);

    while (node != NULL)
    {
        // check if the freed block is right after the current block
        if ((uint32_t)freed == ((uint32_t)node + node->size))
        {
            // append the freed block to the current one
            node->size += size;

            // check if this merge made the link between free blocks
            if ((uint32_t)node->next == ((uint32_t)node + node->size))
            {
                next_node = node->next;
                // add the size of the next node to the current node
                node->size += next_node->size;
                // update the next of the current node
                node->next = next_node->next;
            }
            goto free_end;
        }
        else if ((uint32_t)freed < (uint32_t)node)
        {
            // sanity check: can not happen before first node
            ASSERT(prev_node != NULL);

            // update the next pointer of the previous node
            prev_node->next = (struct rtos_mem_free*)freed;

            // check if the released node is right before the free block
            if (((uint32_t)freed + size) == (uint32_t)node)
            {
                // merge the two nodes
                ((struct rtos_mem_free*)freed)->next = node->next;
                ((struct rtos_mem_free*)freed)->size = node->size + (uint32_t)node - (uint32_t)freed;
            }
            else
            {
                // insert the new node
                ((struct rtos_mem_free*)freed)->next = node;
                ((struct rtos_mem_free*)freed)->size = size;
            }
            goto free_end;
        }

        // move to the next free block node
        prev_node = node;
        node = node->next;
    }
    // if reached here, freed block is after last free block and not contiguous
    prev_node->next = (struct rtos_mem_free*)freed;
    ((struct rtos_mem_free*)freed)->next = NULL;
    ((struct rtos_mem_free*)freed)->size = size;

free_end:

    //Uart1PutS(" - mem free ");
    //Uart1PutU32(--mem_cnt);
    //Uart1PutS("\n");

    return;
}



