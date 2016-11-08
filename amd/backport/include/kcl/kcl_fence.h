#ifndef AMDGPU_BACKPORT_KCL_FENCE_H
#define AMDGPU_BACKPORT_KCL_FENCE_H

#include <linux/version.h>
#include <linux/fence.h>

#if defined(BUILD_AS_DKMS)
extern signed long _kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx);
extern signed long _kcl_fence_wait_timeout(struct fence *fence, bool intr,
				signed long timeout);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static inline bool fence_is_later(struct fence *f1, struct fence *f2)
{
	if (WARN_ON(f1->context != f2->context))
		return false;

	return (int)(f1->seqno - f2->seqno) > 0;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) */

static inline signed long kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#else
	return fence_wait_any_timeout(fences, count, intr, timeout, idx);
#endif
}

static inline signed long kcl_fence_wait_timeout(struct fence *fences, bool intr,
					signed long timeout)
{
#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
	return _kcl_fence_wait_timeout(fences, intr, timeout);
#else
	return fence_wait_timeout(fences, intr, timeout);
#endif
}
#endif /* AMDGPU_BACKPORT_KCL_FENCE_H */
