/*
 * dev - Device functions
 *
 * Copyright (C) 2015 Javier González <javier@cnexlabs.com>
 * Copyright (C) 2015 Matias Bjørling <matias@cnexlabs.com>
 * Copyright (C) 2016 Simon A. F. Lund <slund@cnexlabs.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <libudev.h>
#include <linux/lightnvm.h>
#include <liblightnvm.h>
#include <nvm.h>
#include <nvm_debug.h>

void _spec_12_idf_pr(struct spec_12_idf *idf)
{
	printf(" verid(0x%x), vnvmt(%u), cgroups(%u),\n",
		idf->verid, idf->vnvmt, idf->cgroups);
	printf(" cap("NVM_I32_FMT"),\n", NVM_I32_TO_STR(idf->cap));
	printf(" dom("NVM_I32_FMT"),\n", NVM_I32_TO_STR(idf->dom));

	for (int i = 0; i < idf->cgroups; ++i) {
		struct spec_cgrp grp = idf->grp[i];

		printf(" cgrp(%d) {\n", i);
		printf("  mtype(0x%02x),\n", grp.mtype);
		if (!grp.mtype) {
			printf("  fmtype("NVM_I8_FMT"),\n",
				NVM_I8_TO_STR(grp.fmtype));
			printf("  num_ch(%u), num_luns(%u), num_pln(%u),",
				grp.num_ch, grp.num_lun, grp.num_pln);
			printf(" num_blk(%u), num_pg(%u),\n  fpg_sz(%u),",
				grp.num_blk, grp.num_pg, grp.fpg_sz);
			printf(" csecs(%u), sos(%u),\n",
				grp.csecs, grp.sos);
			printf("  trdt(%d), trdm(%d),\n", grp.trdt, grp.trdm);
			printf("  tprt(%d), tprm(%d),\n", grp.tprt, grp.tprm);
			printf("  tbet(%d), tbem(%d),\n", grp.tbet, grp.tbem);
			printf("  mpos("NVM_I32_FMT"),\n",
				NVM_I32_TO_STR(grp.mpos));
			printf("  mccap("NVM_I32_FMT"),\n",
				NVM_I32_TO_STR(grp.mccap));
			printf("  cpar(%d),\n", grp.cpar);
			printf("  mts(NOT IMPLEMENTED)\n");
		}
		printf(" }\n");
	}
}

void _spec_20_idf_pr(struct spec_20_idf *idf)
{

}

void spec_idf_pr(struct spec_idf *idf)
{
	printf("spec_idf {\n");
	switch(idf->verid) {
	case SPEC_VERID_12:
		_spec_12_idf_pr((struct spec_12_idf*)idf);
		break;
	case SPEC_VERID_20:
		_spec_20_idf_pr((struct spec_20_idf*)idf);
		break;
	default:
		printf("verid(%d:UNSUPPORTED)\n", idf->verid);
	}
	printf("}\n");
}

