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

/// @brief SPT에서 가상 주소 va에 해당하는 struct page *를 찾습니다.
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
    struct page temp_page;
    /* TODO: Fill this function. */
    temp_page.va = pg_round_down(va);


    struct hash_elem *found_element = hash_find(&spt->spt_hash, &temp_page.hash_elem);

    if (found_element != NULL)
        return hash_entry(found_element, struct page, hash_elem);
    else
        return NULL;
}

/// @brief page->va를 기준으로 SPT에 페이지 등록, 중복 시 false
/// @param spt (페이지를 등록할 보조 페이지 테이블의 포인터)
/// @param page (등록할 페이지의 정보가 담긴 구조체의 포인터)
/// @return 삽입에 성공하면 true, 가상 주소가 중복되어 실패하면 false
bool spt_insert_page(struct supplemental_page_table *spt , struct page *page ) {
    int succ = false;
    struct hash_elem *result = hash_insert(&spt->spt_hash, &page->hash_elem);
    
    if(result == NULL){
        succ = true;
    }
    return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page) {
    vm_dealloc_page(page);
    return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim(void) {
    struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *vm_evict_frame(void) {
    struct frame *victim UNUSED = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *vm_get_frame(void) {
    struct frame *frame = NULL;
    /* TODO: Fill this function. */

    ASSERT(frame != NULL);
    ASSERT(frame->page == NULL);
    return frame;
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

/// @brief 주어진 가상 주소에 해당하는 페이지를 프레임에 매핑하는 함수
/// @param va 접근하려는 가상 주소
/// @return 페이지를 성공적으로 매핑했다면 true, 실패했다면 false
bool vm_claim_page(void *va UNUSED) {
    /* TODO: Fill this function */
    struct page *page = spt_find_page(&thread_current()->spt, va);

    if (page == NULL)
        return false;

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

/// @brief 보조 페이지 테이블을 초기화하는 함수
/// @param spt 초기화할 보조 페이지 테이블 구조체 포인터
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED) {
    hash_init(spt, page_hash, page_less, NULL);
}

/// @brief 페이지 구조체의 va를 기반으로 해시를 생성하는 함수
/// @param e 해시 테이블 내의 hash_elem 포인터
/// @return 페이지의 가상 주소를 해시한 64비트 해시 값
uint64_t page_hash(const struct hash_elem *e, void *aux) {
    struct page *page = hash_entry(e, struct page, hash_elem);
    return hash_bytes(page->va, sizeof *page->va);
}

/// @brief 페이지를 가상 주소 기준으로 비교하는 함수
/// @param a 비교하는 해시 요소 1
/// @param b 비교하는 해시 요소 2
/// @return a의 가상 주소가 작다면 true, b의 가상 주소가 작다면 false
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    return hash_entry(a, struct page, hash_elem)->va 
            < hash_entry(b, struct page, hash_elem)->va;
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
                                  struct supplemental_page_table *src UNUSED) {}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
}
