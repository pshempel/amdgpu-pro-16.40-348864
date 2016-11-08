#include <kcl/kcl_fence.h>

#define CREATE_TRACE_POINTS
#include <trace/events/fence.h>

#if defined(BUILD_AS_DKMS)
static bool
fence_test_signaled_any(struct fence **fences, uint32_t count, uint32_t *idx)
{
	int i;

	for (i = 0; i < count; ++i) {
		struct fence *fence = fences[i];
		if (test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
			if (idx)
				*idx = i;
			return true;
		}
	}
	return false;
}

struct default_wait_cb {
	struct fence_cb base;
	struct task_struct *task;
};

static void
fence_default_wait_cb(struct fence *fence, struct fence_cb *cb)
{
	struct default_wait_cb *wait =
		container_of(cb, struct default_wait_cb, base);

	wake_up_process(wait->task);
}

signed long
_kcl_fence_wait_any_timeout(struct fence **fences, uint32_t count,
		       bool intr, signed long timeout, uint32_t *idx)
{
	struct default_wait_cb *cb;
	signed long ret = timeout;
	unsigned i;

	if (WARN_ON(!fences || !count || timeout < 0))
		return -EINVAL;

	if (timeout == 0) {
		for (i = 0; i < count; ++i)
			if (fence_is_signaled(fences[i])) {
				if (idx)
					*idx = i;
				return 1;
			}

		return 0;
	}

	cb = kcalloc(count, sizeof(struct default_wait_cb), GFP_KERNEL);
	if (cb == NULL) {
		ret = -ENOMEM;
		goto err_free_cb;
	}

	for (i = 0; i < count; ++i) {
		struct fence *fence = fences[i];

		if (fence->ops->wait != fence_default_wait) {
			ret = -EINVAL;
			goto fence_rm_cb;
		}

		cb[i].task = current;
		if (fence_add_callback(fence, &cb[i].base,
				       fence_default_wait_cb)) {
			/* This fence is already signaled */
			if (idx)
				*idx = i;
			goto fence_rm_cb;
		}
	}

	while (ret > 0) {
		if (intr)
			set_current_state(TASK_INTERRUPTIBLE);
		else
			set_current_state(TASK_UNINTERRUPTIBLE);

		if (fence_test_signaled_any(fences, count, idx))
			break;

		ret = schedule_timeout(ret);

		if (ret > 0 && intr && signal_pending(current))
			ret = -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

fence_rm_cb:
	while (i-- > 0)
		fence_remove_callback(fences[i], &cb[i].base);

err_free_cb:
	kfree(cb);

	return ret;
}

signed long
_kcl_fence_wait_timeout(struct fence *fence, bool intr, signed long timeout)
{
	signed long ret;

	if (WARN_ON(!fence || timeout < 0))
		return -EINVAL;

	if (timeout == 0)
		return fence_is_signaled(fence);

	if (fence->ops->wait != fence_default_wait)
		return -EINVAL;

	trace_fence_wait_start(fence);
	ret = fence->ops->wait(fence, intr, timeout);
	trace_fence_wait_end(fence);
	return ret;
}
#endif /* BUILD_AS_DKMS */
