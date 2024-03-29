/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* vm_init - 각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화한다.
 */
void vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* spt_find_page - 주어진 SPT에서 가상 주소에 해당하는 페이지를 찾아 반환한다.
 * 실패 시 NULL을 반환한다.
 */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {

	struct page p;
	// p.va = pg_round_down (va);
	p.va = va;

	struct hash_elem *e = hash_find(&spt->pages, &p.h_elem);
	if (e == NULL)
		return NULL;

	return hash_entry(e, struct page, h_elem);
}

/* spt_insert_page - 주어진 SPT에 페이지를 삽입한다.
 * 주어진 SPT에 페이지가 이미 존재하는 경우 실패한다.
 */
bool spt_insert_page(struct supplemental_page_table *spt, struct page *page) {
	struct hash_elem *e = hash_insert(&spt->pages, &page->h_elem);

	return e ? false : true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* vm_get_frame - palloc()을 호출하고 프레임을 가져온다.
 * 사용 가능한 페이지가 없는 경우 페이지를 퇴거하고 반환한다.
 * 이 함수는 항상 유효한 주소를 반환한다.
 * 즉, 사용자 풀 메모리가 가득 찬 경우 
 * 이 함수는 프레임을 퇴거하여 사용 가능한 메모리 공간을 확보한다.
 */
static struct frame *vm_get_frame(void) {
	struct frame *frame = malloc(sizeof(struct frame));
	frame->kva = palloc_get_page(PAL_USER);
	if (frame->kva == NULL) {
		PANIC("TODO: Page eviction is not implemented yet.");
	}
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	if (frame->kva == NULL) {
		free(frame);
		PANIC("TODO: Page eviction is not implemented.");
	} 
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* vm_claim_page - 가상 주소에 할당된 페이지를 요구한다.
 * 먼저 페이지를 가져온 다음, 해당 페이지로 vm_do_claim_page를 호출한다.
 */
bool vm_claim_page(void *va) {
	struct page *page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL) {
		return false;
	}
	return vm_do_claim_page(page);
}

/* vm_do_claim_page - 프레임을 요구하고 페이지와 프레임을 연결한다.
 * MMU를 설정하는데, 페이지 테이블에 페이지의 VA와 프레임의 PA 간의 매핑을 추가한다.
 * 성공 여부를 반환한다.
 */
static bool vm_do_claim_page(struct page *page) {
	struct frame *frame = vm_get_frame();
	struct thread *t = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	if (!pml4_set_page(t->pml4, page->va, frame->kva, page->writable)) {
		return false;
	}
	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->pages, spt_hash, spt_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/* spt_hash - Returns a hash value for page p.
 */
uint64_t spt_hash(const struct hash_elem *e, void *aux UNUSED) {
	struct page *page = hash_entry(e, struct page, h_elem);
	return hash_bytes(&page->va, sizeof(page->va));
}

/* spt_less - Returns true if page a precedes page b.
 */
bool spt_less(const struct hash_elem *a_, const struct hash_elem *b_,
		void *aux UNUSED) {
	const struct page *a = hash_entry(a_, struct page, h_elem);
	const struct page *b = hash_entry(b_, struct page, h_elem);

	return a->va < b->va;
}