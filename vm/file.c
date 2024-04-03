/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "include/threads/vaddr.h"
#include "include/threads/mmu.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static void lazy_load_file_backed_page(struct page *page, void *aux) {
	struct file_info *_aux = (struct file_info *)aux;
	struct file *file = _aux->file;
	off_t ofs = _aux->ofs;
	size_t page_read_bytes = _aux->read_bytes;
	size_t page_zero_bytes = _aux->zero_bytes;
	page->file.aux = _aux;
	// page->file.file = file;
	// page->file.ofs = ofs;

	/* Load this page. */
	file_seek(file, ofs);

	if (file_read (file, page->frame->kva, page_read_bytes) != (int)page_read_bytes) {
		return false;
	}
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	// TODO: 제로 바이트 뺀거 다시 넣기
	return true;
}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset) {
	void *return_addr = addr;
	struct file *_file = file_reopen(file);
	if (_file == NULL) {
		return NULL;
	}
	size_t read_bytes = file_length(file) > length ? length : file_length(file);
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (addr) == 0);
	ASSERT (offset % PGSIZE == 0);

	// TODO : 제로 바이트 추가 및 or 문 추가 & 트러블 슈팅
	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		/* lazy_load_segment에 정보를 전달하기 위해 aux를 설정한다. */
		struct file_info *fi = malloc(sizeof(struct file_info));
		fi->file = _file;
		fi->ofs = offset;
		fi->read_bytes = page_read_bytes;
		fi->zero_bytes = page_zero_bytes;
		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_file_backed_page, fi))
			return NULL;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return return_addr;
}

/* Do the munmap */
void do_munmap(void *addr) {
	ASSERT(pg_ofs(addr) == 0);
	struct thread *t = thread_current();

	struct page *page = spt_find_page(&t->spt, addr);
	if (page == NULL) {
		return;
	}
	struct file_info *_aux = (struct file_info *)page->file.aux;
	struct file *file = _aux->file;
	int size = file_length(file);
	if (size % PGSIZE != 0) {
		size += PGSIZE;
	}
	for (int i = 0; i < size / PGSIZE; i++, addr += PGSIZE) {
		struct page *page = spt_find_page(&t->spt, addr);
		if (page == NULL) {
			return;
		}
		page->file.aux = page->uninit.aux;
		struct file_info *_aux = (struct file_info *)page->file.aux;
		struct file *file = _aux->file;
		off_t ofs = _aux->ofs;
		size_t page_read_bytes = _aux->read_bytes;

		if (pml4_is_dirty(t->pml4, addr)) {
			file_write_at(file, page->frame->kva, page_read_bytes, ofs);
			pml4_set_dirty(t->pml4, addr, false);
		}
		// spt_remove_page(&t->spt, page);
		pml4_clear_page(t->pml4, addr);
	}
}