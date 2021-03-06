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
    { {0}, {NULL,          NULL,                NULL},  1, 0, NULL, NULL, NULL },
    { {0}, {task_stack[1], task_ending_handler, task1}, 0, 0, NULL, NULL, NULL },
    { {0}, {task_stack[2], task_ending_handler, task2}, 0, 0, NULL, NULL, NULL },
    { {0}, {task_stack[3], task_ending_handler, task3}, 0, 0, NULL, NULL, NULL },
};

struct task_desc *task_current = task_desc_tab;


/***************************************************************************************/

// defined in assembly file
struct task_msg *context_start(struct task_msg *req, struct task_reg_init *ctx_init, uint32_t *ctx_save);
//struct task_msg *context_start2(struct task_msg *req, struct task_reg_init *ctx_init);
struct task_msg *context_switch(struct task_msg *req, uint32_t *ctx_restore, uint32_t *ctx_save);
struct task_msg *context_switch2(struct task_msg *req, uint32_t *ctx_restore);


// Allocate a message and fill it with given parameters.
// Just an handy function for testing purpose

struct task_msg *task_malloc(uint32_t id, uint32_t param)
{
    struct task_msg *msg = (struct task_msg*) mem_alloc(sizeof(struct task_msg));
    msg->next = NULL;
    msg->calling = task_current;
    msg->id = id;
    msg->param = param;

    return msg;
}




static struct task_msg *task_start(struct task_msg *req, struct task_desc* called)
{
    struct task_desc* calling = task_current;
    called->started = 1;
    task_current = called;

    return context_start(req,
                         &called->reg_init,
                         calling->reg_save);
}


// Start a new task asynchronously. The context will be switched to the
// new task which must not be already started. Only task 0 can use
// this function.
// The called task will receive a NULL request message.

void task_asynch(int task_id)
{
    ASSERT (1 <= task_id && task_id <= TASK_CNT);
    ASSERT(task_current == task_desc_tab); // Only 0 can start tasks asynchronously

    struct task_desc *called = task_desc_tab + task_id;
    ASSERT(!called->started);
    called->calling = NULL;
    task_start(NULL, called);
}

// Send a request to a new task. The context will be switched to the
// new task, or the request will be enqueued if the task is already
// started. Task 0 cannot use this function.
// The called task will have to free the request
struct task_msg *task_send_req(struct task_msg *req, int task_id) {

    ASSERT(!task_current->blocked);
    ASSERT(1 <= task_id && task_id <= TASK_CNT);
    ASSERT(task_current != task_desc_tab);  // Cannot be task 0
    ASSERT(req);

    struct task_desc *called = task_desc_tab + task_id;

    // block current task, it will only resume when target task returns
    task_current->blocked = 1;

    if (called->started || called->req_queue){
        // find end of request queue and add msg
        struct task_msg *msg_ptr = (struct task_msg*) &called->req_queue;
        while (msg_ptr->next) msg_ptr = msg_ptr->next;
        msg_ptr->next = req;
        req->next = NULL;

        // run any other task
        return task_schedule();

    } else {
        called->calling = task_current;
        return task_start(req, called);

    }

}

// Send an asynchrounous message indication to a task.
// The message is just added to the list and the function return.
// (note: it would be possible to switch to the target task)
// If the task is not started, the indication is discarded.
// The called task will have to free the indication.

void task_send_ind(struct task_msg *ind, int task_id) {

    ASSERT (1 <= task_id && task_id <= TASK_CNT);
    ASSERT(ind); // should be possible to wake up task without ind

    struct task_desc *called = task_desc_tab + task_id;

    if (called->started) {
        // find end of indication queue and add msg
        struct task_msg *msg_ptr = (struct task_msg*) &called->ind_queue;
        while (msg_ptr->next) msg_ptr = msg_ptr->next;
        msg_ptr->next = ind;
        ind->next = NULL;

    } else {
        // discard message
        mem_free(ind);
    }
}



// Switch to another task. The task list is scanned for a task
// that match any of these conditions:
// - task id is 0 (special task which cannot be paused or blocked)
// - task is not blocked, is started, has a pending indication
// - task is not blocked, not stated, has a pending request.
// Context is then switched to the selected task.

struct task_msg *task_schedule(void)
{
    struct task_desc *task_new = task_current;
    struct task_msg *msg = NULL;

    for (;;) {
        // scan next task in the list
        task_new++;

        if (task_new == task_desc_tab + TASK_CNT + 1) {
            // end of list. If task 0 is the calling function, just
            // return, otherwise select it for the context switch
            if (task_current == task_desc_tab)
                return NULL;

            task_new = task_desc_tab;
            break;
        }

        if (!task_new->blocked) {

            if (task_new->started) {
                if (task_new->ind_queue) {
                    // task started and indication pending:
                    // remove indication from the list...
                    msg = task_new->ind_queue;
                    task_new->ind_queue = msg->next;

                    // task is selected for scheduling
                    break;
                }

            } else if (task_new->req_queue) {
                // task not started and request pending:
                msg = task_new->req_queue;
                task_new->req_queue = msg->next;

                task_new->calling = msg->calling;

                return task_start(msg, task_new);
            }
        }
    }

    // prepare contexts and switch to the selected task
    uint32_t * ctx_save = task_current->reg_save;

    task_current = task_new;

    return context_switch(msg, task_new->reg_save, ctx_save);
}


// This function is never called explicitly, but only when a task
// returns.
// If the call was synchronous, it switches back the calling task,
// otherwise any pending task is scheduled.

void task_ending_handler(void * rsp)
{
    // modify task state
    task_current->started = 0;

    struct task_desc *calling = task_current->calling;

    if (calling) {
            ASSERT(calling != task_desc_tab); // cannot be task 0
            ASSERT(calling->blocked && calling->started);
            calling->blocked = 0;

            task_current = calling;
            context_switch2(rsp, calling->reg_save);

    } else {
        task_schedule();
    }

    // we should never arrive here!
    ASSERT(0);
}


// Wait for an indication. If indication queue is not empty, returns
// directly the next one, otherwise schedule any other task until
// a new indication is received.

void *task_wait(void)
{
    struct task_msg *msg = task_current->ind_queue;

    if (msg) {
        // remove new indication from the list and return it to the task
        task_current->ind_queue = msg->next;
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
    struct rtos_mem_free    *next;  ///< Pointer to the next block
    uint32_t                magic;
    size_t                  size;   ///< Size of the current block (including delimiter)
};

/// Used memory block delimiter structure (size must be word multiple)
struct rtos_mem_used
{
    uint32_t    magic;
    size_t      size;               ///< Size of the current block (including delimiter)
};

#define MAGIC_FREE  0x46524545
#define MAGIC_USED  0x55534544

struct rtos_mem_free *mfree;


void mem_init(void* heap_bottom, void* heap_top)
{
    // align address of heap bottom on word boundary
    mfree = (struct rtos_mem_free*)heap_bottom;

    // initialize the first block
    mfree->size = (size_t)heap_top - (size_t)mfree;
    mfree->magic = MAGIC_FREE;
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
        ASSERT(node->magic == MAGIC_FREE);
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
    alloc->magic = MAGIC_USED;

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

    ASSERT(freed->magic == MAGIC_USED);

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
            ((struct rtos_mem_free*)freed)->magic = MAGIC_FREE;

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
    ((struct rtos_mem_free*)freed)->magic = MAGIC_FREE;

free_end:

    //Uart1PutS(" - mem free ");
    //Uart1PutU32(--mem_cnt);
    //Uart1PutS("\n");

    return;
}



