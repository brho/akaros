#ifndef ROS_INC_ELF_H
#define ROS_INC_ELF_H

#include <process.h>
#include <ros/common.h>

#if defined(LITTLE_ENDIAN)
#  define ELF_MAGIC 0x464C457FU	/* "\x7FELF" in little endian */
#elif defined(BIG_ENDIAN)
#  define ELF_MAGIC 0x7F454C46U	/* "\x7FELF" in big endian */
#else
#  error I know not my endianness!
#endif

typedef struct Elf {
	uint32_t e_magic;	// must equal ELF_MAGIC
	uint8_t e_elf[12];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} elf_t;

typedef struct Proghdr {
	uint32_t p_type;
	uint32_t p_offset;
	uint32_t p_va;
	uint32_t p_pa;
	uint32_t p_filesz;
	uint32_t p_memsz;
	uint32_t p_flags;
	uint32_t p_align;
} proghdr_t;

typedef struct Secthdr {
	uint32_t sh_name;
	uint32_t sh_type;
	uint32_t sh_flags;
	uint32_t sh_addr;
	uint32_t sh_offset;
	uint32_t sh_size;
	uint32_t sh_link;
	uint32_t sh_info;
	uint32_t sh_addralign;
	uint32_t sh_entsize;
} secthdr_t;

typedef struct
{
	long entry;
	long highest_addr;
	long phdr;
	int phnum;
	int dynamic;
	char interp[256];
} elf_info_t;

typedef long elf_aux_t[2];

// Values for Proghdr::p_type
#define ELF_PROG_LOAD		1
#define ELF_PROG_INTERP		3
#define ELF_PROG_PHDR		6

// Flag bits for Proghdr::p_flags
#define ELF_PROG_FLAG_EXEC	1
#define ELF_PROG_FLAG_WRITE	2
#define ELF_PROG_FLAG_READ	4

// Values for Secthdr::sh_type
#define ELF_SHT_NULL		0
#define ELF_SHT_PROGBITS	1
#define ELF_SHT_SYMTAB		2
#define ELF_SHT_STRTAB		3

// Values for Secthdr::sh_name
#define ELF_SHN_UNDEF		0

// Values for auxiliary fields
#define ELF_AUX_PHDR		3
#define ELF_AUX_PHENT		4
#define ELF_AUX_PHNUM		5
#define ELF_AUX_ENTRY		9
#define ELF_AUX_HWCAP		16

// Hardware capabilities (for use with ELF_AUX_HWCAP)
#define ELF_HWCAP_SPARC_FLUSH	1

struct file;
int load_elf(struct proc* p, struct file* f);

#endif /* !ROS_INC_ELF_H */
