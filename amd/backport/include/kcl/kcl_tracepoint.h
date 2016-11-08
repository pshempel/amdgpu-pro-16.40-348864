#ifndef AMDGPU_BACKPORT_KCL_TRACEPOINT_H
#define AMDGPU_BACKPORT_KCL_TRACEPOINT_H

#include <linux/tracepoint.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)

#ifdef CONFIG_TRACEPOINTS
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#define DECLARE_TRACE_APPEND(name)					\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return unlikely(__tracepoint_##name.state);		\
	}
#else
#define DECLARE_TRACE_APPEND(name)					\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return static_key_false(&__tracepoint_##name.key);	\
	}
#endif
#else
#define DECLARE_TRACE_APPEND(name)			\
	static inline bool				\
	trace_##name##_enabled(void)			\
	{						\
		return false;                           \
	}
#endif

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, struct, assign, print)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))		\
	DECLARE_TRACE_APPEND(name)

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))	\
	DECLARE_TRACE_APPEND(name)

#endif

#endif /* AMDGPU_BACKPORT_KCL_TRACEPOINT_H */
