#ifndef AMDGPU_BACKPORT_KCL_DRM_H
#define AMDGPU_BACKPORT_KCL_DRM_H

#include <linux/version.h>
#include <drm/drmP.h>

#if defined(BUILD_AS_DKMS)
extern int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw);
#endif /* BUILD_AS_DKMS */

static inline int
kcl_drm_calc_vbltimestamp_from_scanoutpos(struct drm_device *dev,
					  unsigned int pipe,
					  int *max_error,
					  struct timeval *vblank_time,
					  unsigned flags,
					  const struct drm_crtc *refcrtc,
					  const struct drm_display_mode *mode)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) && !defined(OS_NAME_RHEL_6)
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, refcrtc, mode);
#else
	return drm_calc_vbltimestamp_from_scanoutpos(dev, pipe, max_error, vblank_time,
						     flags, mode);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
#include <drm/drm_fb_helper.h>

void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image);

struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper);
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper);
void drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper);

void drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper, int state);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <linux/fence.h>
void drm_atomic_helper_update_legacy_modeset_state(
		struct drm_device *dev,
		struct drm_atomic_state *old_state);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) && !defined(OS_NAME_RHEL_6)
#define DRM_UT_VBL		0x20
#define DRM_DEBUG_VBL(fmt, args...)					\
	do {								\
		if (unlikely(drm_debug & DRM_UT_VBL))			\
			drm_ut_debug_printk(__func__, fmt, ##args);	\
	} while (0)
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) */

#if (defined(OS_NAME_UBUNTU_1404) && LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0) \
		&& LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0) && (UBUNTU_BUILD_NUM < 36)) \
	|| defined(OS_NAME_STEAMOS)
static inline bool drm_arch_can_wc_memory(void)
{
#if defined(CONFIG_PPC) && !defined(CONFIG_NOT_COHERENT_CACHE)
	return false;
#else
	return true;
#endif
}
#endif

static inline int kcl_drm_encoder_init(struct drm_device *dev,
		      struct drm_encoder *encoder,
		      const struct drm_encoder_funcs *funcs,
		      int encoder_type, const char *name, ...)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	return drm_encoder_init(dev, encoder, funcs,
			 encoder_type, name);
#else
	return drm_encoder_init(dev, encoder, funcs,
			 encoder_type);
#endif
}


static inline int kcl_drm_crtc_init_with_planes(struct drm_device *dev, struct drm_crtc *crtc,
			      struct drm_plane *primary,
			      struct drm_plane *cursor,
			      const struct drm_crtc_funcs *funcs,
			      const char *name, ...)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
		return drm_crtc_init_with_planes(dev, crtc, primary,
				 cursor, funcs, name);
#else
		return drm_crtc_init_with_planes(dev, crtc, primary,
				 cursor, funcs);
#endif
}

static inline int kcl_drm_universal_plane_init(struct drm_device *dev, struct drm_plane *plane,
			     unsigned long possible_crtcs,
			     const struct drm_plane_funcs *funcs,
			     const uint32_t *formats, unsigned int format_count,
			     enum drm_plane_type type,
			     const char *name, ...)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, type, name);
#else
		return drm_universal_plane_init(dev, plane, possible_crtcs, funcs,
				 formats, format_count, type);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0) && !defined(OS_NAME_RHEL_6)
#define drm_for_each_plane(plane, dev) \
	list_for_each_entry(plane, &(dev)->mode_config.plane_list, head)

#define drm_for_each_crtc(crtc, dev) \
	list_for_each_entry(crtc, &(dev)->mode_config.crtc_list, head)

#define drm_for_each_connector(connector, dev) \
	for (assert_drm_connector_list_read_locked(&(dev)->mode_config),	\
	     connector = list_first_entry(&(dev)->mode_config.connector_list,	\
					  struct drm_connector, head);		\
	     &connector->head != (&(dev)->mode_config.connector_list);		\
	     connector = list_next_entry(connector, head))

static inline void
assert_drm_connector_list_read_locked(struct drm_mode_config *mode_config)
{
        /*
         * The connector hotadd/remove code currently grabs both locks when
         * updating lists. Hence readers need only hold either of them to be
         * safe and the check amounts to
         *
         * WARN_ON(not_holding(A) && not_holding(B)).
         */
        WARN_ON(!mutex_is_locked(&mode_config->mutex) &&
                !drm_modeset_is_locked(&mode_config->connection_mutex));
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
#include <drm/drm_atomic.h>
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx);
int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx);
struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev);
int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state);

#endif

#endif /* AMDGPU_BACKPORT_KCL_DRM_H */
