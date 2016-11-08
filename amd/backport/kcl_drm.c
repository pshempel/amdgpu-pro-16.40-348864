#include <kcl/kcl_drm.h>

#if defined(BUILD_AS_DKMS)
int drm_pcie_get_max_link_width(struct drm_device *dev, u32 *mlw)
{
	struct pci_dev *root;
	u32 lnkcap;

	*mlw = 0;
	if (!dev->pdev)
		return -EINVAL;

	root = dev->pdev->bus->self;
	if (!root)
		return -EINVAL;

	pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);

	*mlw = (lnkcap & PCI_EXP_LNKCAP_MLW) >> 4;

	DRM_INFO("probing mlw for device %x:%x = %x\n", root->vendor, root->device, lnkcap);
	return 0;
}
#endif /* BUILD_AS_DKMS */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0) && !defined(OS_NAME_RHEL_6)

/**
 * drm_fb_helper_cfb_fillrect - wrapper around cfb_fillrect
 * @info: fbdev registered by the helper
 * @rect: info about rectangle to fill
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void drm_fb_helper_cfb_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
}

/**
 * drm_fb_helper_cfb_copyarea - wrapper around cfb_copyarea
 * @info: fbdev registered by the helper
 * @area: info about area to copy
 *
 * A wrapper around cfb_copyarea implemented by fbdev core
 */
void drm_fb_helper_cfb_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
}

/**
 * drm_fb_helper_cfb_imageblit - wrapper around cfb_imageblit
 * @info: fbdev registered by the helper
 * @image: info about image to blit
 *
 * A wrapper around cfb_imageblit implemented by fbdev core
 */
void drm_fb_helper_cfb_imageblit(struct fb_info *info,
				 const struct fb_image *image)
{
	cfb_imageblit(info, image);
}

/**
 * drm_fb_helper_alloc_fbi - allocate fb_info and some of its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to alloc fb_info and the members cmap and apertures. Called
 * by the driver within the fb_probe fb_helper callback function.
 *
 * RETURNS:
 * fb_info pointer if things went okay, pointer containing error code
 * otherwise
 */
struct fb_info *drm_fb_helper_alloc_fbi(struct drm_fb_helper *fb_helper)
{
	struct device *dev = fb_helper->dev->dev;
	struct fb_info *info;
	int ret;

	info = framebuffer_alloc(0, dev);
	if (!info)
		return ERR_PTR(-ENOMEM);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		goto err_release;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto err_free_cmap;
	}
#endif

	fb_helper->fbdev = info;

	return info;

err_free_cmap:
	fb_dealloc_cmap(&info->cmap);
err_release:
	framebuffer_release(info);
	return ERR_PTR(ret);
}

/**
 * drm_fb_helper_unregister_fbi - unregister fb_info framebuffer device
 * @fb_helper: driver-allocated fbdev helper
 *
 * A wrapper around unregister_framebuffer, to release the fb_info
 * framebuffer device
 */
void drm_fb_helper_unregister_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper && fb_helper->fbdev)
		unregister_framebuffer(fb_helper->fbdev);
}

/**
 * drm_fb_helper_release_fbi - dealloc fb_info and its members
 * @fb_helper: driver-allocated fbdev helper
 *
 * A helper to free memory taken by fb_info and the members cmap and
 * apertures
 */
void drm_fb_helper_release_fbi(struct drm_fb_helper *fb_helper)
{
	if (fb_helper) {
		struct fb_info *info = fb_helper->fbdev;

		if (info) {
			if (info->cmap.len)
				fb_dealloc_cmap(&info->cmap);
			framebuffer_release(info);
		}

		fb_helper->fbdev = NULL;
	}
}

/**
 * drm_fb_helper_set_suspend - wrapper around fb_set_suspend
 * @fb_helper: driver-allocated fbdev helper
 * @state: desired state, zero to resume, non-zero to suspend
 *
 * A wrapper around fb_set_suspend implemented by fbdev core
 */
void drm_fb_helper_set_suspend(struct drm_fb_helper *fb_helper, int state)
{
	if (fb_helper && fb_helper->fbdev)
		fb_set_suspend(fb_helper->fbdev, state);
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0) && !defined(OS_NAME_RHEL_6)
static inline bool
drm_atomic_crtc_needs_modeset(struct drm_crtc_state *state)
{
    return state->mode_changed || state->active_changed ;
}

static inline struct drm_plane_state *
drm_atomic_get_existing_plane_state(struct drm_atomic_state *state,
                    struct drm_plane *plane)
{
    return state->plane_states[drm_plane_index(plane)];
}

void
drm_atomic_helper_update_legacy_modeset_state(struct drm_device *dev,
					      struct drm_atomic_state *old_state)
{
	struct drm_connector *connector;
	struct drm_connector_state *old_conn_state;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state;
	int i;

	/* clear out existing links and update dpms */
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (connector->encoder) {
			WARN_ON(!connector->encoder->crtc);

			connector->encoder->crtc = NULL;
			connector->encoder = NULL;
		}

		crtc = connector->state->crtc;
		if ((!crtc && old_conn_state->crtc) ||
		    (crtc && drm_atomic_crtc_needs_modeset(crtc->state))) {
			struct drm_property *dpms_prop =
				dev->mode_config.dpms_property;
			int mode = DRM_MODE_DPMS_OFF;

			if (crtc && crtc->state->active)
				mode = DRM_MODE_DPMS_ON;

			connector->dpms = mode;
			drm_object_property_set_value(&connector->base,
						      dpms_prop, mode);
		}
	}

	/* set new links */
	for_each_connector_in_state(old_state, connector, old_conn_state, i) {
		if (!connector->state->crtc)
			continue;

		if (WARN_ON(!connector->state->best_encoder))
			continue;

		connector->encoder = connector->state->best_encoder;
		connector->encoder->crtc = connector->state->crtc;
	}

	/* set legacy state in the crtc structure */
	for_each_crtc_in_state(old_state, crtc, old_crtc_state, i) {
		struct drm_plane *primary = crtc->primary;

		crtc->mode = crtc->state->mode;
		crtc->enabled = crtc->state->enable;

		if (drm_atomic_get_existing_plane_state(old_state, primary) &&
		    primary->state->crtc == crtc) {
			crtc->x = primary->state->src_x >> 16;
			crtc->y = primary->state->src_y >> 16;
		}

		if (crtc->state->enable)
			drm_calc_timestamping_constants(crtc,
							&crtc->state->adjusted_mode);
	}
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
int drm_modeset_lock_all_ctx(struct drm_device *dev,
			     struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	int ret;

	ret = drm_modeset_lock(&dev->mode_config.connection_mutex, ctx);
	if (ret)
		return ret;

	drm_for_each_crtc(crtc, dev) {
		ret = drm_modeset_lock(&crtc->mutex, ctx);
		if (ret)
			return ret;
	}

	drm_for_each_plane(plane, dev) {
		ret = drm_modeset_lock(&plane->mutex, ctx);
		if (ret)
			return ret;
	}

	return 0;
}

int drm_atomic_helper_disable_all(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	int err;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return -ENOMEM;

	state->acquire_ctx = ctx;

	drm_for_each_connector(conn, dev) {
		struct drm_crtc *crtc = conn->state->crtc;
		struct drm_crtc_state *crtc_state;

		if (!crtc || conn->dpms != DRM_MODE_DPMS_ON)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}

		crtc_state->active = false;
	}

	err = drm_atomic_commit(state);

free:
	if (err < 0)
		drm_atomic_state_free(state);

	return err;
}

struct drm_atomic_state *
drm_atomic_helper_duplicate_state(struct drm_device *dev,
				  struct drm_modeset_acquire_ctx *ctx)
{
	struct drm_atomic_state *state;
	struct drm_connector *conn;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	int err = 0;

	state = drm_atomic_state_alloc(dev);
	if (!state)
		return ERR_PTR(-ENOMEM);

	state->acquire_ctx = ctx;

	drm_for_each_crtc(crtc, dev) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state)) {
			err = PTR_ERR(crtc_state);
			goto free;
		}
	}

	drm_for_each_plane(plane, dev) {
		struct drm_plane_state *plane_state;

		plane_state = drm_atomic_get_plane_state(state, plane);
		if (IS_ERR(plane_state)) {
			err = PTR_ERR(plane_state);
			goto free;
		}
	}

	drm_for_each_connector(conn, dev) {
		struct drm_connector_state *conn_state;

		conn_state = drm_atomic_get_connector_state(state, conn);
		if (IS_ERR(conn_state)) {
			err = PTR_ERR(conn_state);
			goto free;
		}
	}

	/* clear the acquire context so that it isn't accidentally reused */
	state->acquire_ctx = NULL;

free:
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
	}

	return state;
}

struct drm_atomic_state *drm_atomic_helper_suspend(struct drm_device *dev)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	int err;

	drm_modeset_acquire_init(&ctx, 0);

retry:
	err = drm_modeset_lock_all_ctx(dev, &ctx);
	if (err < 0) {
		state = ERR_PTR(err);
		goto unlock;
	}

	state = drm_atomic_helper_duplicate_state(dev, &ctx);
	if (IS_ERR(state))
		goto unlock;

	err = drm_atomic_helper_disable_all(dev, &ctx);
	if (err < 0) {
		drm_atomic_state_free(state);
		state = ERR_PTR(err);
		goto unlock;
	}

unlock:
	if (PTR_ERR(state) == -EDEADLK) {
		drm_modeset_backoff(&ctx);
		goto retry;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	return state;
}

int drm_atomic_helper_resume(struct drm_device *dev,
			     struct drm_atomic_state *state)
{
	struct drm_mode_config *config = &dev->mode_config;
	int err;

	drm_mode_config_reset(dev);
	drm_modeset_lock_all(dev);
	state->acquire_ctx = config->acquire_ctx;
	err = drm_atomic_commit(state);
	drm_modeset_unlock_all(dev);

	return err;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0) */
