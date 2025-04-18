#ifndef _LINUX_HWTRACE_H_
#define _LINUX_HWTRACE_H_

#define HWTRACE_MAGIC	0xb0cad0

struct hwtrace_private {
	unsigned long magic;
	unsigned long (*read)(unsigned long offset);
	void (*write)(unsigned long offset, unsigned long value);
};

#endif
