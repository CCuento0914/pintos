#include "userprog/syscall.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

static void exit_with_status (int status);
static bool is_valid_uaddr (const void *uaddr);
static void validate_user_ptr (const void *uaddr);
static int get_user_byte (const uint8_t *uaddr);
static uint32_t copy_in_u32 (const void *uaddr);
static void validate_user_range (const void *uaddr, size_t size);
static void validate_user_string (const char *str);

static int syscall_write (int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Ends the current user process with STATUS. */
static void
exit_with_status (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  thread_exit ();
}

/* Basic user-address validity check. */
static bool
is_valid_uaddr (const void *uaddr)
{
  struct thread *cur = thread_current ();

  return uaddr != NULL
         && is_user_vaddr (uaddr)
         && cur->pagedir != NULL
         && pagedir_get_page (cur->pagedir, uaddr) != NULL;
}

/* Validate a single user pointer. */
static void
validate_user_ptr (const void *uaddr)
{
  if (!is_valid_uaddr (uaddr))
    exit_with_status (-1);
}

/* Safely read one byte from user memory. */
static int
get_user_byte (const uint8_t *uaddr)
{
  validate_user_ptr (uaddr);
  return *uaddr;
}

/* Safely read a 32-bit word from user memory. */
static uint32_t
copy_in_u32 (const void *uaddr)
{
  uint32_t value = 0;
  const uint8_t *src = uaddr;
  int i;

  for (i = 0; i < 4; i++)
    {
      int b = get_user_byte (src + i);
      value |= ((uint32_t) b) << (8 * i);
    }

  return value;
}

/* Validate a whole user buffer range [uaddr, uaddr + size). */
static void
validate_user_range (const void *uaddr, size_t size)
{
  const uint8_t *start = uaddr;
  size_t i;

  if (size == 0)
    return;

  validate_user_ptr (start);

  for (i = 0; i < size; i++)
    validate_user_ptr (start + i);
}

/* Validate a user C-string until '\0'. */
static void
validate_user_string (const char *str)
{
  size_t i = 0;

  validate_user_ptr (str);

  while (true)
    {
      int ch = get_user_byte ((const uint8_t *) str + i);
      if (ch == '\0')
        break;
      i++;
    }
}

/* Provided-style write syscall: only stdout is supported. */
static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  if (fd != 1)
    return -1;

  putbuf (buffer, size);
  return size;
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t syscall_no;

  /* Read syscall number from user stack. */
  validate_user_range (f->esp, sizeof (uint32_t));
  syscall_no = copy_in_u32 (f->esp);

  switch (syscall_no)
    {
      case SYS_HALT:
        shutdown_power_off ();
        break;

      case SYS_EXIT:
        {
          int status;

          validate_user_range ((const uint8_t *) f->esp + 4,
                               sizeof (uint32_t));
          status = (int) copy_in_u32 ((const uint8_t *) f->esp + 4);
          exit_with_status (status);
          break;
        }

      case SYS_WRITE:
        {
          int fd;
          const void *buffer;
          unsigned size;

          validate_user_range ((const uint8_t *) f->esp + 4,
                               3 * sizeof (uint32_t));

          fd = (int) copy_in_u32 ((const uint8_t *) f->esp + 4);
          buffer = (const void *) copy_in_u32 ((const uint8_t *) f->esp + 8);
          size = (unsigned) copy_in_u32 ((const uint8_t *) f->esp + 12);

          validate_user_range (buffer, size);
          f->eax = syscall_write (fd, buffer, size);
          break;
        }

      default:
        exit_with_status (-1);
        break;
    }
}