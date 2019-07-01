/*
 * Copyright (C) 2019 Ricardo Leite. All rights reserved.
 * Licenced under the MIT licence. See COPYING file in the project root for details.
 */

#ifndef __DEFINES_H__
#define __DEFINES_H__

// a page is 4KB
#define LG_PAGE         12
// a huge page is 2MB
#define LG_HUGEPAGE     21

#define PAGE        ((size_t)(1U << LG_PAGE))
#define HUGEPAGE    ((size_t)(1U << LG_HUGEPAGE))

// minimum alignment requirement all allocations must meet
// "address returned by malloc will be suitably aligned to store any kind of variable"
#define MIN_ALIGN sizeof(void*)

// returns smallest address >= addr with alignment align
#define ALIGN_ADDR(addr, align) \
        ( __typeof__ (addr))(((size_t)(addr) + (align - 1)) & ((~(align)) + 1))

#endif // __DEFINES_H__
