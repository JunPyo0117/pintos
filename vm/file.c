/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "lib/round.h"
#include "threads/vaddr.h"
#include "include/userprog/process.h"

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

	struct file_page *aux = (struct file_page *)page->uninit.aux;
	file_page->file = aux->file;
	file_page->offset = aux->offset;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	free(aux);
	return true;
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

/**
 * @brief 메모리 매핑 수행
 * 
 * 파일을 메모리에 매핑하는 실제 작업을 수행합니다.
 * 각 페이지별로 파일 백드 페이지를 생성하고 가상 메모리에 할당합니다.
 * 
 * @param addr 매핑할 가상 주소
 * @param length 매핑할 길이
 * @param writable 쓰기 가능 여부
 * @param file 매핑할 파일 객체
 * @param offset 파일 내 오프셋
 * @return 성공시 매핑된 주소, 실패시 NULL
 */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	if (file == NULL) {
        return NULL;
    }

	size_t file_size = file_length(file);
    if (file_size == 0) {
        return NULL;
    }
	
	// 매핑 길이가 파일 크기를 초과하는지 확인
	if (offset >= file_size || length > file_size - offset) {
		return NULL;
	}
	
	size_t page_count = DIV_ROUND_UP(length, PGSIZE);

	for (size_t i = 0; i < page_count; i++) {
		void *page_addr = addr + (i * PGSIZE);
		size_t page_offset = offset + (i * PGSIZE);
		size_t page_read_bytes = (length - (i * PGSIZE) < PGSIZE) ? length - (i * PGSIZE) : PGSIZE;
		
		struct file_page *file_page = malloc(sizeof(struct file_page));
		if (file_page == NULL) {
			return NULL;
		}
		
		file_page->file = file;
		file_page->offset = page_offset;
		file_page->read_bytes = page_read_bytes;
		file_page->zero_bytes = PGSIZE - page_read_bytes;
		file_page->page_count = page_count;  // 전체 페이지 수 저장
		
		if (!vm_alloc_page_with_initializer(VM_FILE, page_addr, writable, file_backed_initializer, file_page)) {
			free(file_page);
			return NULL;
		}
	}
    
    return addr;
}

/* Do the munmap */
void do_munmap(void *addr) {
    struct thread *t = thread_current();
    struct page *page = spt_find_page(&t->spt, addr);
    
    if (page == NULL)
        return;
    
    // 매핑된 페이지 수 확인
    int page_count = page->file.page_count; // 또는 mapped_page_count
    
    for (int i = 0; i < page_count; i++) {
        struct page *p = spt_find_page(&t->spt, addr + (PGSIZE * i));
        if (p == NULL)
            break;
            
        // dirty bit 체크 후 파일에 쓰기
        if (pml4_is_dirty(t->pml4, p->va)) {
            file_write_at(p->file.file, p->frame->kva, 
                         p->file.read_bytes, p->file.offset);
            //pml4_set_dirty(t->pml4, p->va, false);
        }
        
        // 페이지 테이블에서 제거
        pml4_clear_page(t->pml4, p->va);
        
        // 물리 메모리 해제
        if (p->frame)
            palloc_free_page(p->frame->kva);
            //page->frame = NULL;
            
        // SPT에서 제거
        hash_delete(&t->spt.spt_hash, &p->hash_elem);
        //free(page);
    }
    
    //lock_release(&filesys_lock);
}
