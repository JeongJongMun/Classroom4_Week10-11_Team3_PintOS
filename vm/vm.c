/* vm.c: Generic interface for virtual memory objects. */

#include <string.h>
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

uint64_t spt_hash(const struct hash_elem *e, void *aux UNUSED);
bool spt_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

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

/* vm_alloc_page_with_initializer - 초기화기로 보류 중인 페이지 객체를 만든다.
 * 페이지를 만들려면 직접 만들지 말고 이 함수나 `vm_alloc_page`를 통해 만들어라.
 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* upage가 이미 사용(occupied) 중인지 여부 확인 */
	if (spt_find_page (spt, upage) == NULL) {
		/* 페이지를 생성하고 VM 타입에 따라 초기화기를 가져온다.
		 * 그런 다음 uninit_new를 호출하여 "uninit" 페이지 구조체를 만든다.
		 * uninit_new를 호출한 후에 필드를 수정해야 한다.
		 */
		struct page *page = malloc(sizeof(struct page));
		void *initializer = NULL;
		switch (VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				break;
		}
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;

		/* spt에 페이지 삽입 */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* spt_find_page - 주어진 SPT에서 가상 주소에 해당하는 페이지를 찾아 반환한다.
 * 실패 시 NULL을 반환한다.
 */
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
	struct page p;
	p.va = pg_round_down(va);
	
	struct hash_elem *e = hash_find(&spt->pages, &p.h_elem);
	if (e == NULL){
		return NULL;
	}
	
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
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	if (frame->kva == NULL) {
		free(frame);
		PANIC("TODO: Page eviction is not implemented.");
	} 
	return frame;
}

/* vm_stack_growth - addr이 더 이상 오류 주소가 되지 않도록 스택을 확장한다.
 * 할당을 처리할 때 addr을 PGSIZE로 반올림한다.
 */
static void vm_stack_growth(void) {
	struct thread *t = thread_current();
	void *new_stack_bottom = (void *)((uint8_t *)t->stack_bottom - PGSIZE);

	vm_alloc_page(VM_ANON, new_stack_bottom, true);
	t->stack_bottom = new_stack_bottom;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* vm_try_handle_fault - 성공 시에 true를 반환한다. 
 * f: 시스템 콜 또는 페이지 폴트가 발생했을 때, 그 순간의 레지스터 값을 담고 있는 구조체
 * addr: 페이지 폴트가 발생한 가상 주소
 * user: 사용자 영역이면 true, 커널 영역이면 false
 * write: 쓰기 작업이면 true, 읽기 작업이면 false
 * not_present: 페이지 폴트가 발생한 페이지가 메모리에 없으면 true, 있으면 false
 * not_present - false: r/o page에 쓰기 작업 시도
 */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct thread *t = thread_current();
	struct page *page = NULL;
	/* Validate the fault */
	if (addr == NULL || is_kernel_vaddr(addr)) {
		return false;
	}
	void *rsp = f->rsp;
	if(!user){
		rsp = t->tf.rsp;
	}

	/* 스택 증가. 스택의 크기를 1메가로 제한 */
	if (USER_STACK >= addr && rsp - 8 <= addr && (1 << 20) >= (USER_STACK - ((uint64_t )t->stack_bottom))) {
		vm_stack_growth();
		return true;
	} 
	
	page = spt_find_page(spt, addr);
	if (page == NULL || (write && !page->writable)) {
		return false;
	}
	if (not_present) {
		return vm_do_claim_page(page);
	}
	return false;
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
void supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->pages, spt_hash, spt_less, NULL);
}

/* SPT를 src에서 dst로 복사한다. */
bool supplemental_page_table_copy(struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	struct hash_iterator i;

	hash_first(&i, &src->pages);
	while (hash_next(&i)) {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, h_elem);
		struct page *dst_page = NULL;
		if (src_page->frame == NULL) {
			if (!vm_alloc_page_with_initializer(src_page->uninit.type, src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux)) {
				return false;
			}
		}
		else {
			enum vm_type src_type = page_get_type(src_page);
			switch (src_type)
			{
			case VM_ANON:
				if (!vm_alloc_page_with_initializer(src_page->uninit.type, src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux)) {
					return false;
				}
				if (!vm_claim_page(src_page->va)) {
					return false;
				}
				break;
			case VM_FILE:
				if (!vm_alloc_page_with_initializer(src_page->uninit.type, src_page->va, src_page->writable, src_page->uninit.init, src_page->uninit.aux)) {
					return false;
				}
				if (!vm_claim_page(src_page->va)) {
					return false;
				}
				break;
			default:
				break;
			}
			dst_page = spt_find_page(dst, src_page->va);
			memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

void page_delete(const struct hash_elem *e, void *aux UNUSED){
	struct page *page = hash_entry(e, struct page, h_elem);
	vm_dealloc_page(page);
}
/* SPT가 들고있는 자원을 해제한다. */
void supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: 스레드가 들고 있는 SPT를 삭제하고 수정된 모든 내용을 스토리지에 저장한다. */
	
	hash_clear(&spt->pages, page_delete);
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