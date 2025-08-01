/* vm.c: Generic interface for virtual memory objects. */

#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void) {
    vm_anon_init();
    vm_file_init();
#ifdef EFILESYS /* For project 4 */
    pagecache_init();
#endif
    register_inspect_intr();
    /* DO NOT MODIFY UPPER LINES. */
    /* TODO: Your code goes here. */
    list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type page_get_type(struct page *page) {
    int ty = VM_TYPE(page->operations->type);
    switch (ty) {
        case VM_UNINIT:
            return VM_TYPE(page->uninit.type);
        default:
            return ty;
    }
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        /* TODO: Create the page, fetch the initialier according to the VM type,
         * TODO: and then create "uninit" page struct by calling uninit_new. You
         * TODO: should modify the field after calling the uninit_new. */

        /* TODO: Insert the page into the spt. */
    }
err:
    return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function. */

    return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
    int succ = false;
    /* TODO: Fill this function. */

    return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
/**
 * @brief 페이지 교체를 위한 희생자(victim) 프레임을 선택
 * @details 이 함수는 페이지 교체 정책에 따라 희생자 프레임을 결정
 * 현재 구현은 FIFO(First-In, First-Out) 알고리즘을 사용하며,
 * 전역 프레임 테이블(frame_table)에서 가장 먼저 들어온 프레임을 희생자로 선택
 * @return 희생자로 선택된 프레임에 대한 포인터를 반환
 * 만약 프레임 테이블이 비어있다면 NULL을 반환
 */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    if (list_empty(&frame_table)) {
        return NULL;
    }
    
    // FIFO 정책: 가장 오래된 프레임을 victim으로 선택
    struct list_elem *e = list_pop_front(&frame_table);

    victim = list_entry(e, struct frame, frame_elem);
    
    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/**
 * @brief 하나의 페이지를 교체(evict)하고, 해당 프레임을 반환
 * @details 이 함수는 페이지 교체 알고리즘(예: FIFO 또는 Clock)을 사용하여 희생자(victim) 프레임을 선택
 * 그런 다음, 희생자 페이지의 내용을 스왑 공간으로 내보냄
 * 이 함수는 교체된 후 비워진 프레임 구조체의 포인터를 반환
 * @return 교체되어 재사용 가능한 프레임에 대한 포인터. 오류 발생 시 NULL을 반환할 수 있음
 */
static struct frame *vm_evict_frame(void) {
    struct frame *victim = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */
    swap_out(victim->page);

    return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/**
 * @brief 사용자 풀에서 프레임을 할당하거나, 메모리가 부족할 경우 페이지를 교체하여 프레임을 가져옴
 * @details 이 함수는 먼저 사용자 풀에서 새로운 물리 페이지를 할당하려고 시도
 * 할당에 성공하면 새로운 프레임 구조체를 할당하고, 이를 물리 페이지에 연결한 후 전역 프레임 테이블에 추가
 * 만약 사용자 풀이 가득 차 있다면, vm_evict_frame()을 호출하여 기존 페이지를 교체해 사용 가능한 프레임을 확보한 후 반환
 * @return 유효한 프레임 구조체에 대한 포인터를 반환합니다. 복구 불가능한 오류 발생 시에는 패닉을 호출
 */
static struct frame *vm_get_frame(void) {
    /* TODO: Fill this function. */
    struct frame *frame = NULL;
    uint8_t *kpage;
    
    kpage = palloc_get_page(PAL_USER);
    
    if (kpage != NULL) {
        frame = (struct frame *)malloc(sizeof(struct frame));
        if (frame == NULL) {
            palloc_free_page(kpage);  // 메모리 누수 방지
            PANIC("Failed to allocate frame struct");
        }
        frame->kva = kpage;
        frame->page = NULL;
        
        list_push_back(&frame_table, &frame->frame_elem);

        return frame;
    } else {
        frame = vm_evict_frame();
        if (frame == NULL) {
            PANIC("todo");
        }
        return frame;
    }
}

/* Growing the stack. */
static void vm_stack_growth(void *addr UNUSED) {}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED,
                         bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
    struct page *page = NULL;
    /* TODO: Validate the fault */
    /* TODO: Your code goes here */

    return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED) {
    struct page *page = NULL;
    /* TODO: Fill this function */

    return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */

    return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    hash_init(spt, page_hash, page_less, NULL);
    // 1. 해쉬 테이블 초기화하기
    // 2. hash_init의 page_hash 만들기
    // 3. hash_init의 page_less 만들기
}

uint64_t page_hash(const struct hash_elem *e, void *aux) {

    return;
}

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {

    return;
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
}
