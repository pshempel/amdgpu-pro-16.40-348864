#ifndef AMDGPU_BACKPORT_KCL_MN_H
#define AMDGPU_BACKPORT_KCL_MN_H

#include <linux/mmu_notifier.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
extern void mmu_notifier_unregister_no_release(struct mmu_notifier *mn,
					       struct mm_struct *mm);
#endif

#endif /* AMDGPU_BACKPORT_KCL_MN_H */
