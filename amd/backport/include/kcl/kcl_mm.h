#ifndef AMDGPU_BACKPORT_KCL_MM_H
#define AMDGPU_BACKPORT_KCL_MM_H

#include <linux/mm.h>

static inline int kcl_get_user_pages(struct task_struct *tsk, struct mm_struct *mm,
                                     unsigned long start, unsigned long nr_pages,
                                     int write, int force, struct page **pages,
                                     struct vm_area_struct **vmas)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
        return get_user_pages(start, nr_pages, write, force, pages, vmas);
#else
        return get_user_pages(tsk, mm, start, nr_pages,
                              write, force, pages, vmas);
#endif
}

#endif /* AMDGPU_BACKPORT_KCL_MM_H */
