#ifndef AMDGPU_BACKPORT_KCL_TTM_H
#define AMDGPU_BACKPORT_KCL_TTM_H

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo_api.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_memory.h>
/*
#if defined(BUILD_AS_DKMS)
struct _kcl_ttm_mem_reg {
        void *mm_node;
        unsigned long start;
        unsigned long size;
        unsigned long num_pages;
        uint32_t page_alignment;
        uint32_t mem_type;
        uint32_t placement;
        struct ttm_bus_placement bus;
};
#endif
*/
#if defined(BUILD_AS_DKMS)
extern void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo);
#endif

#if defined(BUILD_AS_DKMS)
extern int _kcl_ttm_bo_init(struct ttm_bo_device *bdev,
		struct ttm_buffer_object *bo,
		unsigned long size,
		enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment,
		bool interrubtible,
		struct file *persistent_swap_storage,
		size_t acc_size,
		struct sg_table *sg,
		struct reservation_object *resv,
		void (*destroy) (struct ttm_buffer_object *));

extern size_t _kcl_ttm_bo_acc_size(struct ttm_bo_device *bdev,
		       unsigned long bo_size,
		       unsigned struct_size);

extern size_t _kcl_ttm_bo_dma_acc_size(struct ttm_bo_device *bdev,
			   unsigned long bo_size,
			   unsigned struct_size);

extern void _kcl_ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo);
extern int _kcl_ttm_bo_wait(struct ttm_buffer_object *bo, bool interruptible, bool no_wait);
extern int _kcl_ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
                       bool evict, bool interruptible,
                       bool no_wait_gpu,
                       struct ttm_mem_reg *new_mem);


#endif

static inline int kcl_ttm_bo_init(struct ttm_bo_device *bdev,
		struct ttm_buffer_object *bo,
		unsigned long size,
		enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment,
		bool interruptible,
		struct file *persistent_swap_storage,
		size_t acc_size,
		struct sg_table *sg,
		struct reservation_object *resv,
		void (*destroy) (struct ttm_buffer_object *))
{
#if defined(BUILD_AS_DKMS)
	return _kcl_ttm_bo_init(bdev, bo, size, type, placement, page_alignment,
			interruptible, persistent_swap_storage, acc_size, sg,
			resv, destroy);
#else
	return ttm_bo_init(bdev, bo, size, type, placement, page_alignment,
			interruptible, persistent_swap_storage, acc_size, sg,
			resv, destroy);
#endif
}

static inline size_t kcl_ttm_bo_acc_size(struct ttm_bo_device *bdev,
				   unsigned long bo_size,
				   unsigned struct_size)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_ttm_bo_acc_size(bdev, bo_size, struct_size);
#else
	return ttm_bo_acc_size(bdev, bo_size, struct_size);
#endif
}

static inline size_t kcl_ttm_bo_dma_acc_size(struct ttm_bo_device *bdev,
				   unsigned long bo_size,
				   unsigned struct_size)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_ttm_bo_dma_acc_size(bdev, bo_size, struct_size);
#else
	return ttm_bo_dma_acc_size(bdev, bo_size, struct_size);
#endif
}

static inline int kcl_ttm_bo_reserve(struct ttm_buffer_object *bo,
				 bool interruptible, bool no_wait,
				 struct ww_acquire_ctx *ticket)
{
#if defined(BUILD_AS_DKMS)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	return ttm_bo_reserve(bo, interruptible, no_wait, ticket);
#else
	return ttm_bo_reserve(bo, interruptible, no_wait, false, ticket);
#endif
#else
	return ttm_bo_reserve(bo, interruptible, no_wait, ticket);
#endif
}

static inline void kcl_ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo)
{
#if defined(BUILD_AS_DKMS)
	_kcl_ttm_bo_move_to_lru_tail(bo);
#else
	ttm_bo_move_to_lru_tail(bo);
#endif
}

#endif /* AMDGPU_BACKPORT_KCL_TTM_H */
