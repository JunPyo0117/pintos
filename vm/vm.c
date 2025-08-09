/* vm.c: Generic interface for virtual memory objects. */
#include "vm/vm.h"

#include "threads/malloc.h"
#include "vm/inspect.h"

#include "vm/anon.h"
#include "vm/file.h"
#include "vm/uninit.h"

#include "lib/kernel/hash.h"
#include "lib/string.h"
#include "threads/vaddr.h"

/* Global frame table */
struct list frame_table;


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

/**
 * @brief 가상 주소 upage에 초기화 가능한 페이지를 할당하는 함수
 * 
 * @details 주어진 가상 주소(upage)가 이미 SPT에 등록되어 있지 않은 경우, 
 *          VM 타입에 따라 적절한 초기화자(initializer)를 선택하고 uninit 페이지를 생성
 *          이후 SPT에 페이지를 등록
 * 
 * @param type 페이지의 타입 (ex. VM_ANON, VM_FILE)
 * @param upage 유저 가상 주소 (페이지 단위)
 * @param writable 해당 페이지가 사용자에 의해 쓰기 가능한지 여부
 * @param init 실제 데이터 로딩을 수행할 사용자 정의 초기화 함수
 * @param aux 초기화 함수에 전달할 보조 데이터
 * 
 * @return 성공 시 true, 실패 시 false
 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
                                    vm_initializer *init, void *aux) {
    ASSERT(VM_TYPE(type) != VM_UNINIT)

    struct supplemental_page_table *spt = &thread_current()->spt;

    /* Check wheter the upage is already occupied or not. */
    if (spt_find_page(spt, upage) == NULL) {
        // TODO: Create the page, fetch the initialier according to the VM type,
        struct page *page = (struct page *) malloc(sizeof(struct page));
        
        if (page == NULL)
            goto err;
        
        vm_initializer *page_initailizer = NULL;

        switch (VM_TYPE(type)) {
            case VM_ANON:
                page_initailizer = anon_initializer;
                break;
            case VM_FILE:
                page_initailizer = file_backed_initializer;
                break;  
            default:
                free(page);
                goto err;
        }

        // TODO: and then create "uninit" page struct by calling uninit_new. You
        // TODO: should modify the field after calling the uninit_new.
        uninit_new(page, upage, init, type, aux, page_initailizer);
        page->writable = writable;
        
        // TODO: Insert the page into the spt.
        if (!spt_insert_page(spt, page)) {
            free(page);
            goto err;
        }

        return true;
    }
err:
    return false;
}

/*
* @brief SPT에서 가상 주소 va에 해당하는 struct page *를 찾습니다.
*/
struct page *spt_find_page(struct supplemental_page_table *spt, void *va) {
    struct page temp_page;
    memset(&temp_page, 0, sizeof(struct page));
    /* TODO: Fill this function. */
    temp_page.va = pg_round_down(va);


    struct hash_elem *found_element = hash_find(&spt->spt_hash, &temp_page.hash_elem);

    if (found_element != NULL)
        return hash_entry(found_element, struct page, hash_elem);
    else
        return NULL;
}
/*
* @brief page->va를 기준으로 SPT에 페이지 등록, 중복 시 false
* @param spt (페이지를 등록할 보조 페이지 테이블의 포인터)
* @param page (등록할 페이지의 정보가 담긴 구조체의 포인터)
* @return 삽입에 성공하면 true, 가상 주소가 중복되어 실패하면 false
*/
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
            PANIC("vm_evict_frame returned NULL");
        }

        return frame;
    }
}

/* Growing the stack. */
/**
 * @brief 스택을 한 페이지 확장합니다.
 * @details 반환 타입을 void에서 bool로 수정하여 성공/실패 여부를 알립니다.
 * @return 스택 확장에 성공하면 true, 실패하면 false를 반환합니다.
 */

static bool vm_stack_growth(void *addr) {
    void *page_fault_addr = pg_round_down(addr);

    if(!vm_alloc_page(VM_ANON, page_fault_addr, true)){
        return false;
    }

    bool success = vm_claim_page(page_fault_addr);
    if(!success){
        return false;
    }

    return true;
}

/* Handle the fault on write_protected page */
static bool vm_handle_wp(struct page *page UNUSED) {}

/* Return true on success */
/**
 * @brief 페이지 폴트를 처리하려고 시도하는 메인 함수.
 * @details 폴트의 유효성을 검사하고, 해결 가능한 경우 vm_do_claim_page를 호출합니다.
 * @param f 인터럽트 프레임
 * @param addr 폴트가 발생한 가상 주소
 * @param user 유저 모드에서 발생했는지 여부
 * @param write 쓰기 시도 중 발생했는지 여부
 * @param not_present 페이지가 메모리에 없어 발생했는지 여부
 * @return 폴트 처리에 성공하면 true, 실패하면 false
 */
bool vm_try_handle_fault(struct intr_frame *f, void *addr, bool user,
                         bool write, bool not_present) {
    struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *page = NULL;

    if(!not_present || addr == NULL || is_kernel_vaddr(addr)){
        return false;
    }

    page = spt_find_page(spt, addr);
    if(page == NULL){
        
        void *rsp = user ? f->rsp : thread_current()->rsp_stack;
        if((rsp - 8 <= addr) && (USER_STACK - (1 << 20) < addr) && (addr < USER_STACK)){
            return vm_stack_growth(addr);
        }

        return false;
    }
    
    
    if(write && !page->writable){
        return false;
    }

    return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page) {
    destroy(page);
    free(page);
}

void vm_dealloc_frame(struct frame *frame) {
    if (frame == NULL) {
        return;
    }

    palloc_free_page(frame);
    list_remove(&frame->frame_elem);
    frame->page = NULL;
    free(frame);
}

/* Claim the page that allocate on VA. */
/*
* @brief 주어진 가상 주소에 해당하는 페이지를 프레임에 매핑하는 함수
* @param va 접근하려는 가상 주소
* @return 페이지를 성공적으로 매핑했다면 true, 실패했다면 false
*/
bool vm_claim_page(void *va) {
    /* TODO: Fill this function */
    struct page *page = spt_find_page(&thread_current()->spt, va);

    if (page == NULL)
        return false;

    return vm_do_claim_page(page);
}

/*
* @brief claim_page()의 실제 로직을 수행합니다. 프레임을 할당하고, frame->page 및 page->frame 연결 등을 수행.
* @param page 매핑 및 할당할 보조 페이지 테이블의 페이지 구조체 포인터
* @return 성공 시 true, 프레임 할당 실패 또는 페이지 테이블 등록 실패 시 false
*/
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
/*
* @brief 보조 페이지 테이블을 초기화하는 함수
* @param spt 초기화할 보조 페이지 테이블 구조체 포인터
*/
void supplemental_page_table_init(struct supplemental_page_table *spt) {
    hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/// @brief 페이지 구조체의 va를 기반으로 해시를 생성하는 함수
/// @param e 해시 테이블 내의 hash_elem 포인터
/// @return 페이지의 가상 주소를 해시한 64비트 해시 값
uint64_t page_hash(const struct hash_elem *e, void *aux) {
    struct page *page = hash_entry(e, struct page, hash_elem);
    return hash_bytes(&page->va, sizeof page->va);  
}

/*
* @brief 페이지를 가상 주소 기준으로 비교하는 함수
* @param a 비교하는 해시 요소 1
* @param b 비교하는 해시 요소 2
* @return a의 가상 주소가 작다면 true, b의 가상 주소가 작다면 false
*/
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux) {
    return hash_entry(a, struct page, hash_elem)->va 
            < hash_entry(b, struct page, hash_elem)->va;
}

/* Copy supplemental page table from src to dst */
/**
 * @brief 복사본 supplemental_page_table(dst)에 src의 내용을 복사하는 함수
 * 
 * @details src의 각 페이지를 순회하면서 dst에 같은 가상 주소로 페이지를 할당하고,
 *          페이지 유형에 따라 적절히 초기화 또는 내용을 복사
 *          복사 중 실패가 발생하면 dst를 모두 정리하고 false를 반환
 * 
 * @param dst 복사 대상 supplemental_page_table 포인터
 * @param src 복사 원본 supplemental_page_table 포인터
 * @return 성공 시 true, 실패 시 false
 * 
 */
bool supplemental_page_table_copy(struct supplemental_page_table *dst, struct supplemental_page_table *src) {
    struct hash_iterator i;
    hash_first(&i, &src->spt_hash);

    while (hash_next(&i)) 
    {
        struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem);
        enum vm_type parent_type = page_get_type(parent_page);

        if (parent_type == VM_UNINIT) 
        {
            // UNINIT 페이지는 그대로 복사 (lazy loading 유지)
            if (!vm_alloc_page_with_initializer(parent_page->uninit.type,                 
                                                parent_page->va,                 
                                                parent_page->writable,                 
                                                parent_page->uninit.init, 
                                                parent_page->uninit.aux)) 
                goto err;
        }
        else 
        {
            // 이미 초기화된 페이지들 처리
            if (!vm_alloc_page(parent_type, parent_page->va, parent_page->writable))
                goto err;

            struct page *child_page = spt_find_page(dst, parent_page->va);
            if (child_page == NULL)
                goto err;   
            
            // 부모 페이지가 물리 메모리에 로드되어 있는 경우에만 자식도 로드
            if (parent_page->frame != NULL) {
                if (!vm_claim_page(child_page->va))
                    goto err;
                
                if (child_page->frame == NULL)
                    goto err;
                
                memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
            }
            // 부모가 로드되지 않았다면 자식도 lazy loading으로 유지
            // (vm_claim_page를 호출하지 않음)
        }
    }

    return true;

err:
    supplemental_page_table_kill(dst);
    return false;
}

/* Free the resource hold by the supplemental page table */
/**
 * @brief SPT의 모든 페이지를 제거하는 함수
 *          필요한 경우 디스크에 반영
 * 
 * @details page_destory를 통해 각 페이지에 대해 정리 및 메모리 해제를 수행
 *          파일 기반 페이지의 경우 수정된 내용이 있다면 디스크에 기록
 * 
 * @param spt 제거할 보조 페이지 테이블 포인터
 */
void supplemental_page_table_kill(struct supplemental_page_table *spt) {
    /* TODO: Destroy all the supplemental_page_table hold by thread and
     * TODO: writeback all the modified contents to the storage. */
    hash_clear(&spt->spt_hash, page_destory);
}

/**
 * @brief 해시 테이블 항목 제거 시 호출되는 콜백 함수
 *
 * @details 페이지 타입별 destory를 호출하여 필요한 정리를 수행하고,
 *          페이지 구조체의 메모리 해제
 * 
 * @param elem 제거할 페이지의 해시 요소 포인터
 */
void page_destory(struct hash_elem *elem) {
    struct page *page = hash_entry(elem, struct page, hash_elem);
    destroy(page);
    free(page);
}