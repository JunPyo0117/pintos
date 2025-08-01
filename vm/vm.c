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

/// @brief claim_page()의 실제 로직을 수행합니다. 프레임을 할당하고, frame->page 및 page->frame 연결 등을 수행.
/// @param page 매핑 및 할당할 보조 페이지 테이블의 페이지 구조체 포인터
/// @return 성공 시 true, 프레임 할당 실패 또는 페이지 테이블 등록 실패 시 false
static bool vm_do_claim_page(struct page *page) {
    struct frame *frame = vm_get_frame();
    if (frame == NULL)
        return false;

    /* 1. 가상 페이지 ↔ 프레임 연결 */
    frame->page = page;
    page->frame = frame;

    /* 2. 가상 주소 ↔ 물리 주소(PA) 매핑 */
    if (!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
        /* 페이지 테이블 등록 실패 시 프레임 반납 */
        vm_dealloc_frame(frame);
        return false;
    }

    /* 3. 스왑인: 파일 읽기, 제로 페이지 채우기, 디스크에서 가져오기 등 */
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
