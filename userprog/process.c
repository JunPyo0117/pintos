#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <lib/stdio.h>
#include <stdlib.h>
#include <lib/string.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "lib/user/syscall.h"

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
extern struct lock filesys_lock;

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *ptr;
	strtok_r(file_name, " ", &ptr);

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);

	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */

/// @brief 현재 스레드의 레지스터 상태와 자원을 복제, 자식 프로레스 생성 및 초기화 대기하는 함수
/// @param name 생성할 자식 스레드의 이름
/// @param UNUSED 
/// @return 자식 스레드의 TID 또는 실패 시 TID_ERROR
tid_t process_fork (const char *name, struct intr_frame *if_ UNUSED) 
{
	// 부모 스레드 불러오기
	struct thread *parent = thread_current();
	// 현재 스레드의 intr_frame 위치를 계산하고 복사 (현재 스레드의 if_는 페이지 마지막에 븥어있음)
	struct intr_frame *f = (pg_round_up(rrsp()) - sizeof(struct intr_frame));
	// 자식 프로세스가 부모의 레지스터 상태를 복제할 수 있도록 parent->parent_if에 저장
	memcpy(&parent->parent_if, f, sizeof(struct intr_frame));
	// 자식 스레드 생성
	tid_t child_tid = thread_create(name, PRI_DEFAULT, __do_fork, parent);

	// 자식 생성 실패 시
	if (child_tid == TID_ERROR)
		return TID_ERROR;

	// tid로 자식 찾기
	struct thread *child = get_child_tid(child_tid);

	// 자식을 못찾으면
	if (child == NULL)
		return TID_ERROR;

	// 자식이 fork 완료 될때까지 대기
	sema_down(&child->fork_sema);

	// 자식이 fork중 실패하였다면
	if (child->exit_status == TID_ERROR)
		return TID_ERROR;

	return child_tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */

/// @brief 부모 프로세스의 페이지 테이블 엔트리 복사하여 자식 프로세스 페이지 테이블 추가하는 함수
/// @param pte 부모 페이지의 테이블 엔트리 포인터
/// @param va 복사할 가상 주소
/// @param aux 부모 프로세스를 가리키는 포인터
/// @return 성공 시 true, 실패 시 false
static bool duplicate_pte (uint64_t *pte, void *va, void *aux) 
{
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	// 부모 페이지가 커널 가장 주소라면 복사할 필요 없으므로 true 반환
	if (is_kernel_vaddr(va))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	// 부모 프로세스의 pml4에서 인자로 들어온 virtual address를 이용해 물리 주소를 가져오기
	parent_page = pml4_get_page (parent->pml4, va);

	if (parent_page == NULL) 
		return false;

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	// 자식 프로세스를 위해 유저 페이지 하나를 할당하고, 결과를 newpage에 저장
	newpage = palloc_get_page(PAL_USER);

	if (newpage == NULL)
		return false;

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	// 부모의 페이지 내용을 자식의 페이지에 복사
	// 쓰기 가능 여부를 writable에 저장
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	// 복사한 newpage를 자신의 pml4에 va위치에 매핑
	// 쓰기 가능 여부를 writable 플래그를 함께 전달
	if (!pml4_set_page (current->pml4, va, newpage, writable)) 
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
		
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */

/// @brief 부모 프로세스 실행 컨텍스으와 자원을 복사하고 자식 프로세스 초기화, 실행하는 함수
/// @param aux 부모 프로세스를 가리키는 포인터
static void __do_fork (void *aux) 
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0; // // 자식은 0을 return 해야하므로 0으로 설정

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	if (parent->fd_index >= FDCOUNT_LIMIT) // 예외처리
		goto error;
	
	current->fd_index = parent->fd_index; // fd_index 복사

	for (int fd = 3; fd < parent->fd_index; fd++) // 부모의 file_index 까지 순회하면서
	{
		if (parent->fd_table[fd] == NULL) // NULL인건 건너뛰고
			continue;
		
		current->fd_table[fd] = file_duplicate(parent->fd_table[fd]); // 자식의 파일에 부모의 파일을 복사
	}
	
	sema_up(&current->fork_sema); // fork_sema up
	process_init ();
	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	current->exit_status = -1;
	sema_up(&current->fork_sema); // 복제 실패 시 현재 fork_sema 블락 해제
	thread_exit ();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */

/// @brief 현재 프로세스에서 사용자 프로그램을 실행하는 함수
/// @param f_name 실행할 프로그램 이름, 인자 문자열
/// @return 성공 시 반환x, 실패 시 -1 반환 
int process_exec(void *f_name) 
{
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	// 현재 사용자 프로세스 정리
	process_cleanup ();
	supplemental_page_table_init (&thread_current()->spt);
	/* 파싱해서 넘기기 */
	char *token, *save_ptr; // 토큰 분리에 사용할 포인터
	char *arg_list[32]; 	// 파싱한 문자를 저장할 배열
	int arg_cnt = 0;		// 인자 개수

	// strtok_r를 이름을 공백으로 분할
	for (token = strtok_r(file_name," ", &save_ptr); token != NULL; token = strtok_r(NULL, " ", &save_ptr))
		arg_list[arg_cnt++] = token;	// 분리한 토큰을 arg_list에 저장
	
	/* And then load the binary */
	// ELF 실행 파일 로드

	success = load (arg_list[0], &_if);

	/* If load failed, quit. */
	// 로드 실패 시 종료
	if (!success){
        exit_(-1);
    }

	argument_stack(arg_list, arg_cnt, &_if);
	// hex_dump (_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	palloc_free_page (file_name);

	/* Start switched process. */
	// 성공 시 프로세스 스위치
	do_iret (&_if);
	NOT_REACHED ();
}

/// @brief 사용자 프로그램의 인자를 스택에 올리는 함수
/// @param argv 프로그램 실행 시 전달할 문자열 인자 배열
/// @param argc 인자 개수
/// @param if_ 사용자 스택과 레지스터 정보를 담고있는 프레임 포인터
void argument_stack(char **argv, int argc, struct intr_frame *if_)
{
	char *arg_addr[32]; // 인자 문자열이 위치한 주소를 저장할 배열
	int argv_len;

	// 문자열을 유저 스택에 복사 (역순으로)
	for (int i = argc - 1; i >= 0; i--)
	{
		argv_len = strlen(argv[i]) + 1; // 널 문자 포함 길이 계산
		if_->rsp -= argv_len;			// 스택 포인터 감소 (문자열 길이만큼 감소)
		memcpy(if_->rsp, argv[i], argv_len); // 해당 인자 스택에 복사
		arg_addr[i] = if_->rsp; 		// 복사한 문자열의 시작 주소 저장 
	}

	// 8바이트 정렬을 위해 패딩 처리
	while (if_->rsp % 8) // 8의 배수가 될때까지
		*(uint8_t *)(--if_->rsp) = 0; // 1 바이트 씩 0 패딩 후 스택 포인터 감소
	
	// argc 배열의 끝을 나타내는 NULL 포인터 추가 argc[argc] = NULL
	if_->rsp -= 8;
	memset(if_->rsp, 0, sizeof(char *));

	// argv[i] 배열의 각 요소 (인자 문자열의 주소)를 스택에 저장
	for (int i = argc - 1; i >= 0; i--)
	{
		if_->rsp -= 8;
		memcpy(if_->rsp, &arg_addr[i], sizeof(char *)); // 해당 인자 문자열 주소를 스택에 복사
	}
	
	// fake return address
	if_->rsp -= 8;
	memset((void *)if_->rsp, 0, 8);

	// 레지스터 설정
	if_->R.rdi = argc; // 첫 번째 인자 argc -> %rdi
	if_->R.rsi = if_->rsp + 8; // 두 번째 인자 argv -> %rsi
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */

/// @brief 자식 프로세스가 종료될 때까지 기다리는 함수
/// @param child_tid 기다릴 자식 스레드의 TID
/// @return 종료 상태 반환, 실패 시 -1
int process_wait (tid_t child_tid UNUSED) 
{
	struct thread *child_thread = get_child_tid(child_tid);

	// child_thread가 NULL, 이미 준비중이라면
	if (child_thread == NULL)
		return -1;

	sema_down(&child_thread->exit_sema); // 자식의 exit_sema 대기 (자식 종료를 대기)
	int exit_status = child_thread->exit_status; // 자식의 종료 상태 저장
	list_remove(&child_thread->child_elem); // 자식 리스트에서 제거
	sema_up(&child_thread->wait_sema); // 자식의 exit() 후 자기 메모리를 해제 할 수 있도록 wait_sema를 올릶
	return exit_status;
}

/// @brief 현재 프로세스의 자식 프로세스를 찾는 함수
/// @param child_tid 찾고자 하는 자식 프로세스의 TID
/// @return 성공 시 자식 프로세스 포인터 반환, 실패 시 NULL 
struct thread *get_child_tid(tid_t child_tid) 
{
    struct thread *parent = thread_current();
    struct list_elem *e;

    for (e = list_begin(&parent->child_list); e != list_end(&parent->child_list); e = list_next(e)) 
	  {
        struct thread *child = list_entry(e, struct thread, child_elem);

        if (child->tid == child_tid)
            return child;
    }
    return NULL;  // 못 찾으면 NULL
}

/* Exit the process. This function is called by thread_exit (). */

/// @brief 현재 실행 중인 프로세스 종료하고 자원 정리하는 함수
void process_exit (void) 
{
	struct thread *curr = thread_current ();

	// 파일 디스크립터 닫기
	for (int fd = 0; fd < curr->fd_index; fd++)
	{
		struct file *f = curr->fd_table[fd];

		if (f != NULL)
		{
			file_close(f);
			curr->fd_table[fd] = NULL;
		}
	}

	// 러닝 중인 파일 닫기
	if (curr->runn_file)
		file_close(curr->runn_file);

	// 테이블 메모리 해제
	palloc_free_multiple(curr->fd_table, FDPAGES);
	process_cleanup ();
	sema_up(&curr->exit_sema); 	// 부모에게 죽음을 알림
	sema_down(&curr->wait_sema);// 부모가 wait이 끝날대 까지 대기
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/// @brief 현재 스레드의 파일 디스크립터 테이블에 파일을 추가하는 함수
/// @param file 넣을 파일
/// @return 성공 시 table 인덱스 반환, 실패 시 -1 반환
int process_add_file(struct file *file)
{
	if (file == NULL)
		return -1;
	
	struct thread *cur_thread = thread_current();
	
	if (cur_thread->fd_index >= FDCOUNT_LIMIT)
		return -1;
	
	cur_thread->fd_table[cur_thread->fd_index++] = file;

	return cur_thread->fd_index - 1;
}

/// @brief 현재 스레드의 fd번째 파일 정보 얻는 함수
struct file* process_get_file(int fd)
{
	struct thread *cur_thread = thread_current();

	// fd가 유효 범위 밖이거나, fd 가 3보다 작거나, fd_table[fd]가 NULL이라면
	if (fd >= FDCOUNT_LIMIT || fd < 3 || cur_thread->fd_table[fd] == NULL) 
		return NULL; // NULL 반환
	
	return cur_thread->fd_table[fd];
}

/// @brief 현재 스레드의 fd번째 파일 삭제하는 함수
/// @param fd 파일 디스크립터 인덱스
/// @return 성공 시 0, 실패 시 -1 반환
int process_close_file(int fd)
{
	struct thread *cur_thread = thread_current();

	if (fd >= FDCOUNT_LIMIT || fd < 3 || cur_thread->fd_table[fd] == NULL) 
	{
		return -1;
	}
	else
	{
		cur_thread->fd_table[fd] = NULL;
		return 0;
	}
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool load (const char *file_name, struct intr_frame *if_) 
{
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	t->runn_file = file;   // 현재 실행하고 있는 실행 파일을 저장
	file_deny_write(file); // 실행 파일을 다른 곳에서 write 하는 걸 막기

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;
done:
	/* We arrive here whether the load is successful or not. */
	// file_close (file); 실행 중에 닫아버리면 write 보호가 유지되지 못함
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

/**
 * @brief 페이지 폴트 발생 시 실제로 세그먼트를 디스크에서 로드하는 함수
 * 
 * 이 함수는 지연 로딩(Lazy Loading) 메커니즘의 핵심 함수로, 페이지 폴트가
 * 발생했을 때 호출됩니다. 페이지 테이블에는 엔트리가 있지만 실제 물리 메모리에
 * 로드되지 않은 페이지에 접근할 때 이 함수가 실행
 * @param page 페이지 폴트가 발생한 페이지 구조체
 * @param aux lazy_load_segment에서 전달된 세그먼트 정보 (struct segment_info *)
 * @return 성공 시 true, 실패 시 false
 * @note 이 함수는 페이지 폴트 핸들러에서 호출됨
 * @note aux는 segment_info 구조체로 캐스팅하여 사용
 * @note 파일 읽기 후 반드시 file_close()와 free() 호출하여 리소스 정리
 */
bool lazy_load_segment(struct page *page, void *aux) {
    /* TODO: Load the segment from the file */
    /* TODO: This called when the first page fault occurs on address VA. */
    /* TODO: VA is available when calling this function. */

    struct segment_info *segment_info = (struct segment_info *)aux;
    struct file *file = segment_info->file;
    off_t ofs = segment_info->ofs;
    size_t page_read_bytes = segment_info->page_read_bytes;
    size_t page_zero_bytes = segment_info->page_zero_bytes;
		
    uint8_t *kva = page->frame->kva;
    
    if (file_read_at(file, kva, page_read_bytes, ofs) != (int)page_read_bytes) {
        file_close(file);
        free(segment_info);
        return false; 
    }
    
    memset(kva + page_read_bytes, 0, page_zero_bytes);
    
    free(segment_info);
	return true;
}


/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/**
 * @brief 파일에서 세그먼트를 가상 메모리에 지연 로딩 방식으로 로드
 * 
 * 이 함수는 파일의 OFS 오프셋에서 시작하는 세그먼트를 UPAGE 주소에 로드
 * 지연 로딩(Lazy Loading) 메커니즘을 사용하여 페이지는 할당되지만 실제로는
 * 첫 번째 페이지 폴트가 발생할 때까지 디스크에서 로드되지 않음
 * 
 * 총 READ_BYTES + ZERO_BYTES 바이트의 가상 메모리가 초기화:
 * - UPAGE에서 시작하는 READ_BYTES 바이트는 파일의 OFS 오프셋에서 읽어야 함
 * - UPAGE + READ_BYTES에서 시작하는 ZERO_BYTES 바이트는 0으로 초기화해야 함
 * 
 * 이 함수로 초기화된 페이지들은 WRITABLE이 true이면 사용자 프로세스가
 * 쓰기 가능해야 하고, false이면 읽기 전용이어야 함.
 * 
 * @param file 세그먼트를 로드할 파일 포인터
 * @param ofs 파일에서 세그먼트가 시작되는 오프셋
 * @param upage 세그먼트가 로드될 가상 주소
 * @param read_bytes 파일에서 읽을 바이트 수
 * @param zero_bytes 0으로 초기화할 바이트 수
 * @param writable 페이지를 쓰기 가능하게 할지 여부 (true: 쓰기 가능, false: 읽기 전용)
 * 
 * @return 성공 시 true, 메모리 할당 오류나 디스크 읽기 오류 발생 시 false
 * 
 * @note 이 함수는 지연 로딩을 사용합니다. 페이지는 페이지 테이블에 할당되지만
 *       실제 디스크에서의 로딩은 첫 번째 페이지 폴트까지 지연
 * @note 함수는 (read_bytes + zero_bytes)가 PGSIZE의 배수라고 가정
 * @note 함수는 upage가 페이지 정렬되어 있다고 가정
 * @note 함수는 ofs가 페이지 정렬되어 있다고 가정
 */
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes,
                         uint32_t zero_bytes, bool writable) {
    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(upage) == 0);
    ASSERT(ofs % PGSIZE == 0);

    while (read_bytes > 0 || zero_bytes > 0) {
        /* Do calculate how to fill this page.
         * We will read PAGE_READ_BYTES bytes from FILE
         * and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct segment_info *aux = malloc(sizeof(struct segment_info));

        if (aux == NULL) {
            return false;
        }

        aux->file = file_reopen(file);
        aux->ofs = ofs;
        aux->page_read_bytes = page_read_bytes;
        aux->page_zero_bytes = page_zero_bytes;

        /* TODO: Set up aux to pass information to the lazy_load_segment. */
        if (!vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, aux)){
            file_close(aux->file);
            free(aux);
            return false;
        }

        /* Advance. */
        ofs += page_read_bytes;
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/**
 * @brief 사용자 프로세스를 위한 초기 스택을 설정
 * 
 * 이 함수는 사용자 공간의 최상단에서 한 페이지 크기만큼 아래에 
 * 스택 페이지를 할당하고 매핑. 
 * 성공적으로 설정되면 인터럽트 프레임의 스택 포인터를 적절히 초기화
 * 
 * @param if_ 초기화할 인터럽트 프레임 포인터
 * @return true 스택 설정이 성공한 경우
 * @return false 스택 설정이 실패한 경우 (메모리 할당 실패 등)
 * 
 * @note 실패 시 할당된 리소스는 자동으로 정리
 * @see vm_alloc_page(), vm_claim_page(), vm_dealloc_page()
 */
/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool setup_stack(struct intr_frame *if_) {
    bool success = false;
    void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

    success = vm_alloc_page(VM_ANON, stack_bottom, true);

    if (success) {
        success = vm_claim_page(stack_bottom);
        if (success) {
            if_->rsp = USER_STACK;
        } else {
            vm_dealloc_page(stack_bottom);
        }
    }

    return success;
}
#endif /* VM */