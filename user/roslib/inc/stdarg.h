/*	$NetBSD: stdarg.h,v 1.12 1995/12/25 23:15:31 mycroft Exp $	*/

#ifndef ROS_INC_STDARG_H
#define	ROS_INC_STDARG_H

typedef void * TRUSTED va_list;

#define	__va_size(type) \
	(((sizeof(type) + sizeof(long) - 1) / sizeof(long)) * sizeof(long))

#define	va_start(ap, last) \
	((ap) = (va_list)&(last) + __va_size(last))

#define	va_arg(ap, type) \
	(*(type *)((ap) += __va_size(type), (ap) - __va_size(type)))

#define	va_end(ap)	((void)0)

#endif	/* !ROS_INC_STDARG_H */
