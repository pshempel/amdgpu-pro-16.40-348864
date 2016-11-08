#ifndef AMDGPU_BACKPORT_KCL_DEVICE_H
#define AMDGPU_BACKPORT_KCL_DEVICE_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}
#endif

#endif /* AMDGPU_BACKPORT_KCL_DEVICE_H */
