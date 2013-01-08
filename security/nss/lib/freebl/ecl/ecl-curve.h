/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ecl-exp.h"
#include <stdlib.h>

#ifndef __ecl_curve_h_
#define __ecl_curve_h_

#ifdef NSS_ECC_MORE_THAN_SUITE_B
#error This source file is for Basic ECC only .
#endif

static const ECCurveParams ecCurve_NIST_P256 = {
	"NIST-P256", ECField_GFp, 256,
	"FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF",
	"FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFC",
	"5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B",
	"6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296",
	"4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5",
	"FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", 1
};

static const ECCurveParams ecCurve_NIST_P384 = {
	"NIST-P384", ECField_GFp, 384,
	"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF",
	"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFC",
	"B3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF",
	"AA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7",
	"3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F",
	"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973",
	1
};

static const ECCurveParams ecCurve_NIST_P521 = {
	"NIST-P521", ECField_GFp, 521,
	"01FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
	"01FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC",
	"0051953EB9618E1C9A1F929A21A0B68540EEA2DA725B99B315F3B8B489918EF109E156193951EC7E937B1652C0BD3BB1BF073573DF883D2C34F1EF451FD46B503F00",
	"00C6858E06B70404E9CD9E3ECB662395B4429C648139053FB521F828AF606B4D3DBAA14B5E77EFE75928FE1DC127A2FFA8DE3348B3C1856A429BF97E7E31C2E5BD66",
	"011839296A789A3BC0045C8A5FB42C7D1BD998F54449579B446817AFBD17273E662C97EE72995EF42640C550B9013FAD0761353C7086A272C24088BE94769FD16650",
	"01FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFA51868783BF2F966B7FCC0148F709A5D03BB5C9B8899C47AEBB6FB71E91386409",
	1
};

/* mapping between ECCurveName enum and pointers to ECCurveParams */
static const ECCurveParams *ecCurve_map[] = {
	NULL,			/* ECCurve_noName */
	NULL,			/* ECCurve_NIST_P192 */
	NULL,			/* ECCurve_NIST_P224 */
	&ecCurve_NIST_P256,	/* ECCurve_NIST_P256 */
	&ecCurve_NIST_P384,	/* ECCurve_NIST_P384 */
	&ecCurve_NIST_P521,	/* ECCurve_NIST_P521 */
	NULL,			/* ECCurve_NIST_K163 */
	NULL,			/* ECCurve_NIST_B163 */
	NULL,			/* ECCurve_NIST_K233 */
	NULL,			/* ECCurve_NIST_B233 */
	NULL,			/* ECCurve_NIST_K283 */
	NULL,			/* ECCurve_NIST_B283 */
	NULL,			/* ECCurve_NIST_K409 */
	NULL,			/* ECCurve_NIST_B409 */
	NULL,			/* ECCurve_NIST_K571 */
	NULL,			/* ECCurve_NIST_B571 */
	NULL,			/* ECCurve_X9_62_PRIME_192V2 */
	NULL,			/* ECCurve_X9_62_PRIME_192V3 */
	NULL,			/* ECCurve_X9_62_PRIME_239V1 */
	NULL,			/* ECCurve_X9_62_PRIME_239V2 */
	NULL,			/* ECCurve_X9_62_PRIME_239V3 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB163V1 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB163V2 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB163V3 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB176V1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB191V1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB191V2 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB191V3 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB208W1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB239V1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB239V2 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB239V3 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB272W1 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB304W1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB359V1 */
	NULL,			/* ECCurve_X9_62_CHAR2_PNB368W1 */
	NULL,			/* ECCurve_X9_62_CHAR2_TNB431R1 */
	NULL,			/* ECCurve_SECG_PRIME_112R1 */
	NULL,			/* ECCurve_SECG_PRIME_112R2 */
	NULL,			/* ECCurve_SECG_PRIME_128R1 */
	NULL,			/* ECCurve_SECG_PRIME_128R2 */
	NULL,			/* ECCurve_SECG_PRIME_160K1 */
	NULL,			/* ECCurve_SECG_PRIME_160R1 */
	NULL,			/* ECCurve_SECG_PRIME_160R2 */
	NULL,			/* ECCurve_SECG_PRIME_192K1 */
	NULL,			/* ECCurve_SECG_PRIME_224K1 */
	NULL,			/* ECCurve_SECG_PRIME_256K1 */
	NULL,			/* ECCurve_SECG_CHAR2_113R1 */
	NULL,			/* ECCurve_SECG_CHAR2_113R2 */
	NULL,			/* ECCurve_SECG_CHAR2_131R1 */
	NULL,			/* ECCurve_SECG_CHAR2_131R2 */
	NULL,			/* ECCurve_SECG_CHAR2_163R1 */
	NULL,			/* ECCurve_SECG_CHAR2_193R1 */
	NULL,			/* ECCurve_SECG_CHAR2_193R2 */
	NULL,			/* ECCurve_SECG_CHAR2_239K1 */
	NULL,			/* ECCurve_WTLS_1 */
	NULL,			/* ECCurve_WTLS_8 */
	NULL,			/* ECCurve_WTLS_9 */
	NULL			/* ECCurve_pastLastCurve */
};

#endif
