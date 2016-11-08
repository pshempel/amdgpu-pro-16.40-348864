/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _CORE_STATUS_H_
#define _CORE_STATUS_H_

enum dc_status {
	DC_OK = 1,

	DC_NO_CONTROLLER_RESOURCE,
	DC_NO_STREAM_ENG_RESOURCE,
	DC_NO_CLOCK_SOURCE_RESOURCE,
	DC_FAIL_CONTROLLER_VALIDATE,
	DC_FAIL_ENC_VALIDATE,
	DC_FAIL_ATTACH_SURFACES,
	DC_NO_DP_LINK_BANDWIDTH,
	DC_EXCEED_DONGLE_MAX_CLK,
	DC_SURFACE_PIXEL_FORMAT_UNSUPPORTED,
	DC_FAIL_BANDWIDTH_VALIDATE, /* BW and Watermark validation */

	DC_ERROR_UNEXPECTED = -1
};

#endif /* _CORE_STATUS_H_ */
