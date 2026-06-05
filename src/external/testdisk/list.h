/*

    File: list.h - Adapted for disk-recover

    Copyright (C) 2006-2008 Christophe GRENIER <grenier@cgsecurity.org>

    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */
#ifndef _LIST_H
#define _LIST_H

#include "types.h"

/*
 * Simple doubly linked list implementation.
 * Copied from Linux Kernel 2.6.12.3
 */

struct td_list_head {
    struct td_list_head *next, *prev;
};

#define TD_LIST_HEAD_INIT(name) { &(name), &(name) }

#define TD_LIST_HEAD(name) \
    struct td_list_head name = TD_LIST_HEAD_INIT(name)

#define TD_INIT_LIST_HEAD(ptr) do { \
    (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

static inline void __td_list_add(struct td_list_head *newe,
                                 struct td_list_head *prev,
                                 struct td_list_head *next)
{
    newe->next = next;
    newe->prev = prev;
    prev->next = newe;
    next->prev = newe;
}

static inline void td_list_add(struct td_list_head *newe, struct td_list_head *head)
{
    __td_list_add(newe, head, head->next);
}

static inline void td_list_add_tail(struct td_list_head *newe, struct td_list_head *head)
{
    __td_list_add(newe, head->prev, head);
}

static inline void __td_list_del(struct td_list_head *prev, struct td_list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void td_list_del(struct td_list_head *entry)
{
    __td_list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

static inline int td_list_empty(const struct td_list_head *head)
{
    return head->next == head;
}

#define td_list_entry(ptr, type, member) \
    ((type *)((char *)(ptr)-(size_t)(&((type *)0)->member)))

#define td_list_entry_const(ptr, type, member) \
    ((type *)((const char *)(ptr)-(size_t)(&((type *)0)->member)))

#define td_list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define td_list_for_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

#define td_list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); \
         pos = n, n = pos->next)

#define td_list_for_each_prev_safe(pos, n, head) \
    for (pos = (head)->prev, n = pos->prev; pos != (head); \
         pos = n, n = pos->prev)

#define td_list_for_each_entry(pos, head, member) \
    for (pos = td_list_entry((head)->next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = td_list_entry(pos->member.next, typeof(*pos), member))

#define td_list_for_each_entry_reverse(pos, head, member) \
    for (pos = td_list_entry((head)->prev, typeof(*pos), member); \
         &pos->member != (head); \
         pos = td_list_entry(pos->member.prev, typeof(*pos), member))

#define td_list_for_each_entry_safe(pos, n, head, member) \
    for (pos = td_list_entry((head)->next, typeof(*pos), member), \
         n = td_list_entry(pos->member.next, typeof(*pos), member); \
         &pos->member != (head); \
         pos = n, n = td_list_entry(n->member.next, typeof(*n), member))

#define td_list_first_entry(ptr, type, member) \
    td_list_entry((ptr)->next, type, member)

#define td_list_last_entry(ptr, type, member) \
    td_list_entry((ptr)->prev, type, member)

#define td_list_next_entry(pos, member) \
    td_list_entry((pos)->member.next, typeof(*(pos)), member)

#define td_list_prev_entry(pos, member) \
    td_list_entry((pos)->member.prev, typeof(*(pos)), member)

/* Allocation list structure */
typedef struct alloc_list_s alloc_list_t;
struct alloc_list_s
{
    struct td_list_head list;
    uint64_t start;
    uint64_t end;
    unsigned int data;
};

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* closing brace for extern "C" */
#endif

#endif /* _LIST_H */