/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_sctp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/gsb_crc32.h>
#include <sys/mbuf.h>

#include <netinet/sctp.h>
#include <netinet/sctp_crc32.h>
#if defined(SCTP) || defined(SCTP_SUPPORT)
#include <netinet/sctp_os.h>
#include <netinet/sctp_pcb.h>
#endif

static uint32_t
sctp_finalize_crc32c(uint32_t crc32c)
{
	uint32_t result;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t byte0, byte1, byte2, byte3;
#endif

	/* Complement the result */
	result = ~crc32c;
#if BYTE_ORDER == BIG_ENDIAN
	/*
	 * For BIG-ENDIAN platforms the result is in little-endian form. So
	 * we must swap the bytes to return the result in network byte
	 * order.
	 */
	byte0 = result & 0x000000ff;
	byte1 = (result >> 8) & 0x000000ff;
	byte2 = (result >> 16) & 0x000000ff;
	byte3 = (result >> 24) & 0x000000ff;
	crc32c = ((byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3);
#else
	/*
	 * For LITTLE ENDIAN platforms the result is in already in network
	 * byte order.
	 */
	crc32c = result;
#endif
	return (crc32c);
}

static int
sctp_calculate_cksum_cb(void *arg, void *data, u_int len)
{
	uint32_t *basep;

	basep = arg;
	*basep = calculate_crc32c(*basep, data, len);
	return (0);
}

/*
 * Compute the SCTP checksum in network byte order for a given mbuf chain m
 * which contains an SCTP packet starting at offset.
 * Since this function is also called by ipfw, don't assume that
 * it is compiled on a kernel with SCTP support.
 */
uint32_t
sctp_calculate_cksum(struct mbuf *m, uint32_t offset)
{
	uint32_t base;
	int len;

	M_ASSERTPKTHDR(m);
	KASSERT(offset < m->m_pkthdr.len,
	    ("%s: invalid offset %u into mbuf %p", __func__, offset, m));

	base = 0xffffffff;
	len = m->m_pkthdr.len - offset;
	(void)m_apply(m, offset, len, sctp_calculate_cksum_cb, &base);
	return (sctp_finalize_crc32c(base));
}

#if defined(SCTP) || defined(SCTP_SUPPORT)

VNET_DEFINE(struct sctp_base_info, system_base_info);

/*
 * Compute and insert the SCTP checksum in network byte order for a given
 * mbuf chain m which contains an SCTP packet starting at offset.
 */
void
sctp_delayed_cksum(struct mbuf *m, uint32_t offset)
{
	uint32_t checksum;

	checksum = sctp_calculate_cksum(m, offset);
	SCTP_STAT_DECR(sctps_sendhwcrc);
	SCTP_STAT_INCR(sctps_sendswcrc);
	offset += offsetof(struct sctphdr, checksum);

	if (offset + sizeof(uint32_t) > (uint32_t)(m->m_pkthdr.len)) {
#ifdef INVARIANTS
		panic("sctp_delayed_cksum(): m->m_pkthdr.len: %d, offset: %u.",
		    m->m_pkthdr.len, offset);
#else
		SCTP_PRINTF("sctp_delayed_cksum(): m->m_pkthdr.len: %d, offset: %u.\n",
		    m->m_pkthdr.len, offset);
#endif
		return;
	}
	m_copyback(m, (int)offset, (int)sizeof(uint32_t), (caddr_t)&checksum);
}
#endif
