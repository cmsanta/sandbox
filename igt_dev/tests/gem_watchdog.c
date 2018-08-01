/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "igt.h"
#include "igt_sysfs.h"

#include <fcntl.h>


#define HANG_TIMEOUT ((uint64_t)3 * (uint64_t)NSEC_PER_SEC)

#define MI_STORE_REGISTER_MEM (0x24 << 23)
#define MI_STORE_DATA_IMM (0x20 << 23)
#define TIMESTAMP_REGISTER_LOW 0x02358 //TODO: this is the render register. Need to extend to all engines?

#define WATCHDOG_THRESHOLD (100) //ms

#define MAX_ENGINES 5

static bool is_guc_submission(uint32_t fd)
{
    int dir;

	dir = igt_sysfs_open_parameters(fd);
	igt_assert_lt(0, dir);

	return igt_sysfs_get_boolean(dir, "enable_guc_submission");
}

static void clear_error_state(int fd)
{
	int dir;

	dir = igt_sysfs_open(fd, NULL);
	if (dir < 0)
		return;

	/* Any write to the error state clears it */
	igt_sysfs_set(dir, "error", "");
	close(dir);
}

static bool check_error_state(int fd)
{
	char *error, *str;
	bool found = false;
	int dir;

	dir = igt_sysfs_open(fd, NULL);

	error = igt_sysfs_get(dir, "error");
	igt_sysfs_set(dir, "error", "Begone!");

	igt_assert(error);
	igt_debug("Error: %s\n", error);

	if (str = strstr(error, "GPU HANG")) {
		igt_debug("Found error state! GPU hang triggered! %s\n", str);
		found = true;
	}

	close(dir);

	return found;
}
static void get_watchdog_count(uint32_t fd, const struct intel_execution_engine *engine, uint32_t *count)
{
    int dfs_fd;
    FILE *rd_fd;

    char *line = NULL;
    size_t line_size = 0;

    const char *reset_debugfs = "i915_reset_info";

    dfs_fd = igt_debugfs_open(fd, reset_debugfs, O_RDONLY);
    rd_fd = fdopen(dfs_fd, "r");

    while (getline(&line, &line_size, rd_fd) > 0)
    {
        if (is_guc_submission(fd))
        {
            if (sscanf(line, "watchdog / media reset = %d", count))
                goto found;
        }
        else
        {
            if (!engine)
                igt_assert_f(0, "Invalid Engine pointer.");

            if (strcmp(engine->name, "render") == 0)
                if (sscanf(line, "rcs0" " = %d", count))
                    goto found;

            if (strcmp(engine->name, "bsd") == 0)
                if (sscanf(line, "bcs0" " = %d", count))
                    goto found;

            if (strcmp(engine->name, "bsd1") == 0)
                if (sscanf(line, "vcs0" " = %d", count))
                    goto found;

            if (strcmp(engine->name, "bsd2") == 0)
                if (sscanf(line, "vcs1" " = %d", count))
                    goto found;

            if (strcmp(engine->name, "vebox") == 0)
                if (sscanf(line, "vecs0" " = %d", count))
                    goto found;
	}
    }

    igt_assert_f(0, "Reset entry not found.");

found:
    return;
}

static void send_canary(uint32_t fd, const struct intel_execution_engine *engine, uint32_t target, uint32_t offset, uint32_t *handle)
{
    struct drm_i915_gem_exec_object2 obj[2];
    struct drm_i915_gem_relocation_entry reloc;
    struct drm_i915_gem_execbuffer2 execbuf;

    uint32_t *bo;
    uint32_t batch[16];
    int i = 0;

    int64_t timeout = HANG_TIMEOUT;

    memset(&execbuf, 0, sizeof(execbuf));
    memset(&obj, 0, sizeof(obj));
    memset(&reloc, 0, sizeof(reloc));

    execbuf.buffers_ptr = to_user_pointer(obj);
    execbuf.buffer_count = 2;
    execbuf.flags = engine->exec_id;

    obj[0].handle = target;

    obj[1].handle = gem_create(fd, 4096);
    obj[1].relocation_count = 1;
    obj[1].relocs_ptr = to_user_pointer(&reloc);

    reloc.target_handle = obj[0].handle;
    reloc.read_domains = I915_GEM_DOMAIN_COMMAND;
    reloc.write_domain = I915_GEM_DOMAIN_COMMAND;
    reloc.delta = offset * sizeof(uint32_t);

    batch[i++] = MI_STORE_DATA_IMM | 2;

    reloc.offset = i * sizeof(uint32_t);

    batch[i++] = 0x0;
    batch[i++] = 0x0;
    batch[i++] = 0xdeadbeef;

    batch[i++] = 0x0;
    batch[i++] = MI_BATCH_BUFFER_END;

    gem_write(fd, obj[1].handle, 0, batch, sizeof(batch));
    gem_execbuf(fd, &execbuf);

    if (handle) {
        *handle = obj[1].handle;
        return;
    }

    if(gem_wait(fd, obj[1].handle, &timeout) != 0) {
        //Force reset and fail the test
        igt_force_gpu_reset(fd);
        igt_assert_f(0, "Bad batch did not hang in the expected timeframe!");
    }

    //Read back value to make sure batch executed
    bo = gem_mmap__cpu(fd, obj[0].handle, 0, 4096, PROT_READ);
    gem_set_domain(fd, obj[0].handle, I915_GEM_DOMAIN_CPU, 0);
/*
    for (int j = 0; j < sizeof(batch) / sizeof(uint32_t); j++)
    {
        printf("Buffer[%d]: %x\n", j, bo[j]);
    }
*/
    igt_assert_f(bo[offset] == 0xdeadbeef, "Expected value: %x, got: %x.", 0xdeadbeef, *bo);

    gem_close(fd, obj[1].handle);
}

static void verify_engines(uint32_t fd)
{
    const struct intel_execution_engine *engine;
    uint32_t scratch;

    //Send a batch that changes the value in BO
    for(engine = intel_execution_engines; engine->name; engine++)
    {
        scratch = gem_create(fd, 4096);
        send_canary(fd, engine, scratch, 0, NULL);
        gem_close(fd, scratch);
    }
}

#define RENDER_CLASS 0
#define VIDEO_DECODE_CLASS 1
#define VIDEO_ENHANCEMENT_CLASS 2
#define COPY_ENGINE_CLASS 3
static void context_set_watchdog(int fd, int engine_id,
                                 unsigned ctx, unsigned threshold)
{
    //No need to save/restore the values of the current context since it is
    //just used for the test
	struct drm_i915_gem_context_param arg = {
		.param = 0x7,//I915_CONTEXT_PARAM_WATCHDOG
		.ctx_id = ctx,
	};
    unsigned engines_threshold[MAX_ENGINES];
    unsigned *d = NULL;

    memset(&arg, 0, sizeof(arg));
    memset(&engines_threshold, 0, sizeof(engines_threshold));

    arg.ctx_id = ctx;
    arg.value = (uint64_t)&engines_threshold;
    arg.param = 0x7;//I915_CONTEXT_PARAM_WATCHDOG

    /* read existing values */
    gem_context_get_param(fd, &arg);
    printf("before set(): arg.ctx_id:%u, arg.size: %u, arg.param: 0x%x, arg.value: 0x%x \n",arg.ctx_id, arg.size, arg.param, arg.value);
    //igt_assert_eq(arg.size, sizeof(engines_threshold));
               //"more engines defined in i915, time to update i-g-t\n");

    switch (engine_id) {
    case I915_EXEC_RENDER:
         engines_threshold[RENDER_CLASS] = threshold;
         break;
    case I915_EXEC_BSD:
         engines_threshold[VIDEO_DECODE_CLASS] = threshold;
         break;
    case I915_EXEC_VEBOX:
         engines_threshold[VIDEO_ENHANCEMENT_CLASS] = threshold;
         break;
    default:
        break;
    }

    gem_context_set_param(fd, &arg);
    gem_context_get_param(fd, &arg);
    d = arg.value;
    printf("after set(): engine_threshold[RENDER_CLASS]: %d \n", *(d+0));
    printf("after set(): engine_threshold[VIDEO_CLASS]: %d \n", *(d+1));
    printf("after set(): engine_threshold[VIDEO_ENHACEMENT_CLASS]: %d \n", *(d+2));
}

static float get_timestamp_freq(uint32_t fd)
{
	uint32_t devid = intel_get_drm_devid(fd);

    if (IS_BROXTON(devid))
        return 52.083; //BXT timestamp granularity

    //TODO: skylake has a slightly different value for PMSI
    if (IS_SKYLAKE(devid))
        return 83.333; //SKL Normal timestamp granularity

    return -1;
}

static void inject_hang_timed(uint32_t fd,
                              uint32_t ctx,
                              const struct intel_execution_engine *engine,
                              unsigned flags,
                              uint64_t *delta)
{
	struct drm_i915_gem_relocation_entry reloc[3];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];

    uint32_t *bo;
    uint32_t start_timestamp, stop_timestamp;
    uint32_t timestamp_offset;

    int64_t timeout = HANG_TIMEOUT;

	uint32_t b[16];
    int i = 0;

    const int start_timestamp_pos = 0;
    const int stop_timestamp_pos = 1;

    float elapsed_time;

	igt_require_hang_ring(fd, engine->exec_id);

    switch (engine->exec_id) {
    case I915_EXEC_RENDER:
         timestamp_offset = 0x02358;
         break;
    case I915_EXEC_BSD:
         timestamp_offset = 0x12358;
         break;
    case I915_EXEC_VEBOX:
         timestamp_offset = 0x1a358;
         break;
    default:
        igt_assert_f(0, "No timestamp for ring");
        break;
    }

	memset(reloc, 0, sizeof(reloc));
	memset(obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));

    obj[0].handle = gem_create(fd, 4096);

	obj[1].handle = gem_create(fd, 4096);
	obj[1].relocation_count = 3;
	obj[1].relocs_ptr = to_user_pointer(reloc);

	memset(b, 0xc5, sizeof(b));

	/*
	 * We will loop on the same batch until the driver resets the engine.
	 * The batch is going to collect a timestamp value at the beginning and
	 * then in a loop until it is killed.
	 */
    b[i++] = MI_STORE_REGISTER_MEM | (4 - 2);
    b[i++] = timestamp_offset;

    reloc[0].offset = i * sizeof(uint32_t);
    b[i++] = 0x0;
    b[i++] = 0x0;

    //First relocation on Buffer Object
    reloc[0].target_handle = obj[0].handle;
    reloc[0].read_domains = I915_GEM_DOMAIN_COMMAND;
    reloc[0].write_domain = I915_GEM_DOMAIN_COMMAND;

    reloc[0].delta = start_timestamp_pos * sizeof(uint32_t);

	reloc[2].delta = i * sizeof(uint32_t);
    b[i++] = MI_STORE_REGISTER_MEM | (4 - 2);
    b[i++] = timestamp_offset;

    reloc[1].offset = i * sizeof(uint32_t);

    b[i++] = 0x1;
    b[i++] = 0x0;

    reloc[1].target_handle = obj[0].handle;
    reloc[1].read_domains = I915_GEM_DOMAIN_COMMAND;
    reloc[1].write_domain = I915_GEM_DOMAIN_COMMAND;

    reloc[1].delta = stop_timestamp_pos * sizeof(uint32_t);

	b[i++] = MI_BATCH_BUFFER_START | (1 << 8) | (3 - 2);
	reloc[2].offset = i * sizeof(uint32_t);
	b[i++] = MI_NOOP;
	b[i++] = MI_NOOP;

	b[i++] = MI_BATCH_BUFFER_END;
	b[i++] = MI_NOOP;

	gem_write(fd, obj[1].handle, 0, b, sizeof(b));

	reloc[2].target_handle = obj[1].handle;
	reloc[2].read_domains = I915_GEM_DOMAIN_COMMAND;

	execbuf.buffers_ptr = to_user_pointer(obj);
	execbuf.buffer_count = 2;
	execbuf.flags = engine->exec_id | engine->flags;
	i915_execbuffer2_set_context_id(execbuf, ctx);
	gem_execbuf(fd, &execbuf);

    //Wait for the execution of the BB
    if(gem_wait(fd, obj[1].handle, &timeout) != 0) {
        //Force reset and fail the test
        igt_force_gpu_reset(fd);
        igt_assert_f(0, "Bad batch did not hang in the expected timeframe!");
    }
/*
    bb = gem_mmap__cpu(fd, obj[1].handle, 0, 4096, PROT_READ);
    gem_set_domain(fd, obj[1].handle, I915_GEM_DOMAIN_CPU, 0);
    for (int j = 0; j < sizeof(b) / sizeof(uint32_t); j++)
    {
        printf("Batch buffer[%d]: %x\n", j, bb[j]);
    }
*/
    //Read values from BO
    bo = gem_mmap__cpu(fd, obj[0].handle, 0, 4096, PROT_READ);
    gem_set_domain(fd, obj[0].handle, I915_GEM_DOMAIN_CPU, 0);

    //TODO: unmap buffer

    start_timestamp = bo[start_timestamp_pos];
    stop_timestamp = bo[stop_timestamp_pos];
/*
    for (int j = 0; j < sizeof(b) / sizeof(uint32_t); j++)
    {
        //printf("Buffer Object[%d]: %x\n", j, bo[j]);
    }
*/
    *delta = (uint64_t)stop_timestamp - (uint64_t)start_timestamp;
    *delta += (delta >= 0) ? 0 : 0xFFFFFFFF;

    elapsed_time = *delta * get_timestamp_freq(fd); //10^-9 [s]
    elapsed_time /= 1000; //10^-6 [s]
    elapsed_time /= 1000; //10^-3 [s]

    if (*delta <= 0)
        igt_assert_f(0, "Negative time elapsed!");

    igt_debug("Elapsed time in milli seconds: %f\n", elapsed_time);
}

static void inject_hang(uint32_t fd, uint32_t ctx, const struct intel_execution_engine *engine, unsigned flags)
{
    int64_t timeout = HANG_TIMEOUT;
    igt_hang_t hang;
    hang = igt_hang_ctx(fd, ctx, engine->exec_id | engine->flags, flags);

     gem_sync(fd, hang.spin->handle);

/*
    if(gem_wait(fd, engine->exec_id, &timeout) != 0) {
        //Force reset and fail the test
        //igt_force_gpu_reset(fd);
        //igt_assert_f(0, "Bad batch did not hang in the expected timeframe!");
    }
*/
}

static void inject_hang_no_wait(uint32_t fd, uint32_t ctx, const struct intel_execution_engine *engine, unsigned flags, uint32_t *handle)
{
    igt_hang_t hang;
    hang = igt_hang_ctx(fd, ctx, engine->exec_id | engine->flags, flags);

    //*handle = hang.handle;
}

static void inject_hang_dependent(uint32_t fd, uint32_t ctx, const struct intel_execution_engine *engine, unsigned flags, uint32_t *handle, uint32_t target)
{
	struct drm_i915_gem_relocation_entry reloc[2];
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];

	uint32_t b[16];
    int i = 0;

	igt_require_hang_ring(fd, engine->exec_id);

	memset(reloc, 0, sizeof(reloc));
	memset(obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));

    execbuf.buffers_ptr = to_user_pointer(obj);
    execbuf.buffer_count = 2;
    execbuf.flags = engine->exec_id;

    obj[0].handle = gem_create(fd, 4096);

	obj[1].handle = gem_create(fd, 4096);
	obj[1].relocation_count = 2;
	obj[1].relocs_ptr = to_user_pointer(reloc);

	memset(b, 0xc5, sizeof(b));

    reloc[0].target_handle = obj[0].handle;
    reloc[0].read_domains = I915_GEM_DOMAIN_COMMAND;
    reloc[0].write_domain = I915_GEM_DOMAIN_COMMAND;
    reloc[0].delta = 0 * sizeof(uint32_t);

    reloc[1].target_handle = obj[1].handle;
    reloc[1].read_domains = I915_GEM_DOMAIN_COMMAND;

    b[i++] = MI_STORE_DATA_IMM | 2;

    reloc[0].offset = i * sizeof(uint32_t);

    b[i++] = 0x0;
    b[i++] = 0x0;
    b[i++] = 0xdeadbeef;

    reloc[1].delta = i * sizeof(uint32_t);

	b[i++] = MI_BATCH_BUFFER_START | (1 << 8) | (3 - 2);
	reloc[1].offset = i * sizeof(uint32_t);
	b[i++] = MI_NOOP;
	b[i++] = MI_NOOP;

	b[i++] = MI_BATCH_BUFFER_END;
	b[i++] = MI_NOOP;

    gem_write(fd, obj[1].handle, 0, b, sizeof(b));

	i915_execbuffer2_set_context_id(execbuf, ctx);
    gem_execbuf(fd, &execbuf);
/*
{
    uint32_t *bb = gem_mmap__cpu(fd, obj[1].handle, 0, 4096, PROT_READ);
    gem_set_domain(fd, obj[1].handle, I915_GEM_DOMAIN_CPU, 0);
    for (int j = 0; j < sizeof(b) / sizeof(uint32_t); j++)
    {
        printf("Batch buffer[%d]: %x\n", j, bb[j]);
    }
}
*/
}

static void media_hang_simple(int fd, const struct intel_execution_engine *engine)
{
	uint32_t ctx;
	unsigned flags = 0;

	//make sure the engine exists
	gem_require_ring(fd, engine->exec_id | engine->flags);

	//Make sure engines are working
	verify_engines(fd);

	//set context parameter
	ctx = 0; //Submit on default context
	context_set_watchdog(fd, engine->exec_id, ctx, WATCHDOG_THRESHOLD);

	clear_error_state(fd);
	igt_assert(!check_error_state(fd)); //Assert if error state is not clean

	inject_hang(fd, ctx, engine, flags);
	igt_assert(!check_error_state(fd));

	verify_engines(fd);
}

static void media_hang_stress(int fd, const struct intel_execution_engine *engine)
{
    uint32_t ctx;
    unsigned flags = 0;

    const uint32_t expected_resets = 10000;
    uint32_t watchdog_count, watchdog_count_init;
    uint32_t watchdog_delta;

    //make sure the engine exists
    gem_require_ring(fd, engine->exec_id | engine->flags);

    //set context parameter
    ctx = 0; //Submit on default context
    context_set_watchdog(fd, engine->exec_id, ctx, WATCHDOG_THRESHOLD);

    //Make sure engines are working
    verify_engines(fd);

    get_watchdog_count(fd, engine, &watchdog_count_init);
    for(int i = 0; i < 10000; i++)
        inject_hang(fd, ctx, engine, flags);

    verify_engines(fd);

    //Verify results
    get_watchdog_count(fd, engine, &watchdog_count);

    watchdog_delta = watchdog_count - watchdog_count_init; 

    igt_assert_f(watchdog_delta == expected_resets,
                 "The number of resets is different from what is expected:\n"
                 "\tDelta: %d\n"
                 "\tExpected: %d\n",
                 watchdog_delta,
                 expected_resets);
}

static void media_hang_timed(int fd, const struct intel_execution_engine *engine)
{
    uint32_t ctx;
    unsigned flags = 0;

    uint64_t exec_time_raw;
    float exec_time;
    float exec_time_expected = WATCHDOG_THRESHOLD;
    float exec_time_delta;
    const float tolerance = 0.90;

    const uint32_t expected_resets = 1;
    uint32_t watchdog_count, watchdog_count_init;
    uint32_t watchdog_delta;

    gem_require_ring(fd, engine->exec_id | engine->flags);

    get_watchdog_count(fd, engine, &watchdog_count_init);

    // Set context parameter
    ctx = 0; //Submit on default context
    context_set_watchdog(fd, engine->exec_id, ctx, WATCHDOG_THRESHOLD);

    // Make sure engines are working
    verify_engines(fd);

    inject_hang_timed(fd, ctx, engine, flags, &exec_time_raw);
    exec_time = exec_time_raw * get_timestamp_freq(fd);
    exec_time /= 1000;
    exec_time /= 1000;

    // Make sure engines are working
    verify_engines(fd);

    // Verify results
    get_watchdog_count(fd, engine, &watchdog_count);

    watchdog_delta = watchdog_count - watchdog_count_init;

    igt_assert_f(watchdog_delta == expected_resets,
                 "The number of resets is different from what is expected:\n"
                 "\tDelta: %d\n"
                 "\tExpected: %d\n",
                 watchdog_delta,
                 expected_resets);

    // Calculate time delta
    exec_time_delta = (exec_time > exec_time_expected) ? exec_time - exec_time_expected : exec_time_expected - exec_time; 

    // Normalize and compare with threshold
    igt_assert_f(((exec_time_delta / exec_time_expected) > tolerance) && (exec_time_delta / exec_time_expected < 1), "Reset did not complete within tolerance threshold.");

}

static void media_hang_dependency(int fd, const struct intel_execution_engine *engine)
{
    uint32_t ctx;
    unsigned flags = 0;

    uint32_t target;
    uint32_t handle[MAX_ENGINES];
    uint32_t offset = 1;
    uint32_t *bo;

    uint32_t watchdog_count, watchdog_count_init;
    const uint32_t expected_resets = 1;

    int64_t timeout = 10 * HANG_TIMEOUT;

    //make sure the engine exists
    gem_require_ring(fd, engine->exec_id | engine->flags);

    ctx = 0; //Submit on default context
    target = gem_create(fd, 4096);

    context_set_watchdog(fd, engine->exec_id, ctx, WATCHDOG_THRESHOLD + (100 * 1000));

    get_watchdog_count(fd, engine, &watchdog_count_init);

    inject_hang_dependent(fd, ctx, engine, flags, NULL, target);

    for(const struct intel_execution_engine *e = intel_execution_engines; e->name; e++)
    {
        send_canary(fd, e, target, offset, &handle[offset]);
        offset++;
    }

    if(gem_wait(fd, handle[offset-1], &timeout) != 0) {
        //Force reset and fail the test
        igt_force_gpu_reset(fd);
        igt_assert_f(0, "Bad batch did not hang in the expected timeframe!");
    }

    //Verify all batch executed correctly
    bo = gem_mmap__cpu(fd, target, 0, 4096, PROT_READ);
    gem_set_domain(fd, target, I915_GEM_DOMAIN_CPU, 0);

    /*Do not check hanging batch write, it might have not executed*/
    while (offset - 1) {
        igt_assert_eq(bo[--offset], 0xdeadbeef);
    }

    //Verify we get the number of resets we expect
    get_watchdog_count(fd, engine, &watchdog_count);

    igt_assert_f((watchdog_count - watchdog_count_init) == expected_resets, "The number of resets is different from what is expected:\n"
                                                                            "\tInitial reset count: %d\n"
                                                                            "\tFinal reset count: %d\n"
                                                                            "\tDelta: %d\n"
                                                                            "\tExpected: %d\n",
                                                                            watchdog_count_init,
                                                                            watchdog_count,
                                                                            watchdog_count - watchdog_count_init,
                                                                            expected_resets);

    gem_close(fd, target);

    verify_engines(fd);
}

static void all_engines_stress(int fd)
{
    uint32_t ctx = 0;
    unsigned flags = 0;

    int64_t timeout = HANG_TIMEOUT;
    uint32_t last_handle[MAX_ENGINES];

    uint32_t expected_resets = 0;
    uint32_t watchdog_count, watchdog_count_init;
    uint32_t watchdog_delta;

    get_watchdog_count(fd, NULL, &watchdog_count_init);

    //set context parameter
    for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) 
        context_set_watchdog(fd, engine->exec_id, ctx, WATCHDOG_THRESHOLD);

    //Make sure engines are working
    verify_engines(fd);

    for (int i = 0; i < 10000; i++) {
        for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) {
            //Skip if the engine doesn't exists or is not applicable
            if ((!gem_has_ring(fd, engine->exec_id)) ||
                (engine->exec_id == I915_EXEC_BLT) ||
                (engine->exec_id == 0))
                continue;

            inject_hang_no_wait(fd, ctx, engine, flags, &last_handle[engine->exec_id]);
            expected_resets++;
        }
    }

    for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++)
    {
        if(gem_wait(fd, last_handle[engine->exec_id], &timeout) != 0) {
            //Force reset and fail the test
            igt_force_gpu_reset(fd);
            igt_assert_f(0, "Bad batch did not hang in the expected timeframe!");
        }
    }

    verify_engines(fd);

    get_watchdog_count(fd, NULL, &watchdog_count);

    watchdog_delta = watchdog_count - watchdog_count_init;

    igt_assert_f(watchdog_delta == expected_resets,
                 "The number of resets is different from what is expected:\n"
                 "\tDelta: %d\n"
                 "\tExpected: %d\n",
                 watchdog_delta,
                 expected_resets);
}

igt_main
{
    igt_skip_on_simulation();

    int fd;

    igt_fixture {
	    fd = drm_open_driver(DRIVER_INTEL);
	    igt_require_gem(fd);
    }

    igt_subtest_group {

        for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) {
            //Skip on default exec id which is purely symbolic
            if (engine->exec_id == 0 || engine->exec_id == I915_EXEC_BLT)
                continue;

            igt_subtest_f("basic-%s", engine->name) {
                media_hang_simple(fd, engine);
            }

        }

    }

    igt_subtest_group {
        for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) {
            //Skip on default exec id which is purely symbolic
            if (engine->exec_id == 0 || engine->exec_id == I915_EXEC_BLT)
                continue;

            igt_subtest_f("ctx-stress-%s", engine->name) {
                media_hang_stress(fd, engine);
            }

        }

    }

    igt_subtest_group {
            igt_subtest_f("multi-ring-stress") {
                all_engines_stress(fd);
            }

    }

    igt_subtest_group {
        for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) {
            //Skip on default exec id which is purely symbolic
            if (engine->exec_id == 0 || engine->exec_id == I915_EXEC_BLT)
                continue;

            igt_subtest_f("dependency-%s", engine->name) {
                media_hang_dependency(fd, engine);
            }

        }

    }

    igt_subtest_group {
        for (const struct intel_execution_engine *engine = intel_execution_engines; engine->name; engine++) {
            //Skip on default exec id which is purely symbolic
            if (engine->exec_id == 0 || engine->exec_id == I915_EXEC_BLT)
                continue;
            igt_subtest_f("timed-%s", engine->name) {
                media_hang_timed(fd, engine);
            }
        }
    }

     igt_fixture {
         close(fd);
     }
}
