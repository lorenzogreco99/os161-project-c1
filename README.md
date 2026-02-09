# OS161 – Project C1  
## Virtual Memory with Demand Paging

### Course: System and Device Programming  
### Group: Luciana Colella, Lorenzo Greco, Lisa Giacobazzi

---

## 1. Introduction

This project focuses on the implementation of a virtual memory subsystem for the OS161 operating system, replacing the original dumbvm memory manager.

The main objective is to support demand paging, page tables, and page replacement, allowing user programs to execute without loading all virtual pages into physical memory at startup.

The implementation includes explicit TLB management, read-only protection for text segments, swapping to secondary storage, and runtime statistics collection.

The project was developed as part of the System and Device Programming course and required working directly on kernel-level components of OS161.


## 2. Paging

The first major enhancement with respect to the original DUMBVM system concerns **paging**.  
In the default OS161 implementation, physical memory management is extremely limited and mainly relies on `ram.c`, which is only suitable for basic bootstrap operations.

In our project, physical memory is instead managed through a coremap structure. After system initialization, the coremap completely replaces the use of `ram.c`, allowing the kernel to track and manage physical frames in a precise and structured way.

Paging represents the foundation for all the other virtual memory features implemented in this project, such as demand paging, page replacement, and swapping.


### 2.1 Coremap Structure

The **coremap** is a data structure containing one entry for each physical memory frame.  
Each entry stores the information needed by the kernel to correctly manage memory allocation and replacement.

To locate free frames, the kernel performs a **linear scan** over the coremap. For this reason, each coremap entry includes a **flag** indicating whether the corresponding frame is free or currently allocated.

While user-space pages are always allocated one at a time, the kernel may allocate multiple contiguous pages using the `kmalloc` function. Since these allocations must later be released using `kfree`, the coremap must also store the size of kernel allocations in order to correctly deallocate all contiguous frames (`cm_allocsize`).

Since this project implements a **per-process page table**, it is essential to know which process owns a given physical frame. This information is required when a frame is swapped out, as the corresponding page table entry must be updated. Instead of explicitly storing the process identifier, each coremap entry contains a **pointer** to the page table entry associated with that frame.

Each physical frame must therefore have a corresponding entry in the coremap. Considering a 32-bit architecture with a page size of 4096 bytes, the lower 12 bits of a physical address represent the offset within a page and are not required to identify a frame. As a result, a physical frame index can be represented using **20 bits**. This representation also helps reduce memory waste in the coremap.

It is also necessary to distinguish between kernel and user frames. This information does not require a dedicated field: if the pointer to the page table entry is NULL, the frame is considered to belong to the kernel.

Finally, a locking mechanism is required to prevent frames from being accessed or modified during critical operations, such as swapping. For this reason, each coremap entry includes a **lock field**.

```c
struct coremap_entry {
    unsigned char   cm_used : 1;
    unsigned long   cm_allocsize : 20;
    unsigned char   cm_lock : 1;
    struct pt_entry *cm_ptentry;   /* page table entry of the page living
                                      in this frame, NULL if kernel page */
};
```

### 2.2 Page Allocation

**Page allocation** is straightforward as long as free physical frames are available.

**Kernel pages** are allocated contiguously using the `kmalloc` function. Since these pages contain critical kernel data structures, they are never swapped out and must always remain resident in physical memory.

**User pages**, on the other hand, are allocated individually. This allocation usually occurs during the handling of a page fault, when a page is accessed for the first time or needs to be brought back from secondary storage. To allocate a user page, the coremap is scanned linearly to find a free frame. When a frame is released, the corresponding coremap entry is marked as free.

If no free frames are available, page replacement becomes necessary and a swap-out operation is triggered.

**Victim selection** is implemented using a simple **round-robin policy**. Starting from a rotating index, the algorithm scans the coremap until a frame belonging to user space is found. Kernel frames are never considered as swap victims.

Once a victim frame has been selected, its contents are written to the swap file. The page table entry of the process owning the page is updated with the swap index, which is why the coremap stores a pointer to the page table entry. After the swap-out operation, the physical frame is cleared and returned as a free frame for the new allocation.

Although this algorithm is simple, several optimizations are possible. For example, read-only pages that are already present in the ELF file do not need to be swapped out. Additionally, avoiding unnecessary swap operations can reduce both execution time and swap space usage. Some of these optimizations have been implemented in our project.


### 2.3 User Page Allocation Flow

The allocation of a user page follows a precise sequence of operations:

1. The virtual memory system receives a **page fault**.
2. The kernel identifies the corresponding entry in the page table.
3. If the page is not resident in physical memory, page allocation is requested by calling `alloc_upage`, passing the page table entry.
4. The allocation function invokes `getppages`, which in turn calls `coremap_getppages`.
5. This function searches for a free physical frame; if none is available, it frees memory by performing a swap-out operation. If no space can be freed, the kernel calls `panic("Out of swap space")`.
6. Finally, `coremap_getppages` updates the coremap by inserting the pointer to the page table entry associated with the allocated frame.




## 3. On-demand page load

To support demand paging, pages must be brought into physical memory only when they are first accessed.  
In OS161, this requires changing the default loading behavior, which normally allocates frames and loads the whole program segments at startup.

In our implementation, demand loading is enabled through two key changes:

1. the executable ELF file is kept open while the process runs, so that pages can be read later when faults occur;
2. the `load_elf` routine is modified to avoid loading entire segments in advance, storing instead only the information needed to retrieve each page from the ELF file when required.

### 3.1 Leaving the ELF file open

In the original OS161 workflow, `runprogram` closes the vnode of the executable immediately after loading.  
With demand paging this is not possible, because the kernel must be able to read portions of the program file *later*, when page faults occur.

For this reason we keep the ELF vnode open and store it inside the process structure. Concretely, in `runprogram` we avoid calling `vfs_close(v)` and instead save the vnode pointer in a new field (e.g. `p_vnode`) of `struct proc`.

This allows `vm_fault` (or helper functions called by it) to fetch missing pages directly from the executable on demand.

```c
int runprogram(char *progname) {
    ...
#if OPT_DEMANDVM
    curproc->p_vnode = v;
#else
    vfs_close(v);
#endif
    ...
}

struct proc {
    ...
    struct vnode *p_vnode;   /* process ELF vnode */
    ...
};
```

### 3.2 Disabling eager segment loading in `load_elf`

In standard OS161, `load_elf` reads the ELF headers and iterates over the program segments to:
- define the address space layout;
- **load the full content** of each segment into memory (through routines such as `load_segment`).

With demand paging, the second step must be removed: at program start we want to define segments and metadata, but we do **not** want to allocate and fill all pages immediately.

To achieve this, we simplify `load_elf` by removing the part that loads entire segments into physical memory.  
At the same time, `as_define_region` is extended so that, besides segment virtual bounds and sizes, it also stores ELF-related metadata such as:
- the offset of the segment within the ELF file (`elf_offset`);
- the size of the segment content present in the ELF file (`elfsize`).

The ELF offset is essential later: when a page fault occurs, we can compute exactly which bytes to read from the executable and from which position.

```c
int as_define_region(struct addrspace *as,
                     vaddr_t first_vaddr,
                     size_t memsize,
                     off_t elf_offset,
                     size_t elfsize);
```

### 3.3 Page loading (`as_load_page` and `load_page`)

After the changes above, page faults become the mechanism through which code/data pages are loaded.

We introduced two helper functions:

- `load_page(v, offset, paddr, size)`  
  performs the low-level operation: it reads `size` bytes from the ELF vnode `v` starting at `offset`, and copies them into the physical frame at `paddr`.

- `as_load_page(as, vnode, faultaddress)`  
  acts as a wrapper that computes the correct `(offset, size, target_addr)` for the page that caused the fault, then calls `load_page`.

This extra wrapper is necessary because when reading from ELF, page boundaries are not always aligned in a trivial way:
- the first virtual address of a segment may not be page-aligned;
- the number of bytes present in the ELF file for a segment (FileSiz) can differ from the amount of memory the segment occupies at runtime (MemSiz), because the remaining part must be zero-filled (e.g. `.bss`).

A typical example is the `sort` program, where the data segment begins at a non page-aligned virtual address.

For these reasons, `as_load_page` handles different cases depending on which page of the segment is being loaded.

```c
void load_page(struct vnode *v, off_t offset, paddr_t page_paddr, size_t size);

int as_load_page(struct addrspace *as, struct vnode *vnode, vaddr_t faultaddress);
```


#### Case A: first page of a segment

If the faulting page corresponds to the first page of the segment, the segment may start “in the middle” of a page.  
In this case:
- `offset` is the segment’s ELF offset;
- `target_addr` is the base physical frame address plus the in-page offset of `seg_first_vaddr`;
- `size` is the amount of bytes to read so that we fill only the portion actually present in the ELF, without exceeding either the end of the page or the end of the segment ELF content.

#### Case B: last page of the segment (still backed by ELF)

If the faulting page corresponds to the last page containing data from the ELF file, the amount of remaining bytes can be less than a full page.

In this case:
- `offset` is computed as: segment base offset + distance between `seg_first_vaddr` and the faulting page base address;
- `target_addr` is the page-aligned physical address of the allocated frame;
- `size` equals the number of bytes remaining in the segment ELF content for that page.

#### Case C: middle page of the segment

For any page strictly inside the segment (neither first nor last ELF-backed page), we can read exactly one full page:
- `size = PAGE_SIZE`
- `offset` computed like in Case B
- `target_addr` is the page-aligned physical address of the allocated frame

After computing these parameters, `as_load_page` invokes `load_page`, which transfers data from the ELF file into the allocated frame.  
Control then returns to `vm_fault`, which can finally update the page table and install the correct TLB entry.


## 4. Address Space

In the original OS161 implementation, the ELF file headers explicitly describe only two segments:  
the `.text` segment (read-only) and the `.data` segment (read/write).  
From this information, it is possible to derive that a process virtual address space is logically composed of three segments: **text**, **data**, and **stack**.

In the default system, these segments are allocated contiguously in virtual memory.  
With the introduction of page tables, this restriction is no longer required, and segments can be managed independently.

The user virtual address space spans from `0x00000000` to `0x80000000`. With a page size of 4096 bytes, this would allow up to `0x80000` virtual pages. A naïve page table implementation would therefore require `0x80000` entries per process, which would be highly inefficient due to the presence of large unused regions.

To avoid wasting memory, a different strategy is adopted:  
**the page table contains entries only for pages that belong to a valid segment** (text, data, or stack).  
As a consequence, computing the page table index from a virtual address becomes slightly more complex, since the simple formula `vaddr / PAGE_SIZE` can no longer be applied directly.

This approach significantly reduces memory usage at the cost of a more elaborate index computation.

### 4.1 Address Space Structure

The address space structure stores information related to the three segments of a process and its page table.

For better modularity and code readability, pointers to separate data structures are used instead of embedding all information directly inside the address space structure.

```c
struct addrspace {
    struct segment   *as_text;
    struct segment   *as_data;
    struct segment   *as_stack;
    struct pt_entry  *as_ptable;
};
```

### 4.2 Segment Structure

Each segment is described by a dedicated structure containing the information required to locate it in the virtual address space and to correctly load its pages from the ELF file.

```c
struct segment {
    vaddr_t seg_first_vaddr;
    vaddr_t seg_last_vaddr;
    size_t  seg_elf_size;
    off_t   seg_elf_offset;
    size_t  seg_npages;
};
```

For each segment, the kernel stores:
- the first and last virtual addresses occupied by the segment;
- the offset of the segment within the ELF file;
- the size of the segment content actually present in the ELF file;
- the number of virtual pages covered by the segment.

This information is necessary because a segment may be only partially backed by the ELF file.  
For example, uninitialized data or stack pages do not correspond to data stored in the executable and must be zero-filled instead of being loaded from disk.

Although it would be possible to explicitly store access permissions (read-only or read/write) inside the segment structure, this was deemed unnecessary. Since the number of segments is fixed and their roles are known in advance, permissions can be inferred directly from the segment type (text, data, or stack).

### 4.3 Page Table Structure

Each entry in the page table describes the state and location of a single virtual page.
```c
#define NOT_LOADED        0
#define IN_MEMORY         1
#define IN_SWAP           2
#define IN_MEMORY_RDONLY  3

struct pt_entry {
    unsigned int frame_index : 20;
    unsigned int swap_index  : SWAP_INDEX_SIZE;
    unsigned char status     : 2;
};
```

The page table is used during TLB misses to determine whether a page is resident in physical memory, stored in the swap file, or has not yet been loaded.

Only 20 bits are required to index a physical frame, while `SWAP_INDEX_SIZE` bits are sufficient to index pages in the swap file. Although these two fields together would fit in 32 bits, an additional 2-bit `status` field is included to explicitly represent the state of the page.

Originally, the page state was inferred by checking whether the frame index or swap index was zero. This approach proved to be unsafe, since both indices are zero-based and valid entries may legitimately contain a zero value.

Alternative solutions were considered, such as using sentinel values or a single index field whose meaning depends on context. However, these approaches were either fragile or difficult to extend. The final design choice was therefore to include an explicit status field, simplifying the logic and improving code robustness with negligible memory overhead.

### 4.4 Computing the Page Table Index from a Virtual Address

To retrieve the correct page table entry for a given virtual address, the kernel must first determine which segment the address belongs to.

This is done by comparing the address against the boundaries of the text, data, and stack segments.  
If the address does not belong to any segment, it represents an illegal memory access and the process must be terminated.

```c
int as_get_segment_type(struct addrspace *as, vaddr_t vaddr) {
    KASSERT(as != NULL);

    if (vaddr >= as->as_text->seg_first_vaddr &&
        vaddr <  as->as_text->seg_last_vaddr) {
        return SEGMENT_TEXT;
    }

    if (vaddr >= as->as_data->seg_first_vaddr &&
        vaddr <  as->as_data->seg_last_vaddr) {
        return SEGMENT_DATA;
    }

    if (vaddr >= as->as_stack->seg_first_vaddr &&
        vaddr <  as->as_stack->seg_last_vaddr) {
        return SEGMENT_STACK;
    }

    return 0;
}
```

Once the segment is identified, the offset of the virtual address within the segment is computed relative to the base page-aligned address of the segment. The page table index is then calculated by summing:
- the **number of pages** in all preceding segments;
- the **page offset** within the current segment.

For example, if the address belongs to the data segment, the number of text pages is added before computing the data segment offset.

```c
static int pt_get_index(struct addrspace *as, vaddr_t vaddr) {
    unsigned int pt_index;

    KASSERT(as != NULL);

    switch (as_get_segment_type(as, vaddr)) {

        case SEGMENT_TEXT:
            pt_index = (vaddr -
                       (as->as_text->seg_first_vaddr & PAGE_FRAME)) / PAGE_SIZE;
            KASSERT(pt_index < as->as_text->seg_npages);
            return pt_index;

        case SEGMENT_DATA:
            pt_index = as->as_text->seg_npages +
                       (vaddr -
                       (as->as_data->seg_first_vaddr & PAGE_FRAME)) / PAGE_SIZE;
            KASSERT(pt_index <
                    as->as_text->seg_npages + as->as_data->seg_npages);
            return pt_index;

        case SEGMENT_STACK:
            pt_index = as->as_text->seg_npages +
                       as->as_data->seg_npages +
                       (vaddr -
                       (as->as_stack->seg_first_vaddr & PAGE_FRAME)) / PAGE_SIZE;
            KASSERT(pt_index <
                    as->as_text->seg_npages +
                    as->as_data->seg_npages +
                    as->as_stack->seg_npages);
            return pt_index;

        default:
            panic("invalid segment type (pt_get_index)");
    }
}
```



## 5. Swap

To support page replacement when physical memory is exhausted, a swap mechanism is implemented using a dedicated file named `SWAPFILE`.

During system startup, a bootstrap routine opens the swap file and keeps it open for the entire lifetime of the kernel. The swap subsystem is implemented as a separate module and is invoked by the **coremap**, **virtual memory**, or **page table** code whenever a page needs to be swapped out to secondary storage or swapped back into memory.

The size of the swap file is fixed at **9 MB**. Since pages are 4096 bytes in size, this corresponds to 0x900 swap pages. Each page in the swap file can therefore be identified by an index that fits within 12 bits. Although not all possible indices are used, this representation allows some flexibility for future extensions.

To make the implementation independent from a specific swap size, the number of bits used for swap indexing is defined through the `SWAP_INDEX_SIZE` constant in `swapfile.h`, which can be adjusted if the swap file size changes.

### 5.1 Swap space management

In order to track which portions of the swap file are currently in use, a **bitmap** is employed. Each bit in the bitmap corresponds to one page in the swap file: a value of 1 indicates that the page is **occupied**, while a value of 0 means that it is **free**.

The bitmap is initialized at startup as follows:

```c
static struct bitmap *swapmap;
swapmap = bitmap_create(SWAPFILE_SIZE / PAGE_SIZE);
```

When a page needs to be swapped out, the kernel searches the bitmap for a free swap page. Once an available index is found, the contents of the selected physical frame are written to the corresponding location in the swap file, and the associated bit in the bitmap is set.

Conversely, during a swap-in operation, the kernel reads the page stored at a given swap index into a newly allocated physical frame and clears the corresponding bit in the bitmap, marking that swap page as free again.

This approach ensures a simple and efficient management of secondary storage while maintaining a clear correspondence between physical frames and swap locations.

### 5.2 Swap optimization for read-only pages

After implementing the basic swap mechanism, further analysis revealed that some swap operations are unnecessary and can be avoided.

In particular, swapping out read-only pages is often redundant. Pages belonging to read-only segments, such as the text segment, are already present in identical form inside the executable ELF file. Writing such pages to the swap file provides no benefit and only increases I/O overhead and swap space usage.

Since demand paging is implemented and the ELF file remains open during execution, these pages can always be reloaded directly from the program binary when needed. As both the ELF file and the swap file reside on secondary storage, reading from one or the other has no inherent performance advantage.

For this reason, an optimization is introduced: when a read-only page is selected as a swap victim, it is simply discarded from physical memory without being written to the swap file. If the page is accessed again, it is reloaded from the ELF file.

This optimization reduces unnecessary disk writes and conserves swap space. It can be enabled or disabled at compile time using the `noswap_rdonly` kernel configuration option.





## 6. VM Fault

The `vm_fault` function represents the core of the virtual memory subsystem implemented in this project.  
Although its interface is similar to the one provided by the original `dumbvm` implementation, its internal logic is significantly more complex and integrates all the mechanisms introduced in the project, such as page tables, demand paging, swapping, and TLB management.

The function is invoked whenever:
- a virtual address translation is not present in the TLB (TLB miss);
- an illegal memory operation is attempted, such as writing to a read-only page.

The prototype of the function is the following:

```c
int vm_fault(int faulttype, vaddr_t faultaddress);
```

The parameters passed to the function are:
- `faulttype`, which identifies the type of fault:
  - `VM_FAULT_READONLY` when a write is attempted on a page whose TLB entry does not allow modifications;
  - `VM_FAULT_READ` or `VM_FAULT_WRITE` when a read or write access is performed on a page not present in the TLB.
- `faultaddress`, the virtual address that caused the fault.

### 6.1 Write on Read-Only Page

When a process attempts to write to a read-only page, the hardware triggers a `VM_FAULT_READONLY` exception.  
In this case, the kernel immediately terminates the offending process, since modifying a read-only segment (such as the text segment) represents an illegal operation.

The fault is handled directly inside `vm_fault` by invoking `sys__exit()` from kernel space.

```c
switch (faulttype) {
    case VM_FAULT_READONLY:
        kprintf("vm: got VM_FAULT_READONLY, process killed\n");
        sys__exit(-1);
        return 0;

    case VM_FAULT_READ:
    case VM_FAULT_WRITE:
        break;

    default:
        return EINVAL;
}
```

To correctly trigger this behavior, TLB entries must be inserted with the proper permissions.  
This is achieved through the `tlb_insert` function, which receives a boolean parameter indicating whether the page should be marked as read-only.

```c
void tlb_insert(vaddr_t vaddr, paddr_t paddr, bool ro);
```

When `ro` is set to true, the dirty bit of the TLB entry is cleared, ensuring that any write attempt on that page will generate a `VM_FAULT_READONLY`.

The TLB insertion routine also implements a **round-robin replacement policy** when the TLB is full.



### 6.2 Read and Write Faults

Read or write faults occur when the requested virtual address is not present in the TLB.  
These faults can correspond to several different situations, depending on the state of the page in the page table.

The first operation performed by `vm_fault` is to page-align the faulting address.  
The aligned address is then used, together with the current address space, to:
- determine the segment to which the address belongs;
- retrieve the corresponding page table entry.

```c
basefaultaddr = faultaddress & PAGE_FRAME;

if (!(seg_type = as_get_segment_type(as, faultaddress))) {
    kprintf("vm: got faultaddr out of range, process killed\n");
    sys__exit(-1);
}


pt_row = pt_get_entry(as, faultaddress);
readonly = (seg_type == SEGMENT_TEXT);
```

The segment type is also used to determine whether the page must be treated as read-only, which is the case for text segment pages.

The subsequent behavior of the fault handler depends on the value stored in the `pt_status` field of the page table entry.

#### Case 1: Page already in memory (`IN_MEMORY`, `IN_MEMORY_RDONLY`)

In this case, the page is resident in physical memory, but its TLB entry has been evicted to make room for other entries.

The kernel simply reconstructs the TLB entry using the physical frame index stored in the page table and resumes execution.

#### Case 2: Not loaded stack page (`NOT_LOADED`, stack segment)

Stack pages are not backed by the ELF file and are initially empty.

When a stack page is accessed for the first time, the kernel allocates a new physical frame, zero-fills it, updates the page table entry, and installs the corresponding TLB entry.

#### Case 3: Not loaded text or data page (`NOT_LOADED`, non-stack segment)

If a page belonging to the text or data segment has not yet been loaded, the kernel first allocates a physical frame.

Text pages must always be loaded from the ELF file.  
For data pages, this is not always the case, as some portions of the segment may not be backed by the executable.

To determine whether the page is stored in the ELF file, the function `as_check_in_elf` is used. If the function returns true, the page is loaded from the ELF file using `as_load_page`. Otherwise, the page is simply zero-filled.

Once the page contents are available in memory, the page table entry is updated and the TLB entry is installed.

#### Case 4: Page in swap (`IN_SWAP`)

When the page table entry indicates that the page is stored in the swap file, the kernel must bring it back into physical memory.

A new physical frame is allocated, and the page contents are read from the swap file using the swap index stored in the page table entry.  
After the swap-in operation, the page table is updated to reflect the new physical location, and the TLB entry is installed.

In all cases, if the page belongs to the text segment, the TLB entry is inserted as read-only.

```c
switch (pt_row->pt_status) {

    case NOT_LOADED:
        page_paddr = alloc_upage(pt_row);
        pt_set_entry(pt_row, page_paddr, 0,
                     readonly ? IN_MEMORY_RDONLY : IN_MEMORY);

        if (seg_type != SEGMENT_STACK &&
            as_check_in_elf(as, faultaddress)) {
            as_load_page(as, curproc->p_vnode, faultaddress);
        }
        break;

    case IN_MEMORY_RDONLY:
    case IN_MEMORY:
        break;

    case IN_SWAP:
        page_paddr = alloc_upage(pt_row);
        swap_in(page_paddr, pt_row->pt_swap_index);
        pt_set_entry(pt_row, page_paddr, 0,
                     readonly ? IN_MEMORY_RDONLY : IN_MEMORY);
        break;

    default:
        panic("Cannot resolve fault");
}

KASSERT(seg_type != 0);

/* Insert TLB entry */
tlb_insert(basefaultaddr,
           pt_row->pt_frame_index * PAGE_SIZE,
           readonly);

return 0;
```

## 7. Statistics

As required by the project specifications, the virtual memory subsystem collects runtime statistics related to its behavior and performance.

A fixed set of ten events is monitored. Two parallel arrays are used:
- an array of strings containing the names of the events, used when printing the statistics;
- an array of counters storing the number of occurrences for each event.

Whenever one of the monitored events occurs, the function `vmstats_hit` is invoked to increment the corresponding counter. Since the kernel may run on multiple CPUs, a spinlock is used to protect these counters and prevent race conditions during updates.

A dedicated function, `vmstats_print`, is responsible for printing the collected statistics. This function is invoked during system shutdown and is called from the `shutdown` routine in `vm.c`, ensuring that statistics are displayed at the end of execution.

### 7.1 Statistics Triggers

Each statistic is incremented at a precise point in the virtual memory code, as described below:

- **TLB Fault**  
  Incremented in `vm_fault` whenever a TLB miss occurs.

- **TLB Fault with Free**  
  Incremented in `tlb_insert` when a new TLB entry is added and there is at least one free TLB slot available.

- **TLB Fault with Replace**  
  Incremented in `tlb_insert` when a TLB entry must be replaced because the TLB is full.

- **TLB Invalidation**  
  Incremented in `tlb_invalidate`, which is invoked after `as_activate` during a context switch.

- **TLB Reload**  
  Incremented in `vm_fault` when the faulting page is already resident in physical memory (page table state `IN_MEMORY`).

- **Page Fault (Zeroed)**  
  Incremented when a page fault refers to a page that has never been loaded (`NOT_LOADED`) and either belongs to the stack segment or is not backed by the ELF file. In this case, a new page is allocated and zero-filled.

- **Page Fault (Disk)**  
  Incremented when a page fault requires reading data from secondary storage, either because the page is stored in the ELF file or because it has been previously swapped out (`IN_SWAP`).

- **Page Fault from ELF**  
  Incremented when a page fault causes a page to be loaded from the executable ELF file via `as_load_page`.

- **Page Fault from Swapfile**  
  Incremented when a page fault requires loading a page from the swap file using `swap_in`.

- **Swapfile Writes**  
  Incremented whenever a page is written to the swap file during a swap-out operation.


## 8. Configuration

The kernel has been designed in a modular way so that the features implemented in this project can be enabled or disabled through configuration options.

The available options are the following:

- **syscalls** and **waitpid**  
  These options enable basic system call support and process synchronization mechanisms. They are required for the correct operation of the kernel and must always be enabled.

- **DEMANDVM**  
  Enables the new virtual memory subsystem implemented in this project. This option activates demand paging, per-process page tables, and software-managed TLB handling, effectively replacing the original `dumbvm` system.

- **swap**  
  Enables swap support. When this option is active, pages can be written to and read from the swap file when physical memory is exhausted. This feature is optional but significantly improves the system’s ability to handle memory pressure.

- **stats**  
  Enables the collection and printing of virtual memory statistics described in Section 7. When disabled, all statistic-related code is excluded.

- **noswap_rdonly**  
  Enables the optimization described in Section 4.1. When active, read-only pages are not written to the swap file, as they can be reloaded directly from the ELF executable if needed.


## 9. Tests

The implemented virtual memory system was tested using a combination of standard OS161 tests, user-level programs, and custom stress tests.

The standard test suite includes:

- `at`
- `at2`
- `bt`
- `km1`
- `km2`
- `km3 1000`
- `testbin/palin`
- `testbin/huge`
- `testbin/sort`
- `testbin/ctest`
- `testbin/matmult`

In addition to these, several custom test programs were developed:

- `testbin/nosywrite`, used to verify the correct handling of `VM_FAULT_READONLY`
- `testbin/hugematmult1`, a memory-intensive test
- `testbin/hugematmult2`, designed to trigger an out-of-swap-space condition

To automate testing, a Python script named `execute_tests.py` was created.  
The script executes all user-level tests, collects execution times and virtual memory statistics, and repeats the tests under different RAM size configurations. It also performs a long stability test by executing the full test suite multiple times.

All results are automatically stored in a Markdown file (`testresults.md`), and a summary is reported below.

### 9.1 User Programs

Tests were executed under different memory configurations to evaluate system behavior under varying memory pressure.

**RAM: 512 KB**

| Metric | palin | huge | sort | matmult | hugematmult1 | hugematmult2 | ctest |
|------|------|------|------|--------|--------------|--------------|-------|
| Execution time | 15.541 | 39.080 | 20.864 | 7.4875 | 56.832 | – | 1464.0 |
| TLB Faults | 13986 | 7458 | 6720 | 4341 | 64464 | – | 248545 |
| TLB Faults with Free | 13986 | 7439 | 6578 | 4319 | 64446 | – | 248530 |
| TLB Faults with Replace | 0 | 19 | 142 | 22 | 18 | – | 15 |
| TLB Invalidations | 7824 | 6697 | 2979 | 1218 | 8771 | – | 247943 |
| TLB Reloads | 13981 | 3879 | 5055 | 3533 | 58866 | – | 123624 |
| Page Faults (Zeroed) | 1 | 512 | 289 | 380 | 2350 | – | 257 |
| Page Faults (Disk) | 4 | 3067 | 1376 | 428 | 3248 | – | 124664 |
| Page Faults from ELF | 4 | 58 | 25 | 13 | 78 | – | 1605 |
| Page Faults from Swapfile | 0 | 3009 | 1351 | 415 | 3170 | – | 123059 |
| Swapfile Writes | 0 | 3451 | 1567 | 721 | 5450 | – | 123242 |

**RAM: 4 MB**

| Metric | palin | huge | sort | matmult | hugematmult1 | hugematmult2 | ctest |
|------|------|------|------|--------|--------------|--------------|-------|
| Execution time | 15.487 | 0.9164 | 3.4657 | 0.6779 | 42.957 | 56.902 | 7.3881 |
| TLB Faults | 13897 | 4002 | 2008 | 947 | 38331 | 60192 | 125333 |
| TLB Faults with Free | 13897 | 778 | 122 | 191 | 37010 | 58785 | 151 |
| TLB Faults with Replace | 0 | 3224 | 1886 | 756 | 1321 | 1407 | 125182 |
| TLB Invalidations | 7797 | 182 | 40 | 72 | 6213 | 8154 | 40 |
| TLB Reloads | 13892 | 3487 | 1715 | 564 | 33600 | 54176 | 125073 |
| Page Faults (Zeroed) | 1 | 512 | 289 | 380 | 2350 | 2977 | 257 |
| Page Faults (Disk) | 4 | 3 | 4 | 3 | 2381 | 3039 | 3 |
| Page Faults from ELF | 4 | 3 | 4 | 3 | 7 | 9 | 3 |
| Page Faults from Swapfile | 0 | 0 | 0 | 0 | 2374 | 3030 | 0 |
| Swapfile Writes | 0 | 0 | 0 | 0 | 3759 | 5043 | 0 |


### 9.2 Kernel Tests

All standard kernel tests completed successfully.

| Test name | Passed |
|----------|--------|
| at | True |
| at2 | True |
| bt | True |
| km1 | True |
| km2 | True |
| km3 1000 | True |


### 9.3 Stress Test

A long stress test was executed to evaluate system stability under heavy memory pressure.

| TLB Faults | TLB Faults with Free | TLB Faults with Replace | TLB Invalidations | TLB Reloads | Page Faults (Zeroed) | Page Faults (Disk) | Page Faults from ELF | Page Faults from Swapfile | Swapfile Writes |
|-----------|---------------------|------------------------|-------------------|-------------|---------------------|-------------------|----------------------|---------------------------|-----------------|
| 3455961 | 3453839 | 2122 | 2753799 | 2090311 | 37890 | 1327760 | 17800 | 1309960 | 1344230 |


## 10. Work Division

Given the complexity of the project and the strong interdependence between its components, the work was organized in multiple phases.

An initial study phase was carried out in parallel, during which different design choices and implementation strategies were discussed collectively.

During development, tasks were divided among team members to implement individual components independently:
- TLB management and an initial version of swap functionality
- Coremap and physical memory management
- Page table structures and address space handling

Once the individual components were implemented, the team focused on integration, particularly in the `vm.c` module. This phase required close collaboration, as changes in one subsystem often affected others.

The final phase was dedicated to debugging, optimization, and performance tuning. One particularly challenging issue involved incorrect variable handling during swap-out operations, which required extensive debugging. Further optimizations focused on victim selection, memory usage reduction, and simplification of coremap and swap logic.


## 11. Possible Improvements

Although the implemented system meets all project requirements, several improvements could be explored.

The swap subsystem currently swaps out pages even when they have not been modified. Avoiding swap-out in these cases could reduce disk writes, since such pages could be reloaded directly from the ELF file.

Both coremap and TLB victim selection rely on simple round-robin policies. More advanced algorithms could improve performance, especially under heavy memory pressure.

Finally, searching for free frames in the coremap has linear complexity. Introducing auxiliary data structures (e.g., a free-frame list) could speed up allocation, at the cost of increased maintenance complexity.


## 12. Conclusion

Implementing this project proved to be a challenging and instructive experience.  
Working on OS161 required dealing with low-level system details and carefully coordinating multiple interacting components.

The project significantly deepened our understanding of virtual memory management and highlighted the gap between theoretical concepts and their practical implementation in a real operating system.

Even common tasks such as debugging became non-trivial, often resembling a search for subtle errors in a complex system. Despite the difficulties, solving these issues was highly rewarding and contributed substantially to the development of our skills.
