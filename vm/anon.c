/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "devices/disk.h"
#include "vm/vm.h"
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

struct bitmap *swap_table;
// static struct lock swap_lock;


/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
    .swap_in = anon_swap_in,
    .swap_out = anon_swap_out,
    .destroy = anon_destroy,
    .type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/**
 * @brief 익명 페이지 서브시스템을 초기화합니다.
 * @details 시스템 부팅 시 한 번 호출되어 스왑 디스크를 설정
 *          스왑 공간을 관리할 비트맵과 락을 초기화
 */
void vm_anon_init(void) {
    /* TODO: Set up the swap_disk. */
    swap_disk = NULL;
    swap_disk = disk_get(1,1);

    if(swap_disk == NULL){
        return NULL;
    }

    size_t swap_slots_count = disk_size(swap_disk) / (PGSIZE / DISK_SECTOR_SIZE);

    swap_table = bitmap_create(swap_slots_count);
    if(swap_table == NULL){
        PANIC("FAILED TO CREATE SWAP TABLE BITMAP");
    }

    //(필요시) 스왑 관리를 위한 락을 초기화
    //lock_init(&swap_lock)
}

/*
 * @brief 익명 페이지용 초기화 함수
 * @param page 초기화할 페이지 구조체 포인터
 * @param type 페이지 타입 (VM_ANON 등)
 * @param kva 페이지가 연결될 커널 가상 주소 (kernel virtual address)
 * @return 항상 true를 반환 (익명 페이지는 초기화 실패 가능성이 없음)
 * @details
 * - 이 함수는 uninit 페이지가 물리 프레임을 할당받을 때 호출
 * - 페이지 타입에 맞는 `operations` 테이블을 설정
 * - swap-out 이력이 없는 새 페이지임을 나타내기 위해 `swap_slot_index = (size_t)-1`로 초기화
 * - 이 값은 페이지가 스왑된 적이 없음을 나타내며, 이후 스왑 아웃 시 swap 디스크의 인덱스로 갱신
*/
bool anon_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &anon_ops;

    struct anon_page *anon_page = &page->anon;
    anon_page->swap_slot_index = (size_t)-1;

    return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in(struct page *page, void *kva) {
    struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out(struct page *page) {
    struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void anon_destroy(struct page *page) {
    struct anon_page *anon_page = &page->anon;
}
