#ifndef AMDGPU_BACKPORT_KCL_VGA_SWITCHEROO_H
#define AMDGPU_BACKPORT_KCL_VGA_SWITCHEROO_H

#include <linux/vga_switcheroo.h>

/**
 * arg change in mainline kernel 3.12
 * but only affect RHEL6 without backport
 */
static inline int kcl_vga_switcheroo_register_client(struct pci_dev *dev,
						     const struct vga_switcheroo_client_ops *ops,
						     bool driver_power_control)
{
#if defined(OS_NAME_RHEL_6)
	return vga_switcheroo_register_client(dev, ops);
#else
	return vga_switcheroo_register_client(dev, ops, driver_power_control);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0)
static inline int kcl_vga_switcheroo_register_handler(struct vga_switcheroo_handler *handler,
						      int handler_flags)
#else
static inline int kcl_vga_switcheroo_register_handler(const struct vga_switcheroo_handler *handler,
						      int handler_flags)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
	return vga_switcheroo_register_handler(handler);
#else
	/* the value fo handler_flags is enumerated in vga_switcheroo_handler_flags_t
	 * in vga_switheroo.h */
	return vga_switcheroo_register_handler(handler, handler_flags);
#endif
}

#endif /* AMDGPU_BACKPORT_KCL_VGA_SWITCHEROO_H */
