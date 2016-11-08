#include <kcl/kcl_ttm.h>

#if defined(BUILD_AS_DKMS)

/* From: drivers/gpu/drm/ttm/ttm_bo.c */
static int _kcl_ttm_bo_del_from_lru(struct ttm_buffer_object *bo)
{
	int put_count = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	struct ttm_bo_device *bdev = bo->bdev;

	if (bdev->driver->lru_removal)
		bdev->driver->lru_removal(bo);
#endif

	if (!list_empty(&bo->swap)) {
		list_del_init(&bo->swap);
		++put_count;
	}
	if (!list_empty(&bo->lru)) {
		list_del_init(&bo->lru);
		++put_count;
	}

	return put_count;
}

static void _kcl_ttm_bo_ref_bug(struct kref *list_kref)
{
	BUG();
}

static void _kcl_ttm_bo_list_ref_sub(struct ttm_buffer_object *bo, int count)
{
	kref_sub(&bo->list_kref, count, _kcl_ttm_bo_ref_bug);
}

static int _kcl_ttm_mem_reg_ioremap(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem,
			void **virtual)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	int ret;
	void *addr;

	*virtual = NULL;
	(void) ttm_mem_io_lock(man, false);
	ret = ttm_mem_io_reserve(bdev, mem);
	ttm_mem_io_unlock(man);
	if (ret || !mem->bus.is_iomem)
		return ret;

	if (mem->bus.addr) {
		addr = mem->bus.addr;
	} else {
		if (mem->placement & TTM_PL_FLAG_WC)
			addr = ioremap_wc(mem->bus.base + mem->bus.offset, mem->bus.size);
		else
			addr = ioremap_nocache(mem->bus.base + mem->bus.offset, mem->bus.size);
		if (!addr) {
			(void) ttm_mem_io_lock(man, false);
			ttm_mem_io_free(bdev, mem);
			ttm_mem_io_unlock(man);
			return -ENOMEM;
		}
	}
	*virtual = addr;
	return 0;
}

static int _kcl_ttm_copy_io_page(void *dst, void *src, unsigned long page)
{
	uint32_t *dstP =
	    (uint32_t *) ((unsigned long)dst + (page << PAGE_SHIFT));
	uint32_t *srcP =
	    (uint32_t *) ((unsigned long)src + (page << PAGE_SHIFT));

	int i;
	for (i = 0; i < PAGE_SIZE / sizeof(uint32_t); ++i)
		iowrite32(ioread32(srcP++), dstP++);
	return 0;
}

static int _kcl_ttm_copy_io_ttm_page(struct ttm_tt *ttm, void *src,
				unsigned long page,
				pgprot_t prot)
{
	struct page *d = ttm->pages[page];
	void *dst;

	if (!d)
		return -ENOMEM;

	src = (void *)((unsigned long)src + (page << PAGE_SHIFT));

#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	dst = kmap_atomic_prot(d, KM_USER0, prot);
#else
	dst = kmap_atomic_prot(d, prot);
#endif
#else
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL))
		dst = vmap(&d, 1, 0, prot);
	else
		dst = kmap(d);
#endif
	if (!dst)
		return -ENOMEM;

	memcpy_fromio(dst, src, PAGE_SIZE);

#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	kunmap_atomic(dst, KM_USER0);
#else
	kunmap_atomic(dst);
#endif
#else
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL))
		vunmap(dst);
	else
		kunmap(d);
#endif

	return 0;
}

static int _kcl_ttm_copy_ttm_io_page(struct ttm_tt *ttm, void *dst,
				unsigned long page,
				pgprot_t prot)
{
	struct page *s = ttm->pages[page];
	void *src;

	if (!s)
		return -ENOMEM;

	dst = (void *)((unsigned long)dst + (page << PAGE_SHIFT));
#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	src = kmap_atomic_prot(s, KM_USER0, prot);
#else
	src = kmap_atomic_prot(s, prot);
#endif
#else
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL))
		src = vmap(&s, 1, 0, prot);
	else
		src = kmap(s);
#endif
	if (!src)
		return -ENOMEM;

	memcpy_toio(dst, src, PAGE_SIZE);

#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 37)
	kunmap_atomic(src, KM_USER0);
#else
	kunmap_atomic(src);
#endif
#else
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL))
		vunmap(src);
	else
		kunmap(s);
#endif

	return 0;
}

static void _kcl_ttm_mem_reg_iounmap(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem,
			 void *virtual)
{
	struct ttm_mem_type_manager *man;

	man = &bdev->man[mem->mem_type];

	if (virtual && mem->bus.addr == NULL)
		iounmap(virtual);
	(void) ttm_mem_io_lock(man, false);
	ttm_mem_io_free(bdev, mem);
	ttm_mem_io_unlock(man);
}

static void _kcl_ttm_tt_clear_mapping(struct ttm_tt *ttm)
{
	pgoff_t i;
	struct page **page = ttm->pages;

	if (ttm->page_flags & TTM_PAGE_FLAG_SG)
		return;

	for (i = 0; i < ttm->num_pages; ++i) {
		(*page)->mapping = NULL;
		(*page++)->index = 0;
	}
}

static void _kcl_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	if (ttm->state == tt_unpopulated)
		return;

	_kcl_ttm_tt_clear_mapping(ttm);
	ttm->bdev->driver->ttm_tt_unpopulate(ttm);
}

static void _kcl_ttm_tt_destroy(struct ttm_tt *ttm)
{
	int ret;

	if (ttm == NULL)
		return;

	if (ttm->state == tt_bound) {
		ret = ttm->func->unbind(ttm);
		BUG_ON(ret);
		ttm->state = tt_unbound;
	}

	if (ttm->state == tt_unbound)
		_kcl_ttm_tt_unpopulate(ttm);

	if (!(ttm->page_flags & TTM_PAGE_FLAG_PERSISTENT_SWAP) &&
	    ttm->swap_storage)
		fput(ttm->swap_storage);

	ttm->swap_storage = NULL;
	ttm->func->destroy(ttm);
}


void _kcl_ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo)
{
	int put_count = 0;

	lockdep_assert_held(&bo->resv->lock.base);

	put_count = _kcl_ttm_bo_del_from_lru(bo);
	_kcl_ttm_bo_list_ref_sub(bo, put_count);
	ttm_bo_add_to_lru(bo);
}

int _kcl_ttm_bo_wait(struct ttm_buffer_object *bo,
                bool interruptible, bool no_wait)
{
        struct reservation_object_list *fobj;
        struct reservation_object *resv;
        struct fence *excl;
        long timeout = 15 * HZ;
        int i;

        resv = bo->resv;
        fobj = reservation_object_get_list(resv);
        excl = reservation_object_get_excl(resv);
        if (excl) {
                if (!fence_is_signaled(excl)) {
                        if (no_wait)
                                return -EBUSY;

                        timeout = fence_wait_timeout(excl,
                                                     interruptible, timeout);
                }
        }

        for (i = 0; fobj && timeout > 0 && i < fobj->shared_count; ++i) {
                struct fence *fence;
                fence = rcu_dereference_protected(fobj->shared[i],
                                                reservation_object_held(resv));

                if (!fence_is_signaled(fence)) {
                        if (no_wait)
                                return -EBUSY;

                        timeout = fence_wait_timeout(fence,
                                                     interruptible, timeout);
                }
        }

        if (timeout < 0)
                return timeout;

        if (timeout == 0)
                return -EBUSY;

        reservation_object_add_excl_fence(resv, NULL);
        return 0;
}

int _kcl_ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
                       bool evict, bool interruptible,
                       bool no_wait_gpu,
                       struct ttm_mem_reg *new_mem)
{
        struct ttm_bo_device *bdev = bo->bdev;
        struct ttm_mem_type_manager *man = &bdev->man[new_mem->mem_type];
        struct ttm_tt *ttm = bo->ttm;
        struct ttm_mem_reg *old_mem = &bo->mem;
        struct ttm_mem_reg old_copy = *old_mem;
        void *old_iomap;
        void *new_iomap;
        int ret;
        unsigned long i;
        unsigned long page;
        unsigned long add = 0;
        int dir;

        ret = _kcl_ttm_bo_wait(bo, interruptible, no_wait_gpu);
        if (ret)
                return ret;

        ret = _kcl_ttm_mem_reg_ioremap(bdev, old_mem, &old_iomap);
        if (ret)
                return ret;
        ret = _kcl_ttm_mem_reg_ioremap(bdev, new_mem, &new_iomap);
        if (ret)
                goto out;

        /*
         * Single TTM move. NOP.
         */
        if (old_iomap == NULL && new_iomap == NULL)
                goto out2;

        /*
         * Don't move nonexistent data. Clear destination instead.
         */
        if (old_iomap == NULL &&
            (ttm == NULL || (ttm->state == tt_unpopulated &&
                             !(ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)))) {
                memset_io(new_iomap, 0, new_mem->num_pages*PAGE_SIZE);
                goto out2;
        }

        /*
         * TTM might be null for moves within the same region.
         */
        if (ttm && ttm->state == tt_unpopulated) {
                ret = ttm->bdev->driver->ttm_tt_populate(ttm);
                if (ret)
                        goto out1;
        }

        add = 0;
        dir = 1;

        if ((old_mem->mem_type == new_mem->mem_type) &&
            (new_mem->start < old_mem->start + old_mem->size)) {
                dir = -1;
                add = new_mem->num_pages - 1;
        }

        for (i = 0; i < new_mem->num_pages; ++i) {
                page = i * dir + add;
                if (old_iomap == NULL) {
                        pgprot_t prot = ttm_io_prot(old_mem->placement,
                                                    PAGE_KERNEL);
                        ret = _kcl_ttm_copy_ttm_io_page(ttm, new_iomap, page,
                                                   prot);
                } else if (new_iomap == NULL) {
                        pgprot_t prot = ttm_io_prot(new_mem->placement,
                                                    PAGE_KERNEL);
                        ret = _kcl_ttm_copy_io_ttm_page(ttm, old_iomap, page,
                                                   prot);
                } else
                        ret = _kcl_ttm_copy_io_page(new_iomap, old_iomap, page);
                if (ret)
                        goto out1;
        }
        mb();
out2:
        old_copy = *old_mem;
        *old_mem = *new_mem;
        new_mem->mm_node = NULL;

        if (man->flags & TTM_MEMTYPE_FLAG_FIXED) {
                _kcl_ttm_tt_destroy(ttm);
                bo->ttm = NULL;
        }

out1:
        _kcl_ttm_mem_reg_iounmap(bdev, old_mem, new_iomap);
out:
        _kcl_ttm_mem_reg_iounmap(bdev, &old_copy, old_iomap);

        /*
         * On error, keep the mm node!
         */
        if (!ret)
                ttm_bo_mem_put(bo, &old_copy);
        return ret;
}


#endif

/* From: drivers/gpu/drm/ttm/ttm_bo.c */
int _kcl_ttm_bo_init(struct ttm_bo_device *bdev,
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
	int ret = 0;
	unsigned long num_pages;
	struct ttm_mem_global *mem_glob = bdev->glob->mem_glob;
	bool locked;

	ret = ttm_mem_global_alloc(mem_glob, acc_size, false, false);
	if (ret) {
		pr_err("Out of kernel memory\n");
		if (destroy)
			(*destroy)(bo);
		else
			kfree(bo);
		return -ENOMEM;
	}

	num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (num_pages == 0) {
		pr_err("Illegal buffer object size\n");
		if (destroy)
			(*destroy)(bo);
		else
			kfree(bo);
		ttm_mem_global_free(mem_glob, acc_size);
		return -EINVAL;
	}
	bo->destroy = destroy;

	kref_init(&bo->kref);
	kref_init(&bo->list_kref);
	atomic_set(&bo->cpu_writers, 0);
	INIT_LIST_HEAD(&bo->lru);
	INIT_LIST_HEAD(&bo->ddestroy);
	INIT_LIST_HEAD(&bo->swap);
	INIT_LIST_HEAD(&bo->io_reserve_lru);
	mutex_init(&bo->wu_mutex);
	bo->bdev = bdev;
	bo->glob = bdev->glob;
	bo->type = type;
	bo->num_pages = num_pages;
	bo->mem.mm_node = NULL;
	bo->mem.size = num_pages << PAGE_SHIFT;
	bo->mem.mem_type = TTM_PL_SYSTEM;
	bo->mem.num_pages = bo->num_pages;
	bo->mem.page_alignment = page_alignment;
	bo->mem.bus.io_reserved_vm = false;
	bo->mem.bus.io_reserved_count = 0;
	bo->mem.placement = (TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED);
	bo->persistent_swap_storage = persistent_swap_storage;
	bo->acc_size = acc_size;
	bo->sg = sg;
	if (resv) {
		bo->resv = resv;
		lockdep_assert_held(&bo->resv->lock.base);
	} else {
		bo->resv = &bo->ttm_resv;
		reservation_object_init(&bo->ttm_resv);
	}
	atomic_inc(&bo->glob->bo_count);
	drm_vma_node_reset(&bo->vma_node);

	/*
	 * For ttm_bo_type_device buffers, allocate
	 * address space from the device.
	 */
	if (bo->type == ttm_bo_type_device ||
	    bo->type == ttm_bo_type_sg)
		ret = drm_vma_offset_add(&bdev->vma_manager, &bo->vma_node,
					 bo->mem.num_pages);

	/* passed reservation objects should already be locked,
	 * since otherwise lockdep will be angered in radeon.
	 */
	if (!resv) {
		locked = ww_mutex_trylock(&bo->resv->lock);
		WARN_ON(!locked);
	}

	if (likely(!ret))
		ret = ttm_bo_validate(bo, placement, interruptible, false);

	if (!resv) {
		ttm_bo_unreserve(bo);

	} else if (!(bo->mem.placement & TTM_PL_FLAG_NO_EVICT)) {
		spin_lock(&bo->glob->lru_lock);
		ttm_bo_add_to_lru(bo);
		spin_unlock(&bo->glob->lru_lock);
	}

	if (unlikely(ret))
		ttm_bo_unref(&bo);

	return ret;
}

size_t _kcl_ttm_bo_acc_size(struct ttm_bo_device *bdev,
		       unsigned long bo_size,
		       unsigned struct_size)
{
	unsigned npages = (PAGE_ALIGN(bo_size)) >> PAGE_SHIFT;
	size_t size = 0;

	size += ttm_round_pot(struct_size);
	size += ttm_round_pot(npages * sizeof(void *));
	size += ttm_round_pot(sizeof(struct ttm_tt));
	return size;
}

size_t _kcl_ttm_bo_dma_acc_size(struct ttm_bo_device *bdev,
			   unsigned long bo_size,
			   unsigned struct_size)
{
	unsigned npages = (PAGE_ALIGN(bo_size)) >> PAGE_SHIFT;
	size_t size = 0;

	size += ttm_round_pot(struct_size);
	size += ttm_round_pot(npages * (2*sizeof(void *) +
		sizeof(dma_addr_t)));
	size += ttm_round_pot(sizeof(struct ttm_dma_tt));
	return size;
}
