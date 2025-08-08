#include "userprog/syscall.h"
#include "lib/user/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "include/userprog/process.h"
#include "threads/init.h"
#include "include/threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/string.h"
#include "lib/round.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void *mmap_(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap_(void *addr);
struct lock filesys_lock; // 파일 읽기 쓰기 lock

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
		
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) 
{
	int sys_num = f->R.rax;
#ifdef VM
    thread_current()->rsp_stack = f->rsp; // 추가
#endif
	switch (sys_num)
	{
	case SYS_HALT:
		halt_();
		break;
	case SYS_EXIT:
		exit_(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork_(f->R.rdi);
		break;
	case SYS_EXEC:
		f->R.rax = exec_(f->R.rdi);
		break;
	case SYS_WAIT:
		f->R.rax = wait_(f->R.rdi);
		break;
	case SYS_CREATE:
		f->R.rax = create_(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove_(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open_(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize_(f->R.rdi);
		break;
	case SYS_READ:
        check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
		f->R.rax = read_(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
        check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
		f->R.rax = write_(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek_(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell_(f->R.rdi);
		break;
	case SYS_CLOSE:
		close_(f->R.rdi);
		break;
	case SYS_MMAP:
		mmap_(f->R.rdi, f->R.rsi, f->R.rdx, f->R.rcx, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap_(f->R.rdi);
		break;
	default:
		exit_(-1);
		break;
	}
}

// // Userprog check_address
// void check_address(void *address)
// {	
// 	// 커널영역이거나, address가 null이거나, 가상페이지에 할당되었는지
// 	if (is_kernel_vaddr(address) || address == NULL || pml4_get_page(thread_current()->pml4, address) == NULL)
// 		exit_(-1);
// }

// VM check_address
struct page * check_address(void *addr) {
    if (is_kernel_vaddr(addr) || addr == NULL || !spt_find_page(&thread_current()->spt, addr))
    {
        exit_(-1);
    }
    return spt_find_page(&thread_current()->spt, addr);
}

void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write) {
    if (buffer == NULL || size == 0) return;
    
    // 버퍼의 시작과 끝 주소를 페이지 단위로 정렬
    void *start_addr = pg_round_down(buffer);
    void *end_addr = pg_round_down((char*)buffer + size - 1);
    
    // 각 페이지에 대해 검증
    for (void *addr = start_addr; addr <= end_addr; addr += PGSIZE) {
        // 커널 주소 검증
        if (is_kernel_vaddr(addr)) {
            exit_(-1);
        }
        
        // 페이지 테이블에서 페이지 찾기
        struct page *page = spt_find_page(&thread_current()->spt, addr);
        if (page == NULL) {
            // 페이지가 없으면 스택 영역인지 확인
            // syscall_handler에서 전달받은 rsp 사용
            if (!((rsp - 8 <= addr) && 
                  (USER_STACK - (1 << 20) < addr) && 
                  (addr < USER_STACK))) {
                exit_(-1);
            }
            // 스택 영역이면 통과 (페이지 폴트에서 할당됨)
        } else {
            // 페이지가 있으면 쓰기 권한 확인
            if (to_write && !page->writable) {
                exit_(-1);
            }
        }
    }
}

void halt_(void)
{
	power_off();
}

void exit_(int status)
{
	struct thread *cur = thread_current(); // 현재 스레드 저장
	cur->exit_status = status; // 종료 상태 저장
	printf("%s: exit(%d)\n", cur->name, cur->exit_status); // 디버깅
	thread_exit(); // 현재 스레드 종료
}

pid_t fork_(const char *thread_name)
{
	check_address(thread_name);

	return process_fork(thread_name, NULL);
}

int exec_(const char *cmd_line)
{
	check_address(cmd_line);

	int size = strlen(cmd_line) + 1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);

	if (cmd_copy == NULL)
		return -1;
	
	memcpy(cmd_copy, cmd_line, size);
	
	if (process_exec(cmd_copy) == -1)
	{	
		palloc_free_page(cmd_copy);
		return -1;
	}

	return 0;
}

int wait_(pid_t pid)
{
	return process_wait(pid);
}

bool create_(const char *file, unsigned initial_size)
{
	check_address(file);
	
	// 주어진 이름과 초기 크기로 새로운 파일 생성하는 함수
	return filesys_create(file, initial_size);
}

bool remove_(const char *file)
{
	check_address(file);
	
	// 주어진 이름의 파일 삭제하는 함수
	return filesys_remove(file);
}

int open_(const char *file)
{
	check_address(file);
	
	struct file *open_file = filesys_open(file);
	
	if (open_file == NULL)
		return -1;
	
	int fd = process_add_file(open_file);
	
	if (fd == -1)
		file_close(open_file);
	
	return fd;
}

int filesize_(int fd)
{
	struct file *file = process_get_file(fd);
	
	if (file == NULL || fd < 3)
		return;
	
	return file_length(file);
}

int read_(int fd, void *buffer, unsigned size)
{
	// check_address(buffer);
	int read_bytes = 0;
	
	if (fd == 0) // 파일 디스크립터가 0이면
	{
		char c;
		unsigned char *buf = buffer;
		
		for (int i = 0; i < size; i++) // 길이만큼 반복하고
		{
			c = input_getc(); // input_getc() = 키보드 하나하나 입력이 가능
			*buf++ = c; // c를 buf에 저장하고 포인터 늘리기
			read_bytes++; // read_bytes 증가
		}
		
		return read_bytes; 
	}
	else if (fd == 1 || fd == 2) // 파일 디스크립터 1, 2 -> 표준 출력, 표준 에러는 읽을 수 없으므로
	{
		return -1; 
	}
	else // 3 이상 이라면
	{
		struct file *file = process_get_file(fd); 
		
		if (file == NULL)
			return -1;
		
		lock_acquire(&filesys_lock); // race condition 방지를 위해 잠금
		read_bytes = file_read(file, buffer, size);
		lock_release(&filesys_lock); // 해제
		return read_bytes;
	}
}

int write_(int fd, const void *buffer, unsigned size)
{
	// check_address(buffer);

	if (fd <= 0)
	{
		return -1;
	}
	else if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}
	else
	{
		struct file *file = process_get_file(fd);
		int write_bytes = -1;
		
		if (file == NULL)
			return -1;

		lock_acquire(&filesys_lock); // race condition 방지 락
		write_bytes = file_write(file, buffer, size);
		lock_release(&filesys_lock); // 락 해제
		
		return write_bytes;
	}
}

void seek_(int fd, unsigned position)
{
	struct file *file = process_get_file(fd);
	
	if (file == NULL || fd < 3)
		return;
	
	file_seek(file, position);
}

unsigned tell_(int fd)
{
	struct file *file = process_get_file(fd);
	
	if (file == NULL || fd < 3)
		return;
	
	return file_tell(file);
}

void close_(int fd)
{
	struct file *file = process_get_file(fd);
	
	if (file == NULL || fd < 3)
		return;
	
	process_close_file(fd);	
	file_close(file);
}

/**
 * @brief 메모리 매핑 시스템 콜
 * 
 * 파일을 메모리에 매핑합니다. 지정된 주소에 파일의 내용을 매핑하여
 * 파일을 메모리처럼 접근할 수 있게 합니다.
 * 
 * @param addr 매핑할 가상 주소 (페이지 정렬되어야 함)
 * @param length 매핑할 길이
 * @param writable 쓰기 가능 여부
 * @param fd 파일 디스크립터
 * @param offset 파일 내 오프셋
 * @return 성공시 매핑된 주소, 실패시 NULL
 */
void *mmap_(void *addr, size_t length, int writable, int fd, off_t offset) {
	check_address(addr);

	if (pg_ofs(addr) != 0 || length == 0) {
		return NULL;
	}

    if (fd == 0 || fd == 1) {
		return NULL;
	} 

    struct file *file = file_reopen(process_get_file(fd));
	if (file == NULL) {
		return NULL;
	}
	
	size_t file_size = filesize_(fd);
	if (file_size == -1 || file_size == 0) {
		file_close(file);
		return NULL;
	}
	
	// 매핑 길이가 파일 크기를 초과하는지 확인
	if (offset >= file_size || length > file_size - offset) {
		file_close(file);
		return NULL;
	}

	size_t page_count = DIV_ROUND_UP(length, PGSIZE);

    for (size_t i = 0; i < page_count; i++) {
		void *check_addr = addr + (i * PGSIZE);
		if (spt_find_page(&thread_current()->spt, check_addr) != NULL) {
			return NULL;
		}
	}

    void *result = do_mmap(addr, length, writable, file, offset);
    
	return result;
}