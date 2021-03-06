/**
 * @file
 * @brief Heap implementation based on boundary markers algorithm.
 * @details
 *    Segment structure:
 *    |struct mm_segment| *** space for bm ***|
 *
 *    TODO:
 *    Should be improved by usage of page_alloc when size is divisible by PAGE_SIZE()
 *    Also SLAB allocator can be used when size is 16, 32, 64, 128...
 *
 * @date 04.03.2014
 * @author Alexander Kalmuk
 */

#include <util/err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <mem/heap_bm.h>
#include <mem/page.h>

#include <util/dlist.h>
#include <util/array.h>

#include <kernel/printk.h>
#include <kernel/panic.h>

/* TODO make it per task field */
//static DLIST_DEFINE(task_mem_segments);

//#define DEBUG

extern struct page_allocator *__heap_pgallocator;
extern struct page_allocator *__heap_pgallocator2 __attribute__((weak));
static struct page_allocator ** const mm_page_allocs[] = {
	&__heap_pgallocator,
	&__heap_pgallocator2,
};

static void *mm_segment_alloc(int page_cnt) {
	void *ret;
	int i;
	for (i = 0; i < ARRAY_SIZE(mm_page_allocs); i++) {
		if (mm_page_allocs[i]) {
			ret = page_alloc(*mm_page_allocs[i], page_cnt);
			if (ret) {
				break;
			}
		}
	}
	return ret;
}

static void mm_segment_free(void *segment, int page_cnt) {
	int i;
	for (i = 0; i < ARRAY_SIZE(mm_page_allocs); i++) {
		if (mm_page_allocs[i] && page_belong(*mm_page_allocs[i], segment)) {
			page_free(*mm_page_allocs[i], segment, page_cnt);
			break;
		}
	}
}

struct mm_segment {
	struct dlist_head link;
	size_t size;
};

static inline int pointer_inside_segment(void *segment, size_t size, void *pointer) {
	return (pointer > segment && pointer < (segment + size));
}

static inline void *mm_to_segment(struct mm_segment *mm) {
	assert(mm);
	return ((char *) mm + sizeof *mm);
}

static void *pointer_to_segment(void *ptr, struct dlist_head *mspace) {
	struct mm_segment *mm;
	void *segment;

	assert(ptr);
	assert(mspace);

	dlist_foreach_entry(mm, mspace, link) {
		segment = mm_to_segment(mm);
		if (pointer_inside_segment(segment, mm->size, ptr)) {
			return segment;
		}
	}

	return NULL;
}

static void *mspace_do_alloc(size_t boundary, size_t size, struct dlist_head *mspace) {
	struct mm_segment *mm;
	dlist_foreach_entry(mm, mspace, link) {
		void *block = bm_memalign(mm_to_segment(mm), boundary, size);
		if (block != NULL) {
			return block;
		}
	}

	return NULL;
}

void *mspace_memalign(size_t boundary, size_t size, struct dlist_head *mspace) {
	/* No corresponding heap was found */
	struct mm_segment *mm;
	size_t segment_pages_cnt;
	void *block;

	if (size == 0)
		return NULL;

	assert(mspace);

	block = mspace_do_alloc(boundary, size, mspace);
	if (block) {
		return block;
	}

	/* XXX allocate more approproate count of pages without redundancy */
	/* Note, integer overflow may occur */
	segment_pages_cnt = size / PAGE_SIZE() + boundary / PAGE_SIZE();
	segment_pages_cnt += (size % PAGE_SIZE() + boundary % PAGE_SIZE() + 2 * PAGE_SIZE()) / PAGE_SIZE();

	mm = mm_segment_alloc(segment_pages_cnt);
	if (mm == NULL)
		return NULL;

	mm->size = segment_pages_cnt * PAGE_SIZE();
	dlist_head_init(&mm->link);
	dlist_add_next(&mm->link, mspace);

	bm_init(mm_to_segment(mm), mm->size - sizeof(struct mm_segment));

	block = mspace_do_alloc(boundary, size, mspace);
	if (!block) {
		panic("new memory block is not sufficient to allocate requested size");
	}

	return block;
}

void *mspace_malloc(size_t size, struct dlist_head *mspace) {
	assert(mspace);
	return mspace_memalign(8, size, mspace);
}

int mspace_free(void *ptr, struct dlist_head *mspace) {
	void *segment;

	assert(ptr);
	assert(mspace);

	segment = pointer_to_segment(ptr, mspace);

	if (segment != NULL) {
		bm_free(segment, ptr);
	} else {
		/* No segment containing pointer @c ptr was found. */
#ifdef DEBUG
		printk("***** free(): incorrect address space\n");
#endif
		return -1;
	}

	return 0;
}

void *mspace_realloc(void *ptr, size_t size, struct dlist_head *mspace) {
	void *ret;

	assert(mspace);
	assert(size != 0 || ptr == NULL);

	ret = mspace_memalign(8, size, mspace);

	if (ret == NULL) {
		return NULL; /* error: errno set in malloc */
	}

	if (ptr == NULL) {
		return ret;
	}

	/* The content of new region will be unchanged in the range from the start of the region up to
	 * the minimum of the old and new sizes. So simply copy size bytes (may be with redundant bytes) */
	memcpy(ret, ptr, size);
	if (0 > mspace_free(ptr, mspace)) {
		mspace_free(ret, mspace);
		return err_ptr(EINVAL);
	}

	return ret;
}

void *mspace_calloc(size_t nmemb, size_t size, struct dlist_head *mspace) {
	void *ret;
	size_t total_size;

	total_size = nmemb * size;

	assert(mspace);
	assert(total_size > 0);

	ret = mspace_malloc(total_size, mspace);
	if (ret == NULL) {
		return NULL; /* error: errno set in malloc */
	}

	memset(ret, 0, total_size);
	return ret;
}

int mspace_init(struct dlist_head *mspace) {
	dlist_init(mspace);
	return 0;
}

int mspace_fini(struct dlist_head *mspace) {
	struct mm_segment *mm;

	dlist_foreach_entry(mm, mspace, link) {
		mm_segment_free(mm, mm->size / PAGE_SIZE());
	}

	return 0;
}

size_t mspace_deep_copy_size(struct dlist_head *mspace) {
	struct mm_segment *mm;
	size_t ret;

	ret = 0;
	dlist_foreach_entry(mm, mspace, link) {
		ret += mm->size;
	}
	return ret;
}


void mspace_deep_store(struct dlist_head *mspace, struct dlist_head *store_space, void *buf) {
	struct mm_segment *mm;
	void *p;

	dlist_init(store_space);

	/* if mspace is empty list manipulation is illegal */
	if (dlist_empty(mspace)) {
		return;
	}

	dlist_del(mspace);
	dlist_add_prev(store_space, mspace->next);

	p = buf;
	dlist_foreach_entry(mm, store_space, link) {
		memcpy(p, mm, mm->size);
		p += mm->size;
	}

	dlist_del(store_space);
	dlist_add_prev(mspace, mspace->next);
}

void mspace_deep_restore(struct dlist_head *mspace, struct dlist_head *store_space, void *buf) {
	struct dlist_head *raw_mm;
	void *p;

	dlist_init(mspace);

	p = buf;
	raw_mm = store_space->next;

	/* can't use foreach, since it stores next pointer in accumulator */
	while (raw_mm != store_space) {
		struct mm_segment *buf_mm, *mm;

		buf_mm = p;

		mm = member_cast_out(raw_mm, struct mm_segment, link);
		memcpy(mm, buf_mm, buf_mm->size);

		p += buf_mm->size;
		raw_mm = raw_mm->next;
	}

	if (!dlist_empty(store_space)) {
		dlist_del(store_space);
		dlist_add_prev(mspace, store_space->next);
	}
}
