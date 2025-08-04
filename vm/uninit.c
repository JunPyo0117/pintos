/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * */

#include "vm/uninit.h"

#include "vm/vm.h"

static bool uninit_initialize(struct page *page, void *kva);
static void uninit_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
    .swap_in = uninit_initialize,
    .swap_out = NULL,
    .destroy = uninit_destroy,
    .type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
/**
 * @brief Uninitialized 페이지를 초기화하여 struct page를 설정하는 함수
 *
 * @details 이 함수는 아직 메모리에 로드되지 않은(uninitialized) 페이지에 대해 `struct page`를 초기화한다.
 * 해당 페이지를 이후 페이지 폴트 시 동적으로 초기화할 수 있도록 준비한다.
 *
 * 초기화된 `page`는 내부적으로 `uninit_page` 정보를 가지고 있으며,
 * 실제 프레임은 아직 할당되지 않은 상태 (`frame = NULL`)이다.
 *
 * @param page          초기화할 대상 페이지 구조체 포인터
 * @param va            사용자 가상 주소
 * @param init          실제 초기화를 수행할 커스텀 함수 포인터 (lazy load 등)
 * @param type          페이지 타입 (VM_ANON, VM_FILE 등)
 * @param aux           초기화 시 전달할 보조 데이터 포인터
 * @param initializer   `vm_type`에 따른 기본 페이지 초기화 함수 포인터 (예: anon_initializer 등)
 */
void uninit_new(struct page *page, void *va, vm_initializer *init, enum vm_type type, void *aux,
                bool (*initializer)(struct page *, enum vm_type, void *)) {
    ASSERT(page != NULL);

    *page = (struct page){.operations = &uninit_ops,
                          .va = va,
                          .frame = NULL, /* no frame for now */
                          .uninit = (struct uninit_page){
                              .init = init,
                              .type = type,
                              .aux = aux,
                              .page_initializer = initializer,
                          }};
}

/* Initalize the page on first fault */

/**
 * @brief 첫 접근 시 초기화되지 않은(uninit) 페이지를 실제로 초기화하는 함수
 *
 * 해당 페이지가 처음 접근되어 페이지 폴트가 발생했을 때 호출
 * 페이지를 타입에 따라 실제로 초기화하고, 
 * 만약 추가 초기화 함수가 등록되어 있다면 그 함수도 함께 호출
 *
 * @param page 초기화할 대상 페이지 구조체
 * @param kva  해당 페이지가 매핑될 커널 가상 주소
 *
 * @return 초기화에 성공하면 true, 실패하면 false를 반환합니다.
 */
static bool uninit_initialize(struct page *page, void *kva) {
    struct uninit_page *uninit = &page->uninit;

    /* Fetch first, page_initialize may overwrite the values */
    vm_initializer *init = uninit->init;
    void *aux = uninit->aux;

    /* TODO: You may need to fix this function. */
    return uninit->page_initializer(page, uninit->type, kva) && (init ? init(page, aux) : true);
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void uninit_destroy(struct page *page) {
    struct uninit_page *uninit UNUSED = &page->uninit;
    /* TODO: Fill this function.
     * TODO: If you don't have anything to do, just return. */
}
