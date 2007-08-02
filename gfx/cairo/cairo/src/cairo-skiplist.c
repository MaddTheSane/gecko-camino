/*
 * Copyright © 2006 Keith Packard
 * Copyright © 2006 Carl Worth
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "cairoint.h"

#include "cairo-skiplist-private.h"

#define ELT_DATA(elt) (void *)	((char*) (elt) - list->data_size)
#define NEXT_TO_ELT(next)	(skip_elt_t *) ((char *) (next) - offsetof (skip_elt_t, next))

/* Four 256 element lookup tables back to back implementing a linear
 * feedback shift register of degree 32. */
static unsigned const _cairo_lfsr_random_lut[1024] = {
 0x00000000, 0x9a795537, 0xae8bff59, 0x34f2aa6e, 0xc76eab85, 0x5d17feb2,
 0x69e554dc, 0xf39c01eb, 0x14a4023d, 0x8edd570a, 0xba2ffd64, 0x2056a853,
 0xd3caa9b8, 0x49b3fc8f, 0x7d4156e1, 0xe73803d6, 0x2948047a, 0xb331514d,
 0x87c3fb23, 0x1dbaae14, 0xee26afff, 0x745ffac8, 0x40ad50a6, 0xdad40591,
 0x3dec0647, 0xa7955370, 0x9367f91e, 0x091eac29, 0xfa82adc2, 0x60fbf8f5,
 0x5409529b, 0xce7007ac, 0x529008f4, 0xc8e95dc3, 0xfc1bf7ad, 0x6662a29a,
 0x95fea371, 0x0f87f646, 0x3b755c28, 0xa10c091f, 0x46340ac9, 0xdc4d5ffe,
 0xe8bff590, 0x72c6a0a7, 0x815aa14c, 0x1b23f47b, 0x2fd15e15, 0xb5a80b22,
 0x7bd80c8e, 0xe1a159b9, 0xd553f3d7, 0x4f2aa6e0, 0xbcb6a70b, 0x26cff23c,
 0x123d5852, 0x88440d65, 0x6f7c0eb3, 0xf5055b84, 0xc1f7f1ea, 0x5b8ea4dd,
 0xa812a536, 0x326bf001, 0x06995a6f, 0x9ce00f58, 0xa52011e8, 0x3f5944df,
 0x0babeeb1, 0x91d2bb86, 0x624eba6d, 0xf837ef5a, 0xccc54534, 0x56bc1003,
 0xb18413d5, 0x2bfd46e2, 0x1f0fec8c, 0x8576b9bb, 0x76eab850, 0xec93ed67,
 0xd8614709, 0x4218123e, 0x8c681592, 0x161140a5, 0x22e3eacb, 0xb89abffc,
 0x4b06be17, 0xd17feb20, 0xe58d414e, 0x7ff41479, 0x98cc17af, 0x02b54298,
 0x3647e8f6, 0xac3ebdc1, 0x5fa2bc2a, 0xc5dbe91d, 0xf1294373, 0x6b501644,
 0xf7b0191c, 0x6dc94c2b, 0x593be645, 0xc342b372, 0x30deb299, 0xaaa7e7ae,
 0x9e554dc0, 0x042c18f7, 0xe3141b21, 0x796d4e16, 0x4d9fe478, 0xd7e6b14f,
 0x247ab0a4, 0xbe03e593, 0x8af14ffd, 0x10881aca, 0xdef81d66, 0x44814851,
 0x7073e23f, 0xea0ab708, 0x1996b6e3, 0x83efe3d4, 0xb71d49ba, 0x2d641c8d,
 0xca5c1f5b, 0x50254a6c, 0x64d7e002, 0xfeaeb535, 0x0d32b4de, 0x974be1e9,
 0xa3b94b87, 0x39c01eb0, 0xd03976e7, 0x4a4023d0, 0x7eb289be, 0xe4cbdc89,
 0x1757dd62, 0x8d2e8855, 0xb9dc223b, 0x23a5770c, 0xc49d74da, 0x5ee421ed,
 0x6a168b83, 0xf06fdeb4, 0x03f3df5f, 0x998a8a68, 0xad782006, 0x37017531,
 0xf971729d, 0x630827aa, 0x57fa8dc4, 0xcd83d8f3, 0x3e1fd918, 0xa4668c2f,
 0x90942641, 0x0aed7376, 0xedd570a0, 0x77ac2597, 0x435e8ff9, 0xd927dace,
 0x2abbdb25, 0xb0c28e12, 0x8430247c, 0x1e49714b, 0x82a97e13, 0x18d02b24,
 0x2c22814a, 0xb65bd47d, 0x45c7d596, 0xdfbe80a1, 0xeb4c2acf, 0x71357ff8,
 0x960d7c2e, 0x0c742919, 0x38868377, 0xa2ffd640, 0x5163d7ab, 0xcb1a829c,
 0xffe828f2, 0x65917dc5, 0xabe17a69, 0x31982f5e, 0x056a8530, 0x9f13d007,
 0x6c8fd1ec, 0xf6f684db, 0xc2042eb5, 0x587d7b82, 0xbf457854, 0x253c2d63,
 0x11ce870d, 0x8bb7d23a, 0x782bd3d1, 0xe25286e6, 0xd6a02c88, 0x4cd979bf,
 0x7519670f, 0xef603238, 0xdb929856, 0x41ebcd61, 0xb277cc8a, 0x280e99bd,
 0x1cfc33d3, 0x868566e4, 0x61bd6532, 0xfbc43005, 0xcf369a6b, 0x554fcf5c,
 0xa6d3ceb7, 0x3caa9b80, 0x085831ee, 0x922164d9, 0x5c516375, 0xc6283642,
 0xf2da9c2c, 0x68a3c91b, 0x9b3fc8f0, 0x01469dc7, 0x35b437a9, 0xafcd629e,
 0x48f56148, 0xd28c347f, 0xe67e9e11, 0x7c07cb26, 0x8f9bcacd, 0x15e29ffa,
 0x21103594, 0xbb6960a3, 0x27896ffb, 0xbdf03acc, 0x890290a2, 0x137bc595,
 0xe0e7c47e, 0x7a9e9149, 0x4e6c3b27, 0xd4156e10, 0x332d6dc6, 0xa95438f1,
 0x9da6929f, 0x07dfc7a8, 0xf443c643, 0x6e3a9374, 0x5ac8391a, 0xc0b16c2d,
 0x0ec16b81, 0x94b83eb6, 0xa04a94d8, 0x3a33c1ef, 0xc9afc004, 0x53d69533,
 0x67243f5d, 0xfd5d6a6a, 0x1a6569bc, 0x801c3c8b, 0xb4ee96e5, 0x2e97c3d2,
 0xdd0bc239, 0x4772970e, 0x73803d60, 0xe9f96857, 0x00000000, 0x3a0bb8f9,
 0x741771f2, 0x4e1cc90b, 0xe82ee3e4, 0xd2255b1d, 0x9c399216, 0xa6322aef,
 0x4a2492ff, 0x702f2a06, 0x3e33e30d, 0x04385bf4, 0xa20a711b, 0x9801c9e2,
 0xd61d00e9, 0xec16b810, 0x944925fe, 0xae429d07, 0xe05e540c, 0xda55ecf5,
 0x7c67c61a, 0x466c7ee3, 0x0870b7e8, 0x327b0f11, 0xde6db701, 0xe4660ff8,
 0xaa7ac6f3, 0x90717e0a, 0x364354e5, 0x0c48ec1c, 0x42542517, 0x785f9dee,
 0xb2eb1ecb, 0x88e0a632, 0xc6fc6f39, 0xfcf7d7c0, 0x5ac5fd2f, 0x60ce45d6,
 0x2ed28cdd, 0x14d93424, 0xf8cf8c34, 0xc2c434cd, 0x8cd8fdc6, 0xb6d3453f,
 0x10e16fd0, 0x2aead729, 0x64f61e22, 0x5efda6db, 0x26a23b35, 0x1ca983cc,
 0x52b54ac7, 0x68bef23e, 0xce8cd8d1, 0xf4876028, 0xba9ba923, 0x809011da,
 0x6c86a9ca, 0x568d1133, 0x1891d838, 0x229a60c1, 0x84a84a2e, 0xbea3f2d7,
 0xf0bf3bdc, 0xcab48325, 0xffaf68a1, 0xc5a4d058, 0x8bb81953, 0xb1b3a1aa,
 0x17818b45, 0x2d8a33bc, 0x6396fab7, 0x599d424e, 0xb58bfa5e, 0x8f8042a7,
 0xc19c8bac, 0xfb973355, 0x5da519ba, 0x67aea143, 0x29b26848, 0x13b9d0b1,
 0x6be64d5f, 0x51edf5a6, 0x1ff13cad, 0x25fa8454, 0x83c8aebb, 0xb9c31642,
 0xf7dfdf49, 0xcdd467b0, 0x21c2dfa0, 0x1bc96759, 0x55d5ae52, 0x6fde16ab,
 0xc9ec3c44, 0xf3e784bd, 0xbdfb4db6, 0x87f0f54f, 0x4d44766a, 0x774fce93,
 0x39530798, 0x0358bf61, 0xa56a958e, 0x9f612d77, 0xd17de47c, 0xeb765c85,
 0x0760e495, 0x3d6b5c6c, 0x73779567, 0x497c2d9e, 0xef4e0771, 0xd545bf88,
 0x9b597683, 0xa152ce7a, 0xd90d5394, 0xe306eb6d, 0xad1a2266, 0x97119a9f,
 0x3123b070, 0x0b280889, 0x4534c182, 0x7f3f797b, 0x9329c16b, 0xa9227992,
 0xe73eb099, 0xdd350860, 0x7b07228f, 0x410c9a76, 0x0f10537d, 0x351beb84,
 0x65278475, 0x5f2c3c8c, 0x1130f587, 0x2b3b4d7e, 0x8d096791, 0xb702df68,
 0xf91e1663, 0xc315ae9a, 0x2f03168a, 0x1508ae73, 0x5b146778, 0x611fdf81,
 0xc72df56e, 0xfd264d97, 0xb33a849c, 0x89313c65, 0xf16ea18b, 0xcb651972,
 0x8579d079, 0xbf726880, 0x1940426f, 0x234bfa96, 0x6d57339d, 0x575c8b64,
 0xbb4a3374, 0x81418b8d, 0xcf5d4286, 0xf556fa7f, 0x5364d090, 0x696f6869,
 0x2773a162, 0x1d78199b, 0xd7cc9abe, 0xedc72247, 0xa3dbeb4c, 0x99d053b5,
 0x3fe2795a, 0x05e9c1a3, 0x4bf508a8, 0x71feb051, 0x9de80841, 0xa7e3b0b8,
 0xe9ff79b3, 0xd3f4c14a, 0x75c6eba5, 0x4fcd535c, 0x01d19a57, 0x3bda22ae,
 0x4385bf40, 0x798e07b9, 0x3792ceb2, 0x0d99764b, 0xabab5ca4, 0x91a0e45d,
 0xdfbc2d56, 0xe5b795af, 0x09a12dbf, 0x33aa9546, 0x7db65c4d, 0x47bde4b4,
 0xe18fce5b, 0xdb8476a2, 0x9598bfa9, 0xaf930750, 0x9a88ecd4, 0xa083542d,
 0xee9f9d26, 0xd49425df, 0x72a60f30, 0x48adb7c9, 0x06b17ec2, 0x3cbac63b,
 0xd0ac7e2b, 0xeaa7c6d2, 0xa4bb0fd9, 0x9eb0b720, 0x38829dcf, 0x02892536,
 0x4c95ec3d, 0x769e54c4, 0x0ec1c92a, 0x34ca71d3, 0x7ad6b8d8, 0x40dd0021,
 0xe6ef2ace, 0xdce49237, 0x92f85b3c, 0xa8f3e3c5, 0x44e55bd5, 0x7eeee32c,
 0x30f22a27, 0x0af992de, 0xaccbb831, 0x96c000c8, 0xd8dcc9c3, 0xe2d7713a,
 0x2863f21f, 0x12684ae6, 0x5c7483ed, 0x667f3b14, 0xc04d11fb, 0xfa46a902,
 0xb45a6009, 0x8e51d8f0, 0x624760e0, 0x584cd819, 0x16501112, 0x2c5ba9eb,
 0x8a698304, 0xb0623bfd, 0xfe7ef2f6, 0xc4754a0f, 0xbc2ad7e1, 0x86216f18,
 0xc83da613, 0xf2361eea, 0x54043405, 0x6e0f8cfc, 0x201345f7, 0x1a18fd0e,
 0xf60e451e, 0xcc05fde7, 0x821934ec, 0xb8128c15, 0x1e20a6fa, 0x242b1e03,
 0x6a37d708, 0x503c6ff1, 0x00000000, 0xca4f08ea, 0x0ee744e3, 0xc4a84c09,
 0x1dce89c6, 0xd781812c, 0x1329cd25, 0xd966c5cf, 0x3b9d138c, 0xf1d21b66,
 0x357a576f, 0xff355f85, 0x26539a4a, 0xec1c92a0, 0x28b4dea9, 0xe2fbd643,
 0x773a2718, 0xbd752ff2, 0x79dd63fb, 0xb3926b11, 0x6af4aede, 0xa0bba634,
 0x6413ea3d, 0xae5ce2d7, 0x4ca73494, 0x86e83c7e, 0x42407077, 0x880f789d,
 0x5169bd52, 0x9b26b5b8, 0x5f8ef9b1, 0x95c1f15b, 0xee744e30, 0x243b46da,
 0xe0930ad3, 0x2adc0239, 0xf3bac7f6, 0x39f5cf1c, 0xfd5d8315, 0x37128bff,
 0xd5e95dbc, 0x1fa65556, 0xdb0e195f, 0x114111b5, 0xc827d47a, 0x0268dc90,
 0xc6c09099, 0x0c8f9873, 0x994e6928, 0x530161c2, 0x97a92dcb, 0x5de62521,
 0x8480e0ee, 0x4ecfe804, 0x8a67a40d, 0x4028ace7, 0xa2d37aa4, 0x689c724e,
 0xac343e47, 0x667b36ad, 0xbf1df362, 0x7552fb88, 0xb1fab781, 0x7bb5bf6b,
 0x4691c957, 0x8cdec1bd, 0x48768db4, 0x8239855e, 0x5b5f4091, 0x9110487b,
 0x55b80472, 0x9ff70c98, 0x7d0cdadb, 0xb743d231, 0x73eb9e38, 0xb9a496d2,
 0x60c2531d, 0xaa8d5bf7, 0x6e2517fe, 0xa46a1f14, 0x31abee4f, 0xfbe4e6a5,
 0x3f4caaac, 0xf503a246, 0x2c656789, 0xe62a6f63, 0x2282236a, 0xe8cd2b80,
 0x0a36fdc3, 0xc079f529, 0x04d1b920, 0xce9eb1ca, 0x17f87405, 0xddb77cef,
 0x191f30e6, 0xd350380c, 0xa8e58767, 0x62aa8f8d, 0xa602c384, 0x6c4dcb6e,
 0xb52b0ea1, 0x7f64064b, 0xbbcc4a42, 0x718342a8, 0x937894eb, 0x59379c01,
 0x9d9fd008, 0x57d0d8e2, 0x8eb61d2d, 0x44f915c7, 0x805159ce, 0x4a1e5124,
 0xdfdfa07f, 0x1590a895, 0xd138e49c, 0x1b77ec76, 0xc21129b9, 0x085e2153,
 0xccf66d5a, 0x06b965b0, 0xe442b3f3, 0x2e0dbb19, 0xeaa5f710, 0x20eafffa,
 0xf98c3a35, 0x33c332df, 0xf76b7ed6, 0x3d24763c, 0x8d2392ae, 0x476c9a44,
 0x83c4d64d, 0x498bdea7, 0x90ed1b68, 0x5aa21382, 0x9e0a5f8b, 0x54455761,
 0xb6be8122, 0x7cf189c8, 0xb859c5c1, 0x7216cd2b, 0xab7008e4, 0x613f000e,
 0xa5974c07, 0x6fd844ed, 0xfa19b5b6, 0x3056bd5c, 0xf4fef155, 0x3eb1f9bf,
 0xe7d73c70, 0x2d98349a, 0xe9307893, 0x237f7079, 0xc184a63a, 0x0bcbaed0,
 0xcf63e2d9, 0x052cea33, 0xdc4a2ffc, 0x16052716, 0xd2ad6b1f, 0x18e263f5,
 0x6357dc9e, 0xa918d474, 0x6db0987d, 0xa7ff9097, 0x7e995558, 0xb4d65db2,
 0x707e11bb, 0xba311951, 0x58cacf12, 0x9285c7f8, 0x562d8bf1, 0x9c62831b,
 0x450446d4, 0x8f4b4e3e, 0x4be30237, 0x81ac0add, 0x146dfb86, 0xde22f36c,
 0x1a8abf65, 0xd0c5b78f, 0x09a37240, 0xc3ec7aaa, 0x074436a3, 0xcd0b3e49,
 0x2ff0e80a, 0xe5bfe0e0, 0x2117ace9, 0xeb58a403, 0x323e61cc, 0xf8716926,
 0x3cd9252f, 0xf6962dc5, 0xcbb25bf9, 0x01fd5313, 0xc5551f1a, 0x0f1a17f0,
 0xd67cd23f, 0x1c33dad5, 0xd89b96dc, 0x12d49e36, 0xf02f4875, 0x3a60409f,
 0xfec80c96, 0x3487047c, 0xede1c1b3, 0x27aec959, 0xe3068550, 0x29498dba,
 0xbc887ce1, 0x76c7740b, 0xb26f3802, 0x782030e8, 0xa146f527, 0x6b09fdcd,
 0xafa1b1c4, 0x65eeb92e, 0x87156f6d, 0x4d5a6787, 0x89f22b8e, 0x43bd2364,
 0x9adbe6ab, 0x5094ee41, 0x943ca248, 0x5e73aaa2, 0x25c615c9, 0xef891d23,
 0x2b21512a, 0xe16e59c0, 0x38089c0f, 0xf24794e5, 0x36efd8ec, 0xfca0d006,
 0x1e5b0645, 0xd4140eaf, 0x10bc42a6, 0xdaf34a4c, 0x03958f83, 0xc9da8769,
 0x0d72cb60, 0xc73dc38a, 0x52fc32d1, 0x98b33a3b, 0x5c1b7632, 0x96547ed8,
 0x4f32bb17, 0x857db3fd, 0x41d5fff4, 0x8b9af71e, 0x6961215d, 0xa32e29b7,
 0x678665be, 0xadc96d54, 0x74afa89b, 0xbee0a071, 0x7a48ec78, 0xb007e492,
 0x00000000, 0x803e706b, 0x9a05b5e1, 0x1a3bc58a, 0xae723ef5, 0x2e4c4e9e,
 0x34778b14, 0xb449fb7f, 0xc69d28dd, 0x46a358b6, 0x5c989d3c, 0xdca6ed57,
 0x68ef1628, 0xe8d16643, 0xf2eaa3c9, 0x72d4d3a2, 0x1743048d, 0x977d74e6,
 0x8d46b16c, 0x0d78c107, 0xb9313a78, 0x390f4a13, 0x23348f99, 0xa30afff2,
 0xd1de2c50, 0x51e05c3b, 0x4bdb99b1, 0xcbe5e9da, 0x7fac12a5, 0xff9262ce,
 0xe5a9a744, 0x6597d72f, 0x2e86091a, 0xaeb87971, 0xb483bcfb, 0x34bdcc90,
 0x80f437ef, 0x00ca4784, 0x1af1820e, 0x9acff265, 0xe81b21c7, 0x682551ac,
 0x721e9426, 0xf220e44d, 0x46691f32, 0xc6576f59, 0xdc6caad3, 0x5c52dab8,
 0x39c50d97, 0xb9fb7dfc, 0xa3c0b876, 0x23fec81d, 0x97b73362, 0x17894309,
 0x0db28683, 0x8d8cf6e8, 0xff58254a, 0x7f665521, 0x655d90ab, 0xe563e0c0,
 0x512a1bbf, 0xd1146bd4, 0xcb2fae5e, 0x4b11de35, 0x5d0c1234, 0xdd32625f,
 0xc709a7d5, 0x4737d7be, 0xf37e2cc1, 0x73405caa, 0x697b9920, 0xe945e94b,
 0x9b913ae9, 0x1baf4a82, 0x01948f08, 0x81aaff63, 0x35e3041c, 0xb5dd7477,
 0xafe6b1fd, 0x2fd8c196, 0x4a4f16b9, 0xca7166d2, 0xd04aa358, 0x5074d333,
 0xe43d284c, 0x64035827, 0x7e389dad, 0xfe06edc6, 0x8cd23e64, 0x0cec4e0f,
 0x16d78b85, 0x96e9fbee, 0x22a00091, 0xa29e70fa, 0xb8a5b570, 0x389bc51b,
 0x738a1b2e, 0xf3b46b45, 0xe98faecf, 0x69b1dea4, 0xddf825db, 0x5dc655b0,
 0x47fd903a, 0xc7c3e051, 0xb51733f3, 0x35294398, 0x2f128612, 0xaf2cf679,
 0x1b650d06, 0x9b5b7d6d, 0x8160b8e7, 0x015ec88c, 0x64c91fa3, 0xe4f76fc8,
 0xfeccaa42, 0x7ef2da29, 0xcabb2156, 0x4a85513d, 0x50be94b7, 0xd080e4dc,
 0xa254377e, 0x226a4715, 0x3851829f, 0xb86ff2f4, 0x0c26098b, 0x8c1879e0,
 0x9623bc6a, 0x161dcc01, 0xba182468, 0x3a265403, 0x201d9189, 0xa023e1e2,
 0x146a1a9d, 0x94546af6, 0x8e6faf7c, 0x0e51df17, 0x7c850cb5, 0xfcbb7cde,
 0xe680b954, 0x66bec93f, 0xd2f73240, 0x52c9422b, 0x48f287a1, 0xc8ccf7ca,
 0xad5b20e5, 0x2d65508e, 0x375e9504, 0xb760e56f, 0x03291e10, 0x83176e7b,
 0x992cabf1, 0x1912db9a, 0x6bc60838, 0xebf87853, 0xf1c3bdd9, 0x71fdcdb2,
 0xc5b436cd, 0x458a46a6, 0x5fb1832c, 0xdf8ff347, 0x949e2d72, 0x14a05d19,
 0x0e9b9893, 0x8ea5e8f8, 0x3aec1387, 0xbad263ec, 0xa0e9a666, 0x20d7d60d,
 0x520305af, 0xd23d75c4, 0xc806b04e, 0x4838c025, 0xfc713b5a, 0x7c4f4b31,
 0x66748ebb, 0xe64afed0, 0x83dd29ff, 0x03e35994, 0x19d89c1e, 0x99e6ec75,
 0x2daf170a, 0xad916761, 0xb7aaa2eb, 0x3794d280, 0x45400122, 0xc57e7149,
 0xdf45b4c3, 0x5f7bc4a8, 0xeb323fd7, 0x6b0c4fbc, 0x71378a36, 0xf109fa5d,
 0xe714365c, 0x672a4637, 0x7d1183bd, 0xfd2ff3d6, 0x496608a9, 0xc95878c2,
 0xd363bd48, 0x535dcd23, 0x21891e81, 0xa1b76eea, 0xbb8cab60, 0x3bb2db0b,
 0x8ffb2074, 0x0fc5501f, 0x15fe9595, 0x95c0e5fe, 0xf05732d1, 0x706942ba,
 0x6a528730, 0xea6cf75b, 0x5e250c24, 0xde1b7c4f, 0xc420b9c5, 0x441ec9ae,
 0x36ca1a0c, 0xb6f46a67, 0xaccfafed, 0x2cf1df86, 0x98b824f9, 0x18865492,
 0x02bd9118, 0x8283e173, 0xc9923f46, 0x49ac4f2d, 0x53978aa7, 0xd3a9facc,
 0x67e001b3, 0xe7de71d8, 0xfde5b452, 0x7ddbc439, 0x0f0f179b, 0x8f3167f0,
 0x950aa27a, 0x1534d211, 0xa17d296e, 0x21435905, 0x3b789c8f, 0xbb46ece4,
 0xded13bcb, 0x5eef4ba0, 0x44d48e2a, 0xc4eafe41, 0x70a3053e, 0xf09d7555,
 0xeaa6b0df, 0x6a98c0b4, 0x184c1316, 0x9872637d, 0x8249a6f7, 0x0277d69c,
 0xb63e2de3, 0x36005d88, 0x2c3b9802, 0xac05e869};

static unsigned _cairo_lfsr_random_state = 0x12345678;

static unsigned
lfsr_random(void)
{
    unsigned next;
    next  = _cairo_lfsr_random_lut[((_cairo_lfsr_random_state>> 0) & 0xFF) + 0*256];
    next ^= _cairo_lfsr_random_lut[((_cairo_lfsr_random_state>> 8) & 0xFF) + 1*256];
    next ^= _cairo_lfsr_random_lut[((_cairo_lfsr_random_state>>16) & 0xFF) + 2*256];
    next ^= _cairo_lfsr_random_lut[((_cairo_lfsr_random_state>>24) & 0xFF) + 3*256];
    return _cairo_lfsr_random_state = next;
}

/*
 * Initialize an empty skip list
 */
void
_cairo_skip_list_init (cairo_skip_list_t		*list,
		cairo_skip_list_compare_t	 compare,
		size_t			 elt_size)
{
    int i;

    list->compare = compare;
    list->elt_size = elt_size;
    list->data_size = elt_size - sizeof (skip_elt_t);

    for (i = 0; i < MAX_LEVEL; i++) {
	list->chains[i] = NULL;
    }

    for (i = 0; i < MAX_FREELIST_LEVEL; i++) {
	list->freelists[i] = NULL;
    }

    list->max_level = 0;
}

void
_cairo_skip_list_fini (cairo_skip_list_t *list)
{
    skip_elt_t *elt;
    int i;

    while ((elt = list->chains[0])) {
	_cairo_skip_list_delete_given (list, elt);
    }
    for (i=0; i<MAX_FREELIST_LEVEL; i++) {
	elt = list->freelists[i];
	while (elt) {
	    skip_elt_t *nextfree = elt->prev;
	    free (ELT_DATA(elt));
	    elt = nextfree;
	}
    }
}

/*
 * Generate a random level number, distributed
 * so that each level is 1/4 as likely as the one before
 *
 * Note that level numbers run 1 <= level < MAX_LEVEL
 */
static int
random_level (void)
{
    int	level = 0;
    /* tricky bit -- each bit is '1' 75% of the time.
     * This works because we only use the lower MAX_LEVEL
     * bits, and MAX_LEVEL < 16 */
    long int	bits = lfsr_random();
    bits |= bits >> 16;

    while (++level < MAX_LEVEL)
    {
	if (bits & 1)
	    break;
	bits >>= 1;
    }
    return level;
}

static void *
alloc_node_for_level (cairo_skip_list_t *list, unsigned level)
{
    int freelist_level = FREELIST_FOR_LEVEL (level);
    if (list->freelists[freelist_level]) {
	skip_elt_t *elt = list->freelists[freelist_level];
	list->freelists[freelist_level] = elt->prev;
	return ELT_DATA(elt);
    }
    return malloc (list->elt_size
		   + (FREELIST_MAX_LEVEL_FOR (level) - 1) * sizeof (skip_elt_t *));
}

static void
free_elt (cairo_skip_list_t *list, skip_elt_t *elt)
{
    int level = elt->prev_index + 1;
    int freelist_level = FREELIST_FOR_LEVEL (level);
    elt->prev = list->freelists[freelist_level];
    list->freelists[freelist_level] = elt;
}

/*
 * Insert 'data' into the list
 */
void *
_cairo_skip_list_insert (cairo_skip_list_t *list, void *data, int unique)
{
    skip_elt_t **update[MAX_LEVEL];
    skip_elt_t *prev[MAX_LEVEL];
    char *data_and_elt;
    skip_elt_t *elt, **next;
    int	    i, level, prev_index;

    /*
     * Find links along each chain
     */
    next = list->chains;
    for (i = list->max_level; --i >= 0; )
    {
	for (; (elt = next[i]); next = elt->next)
	{
	    int cmp = list->compare (list, ELT_DATA(elt), data);
	    if (unique && 0 == cmp)
		return ELT_DATA(elt);
	    if (cmp > 0)
		break;
	}
        update[i] = next;
	if (next != list->chains)
	    prev[i] = NEXT_TO_ELT (next);
	else
	    prev[i] = NULL;
    }
    level = random_level ();
    prev_index = level - 1;

    /*
     * Create new list element
     */
    if (level > list->max_level)
    {
	level = list->max_level + 1;
	prev_index = level - 1;
	prev[prev_index] = NULL;
	update[list->max_level] = list->chains;
	list->max_level = level;
    }

    data_and_elt = alloc_node_for_level (list, level);
    if (data_and_elt == NULL)
	return NULL;
    memcpy (data_and_elt, data, list->data_size);
    elt = (skip_elt_t *) (data_and_elt + list->data_size);

    elt->prev_index = prev_index;
    elt->prev = prev[prev_index];

    /*
     * Insert into all chains
     */
    for (i = 0; i < level; i++)
    {
	elt->next[i] = update[i][i];
	if (elt->next[i] && elt->next[i]->prev_index == i)
	    elt->next[i]->prev = elt;
	update[i][i] = elt;
    }

    return data_and_elt;
}

void *
_cairo_skip_list_find (cairo_skip_list_t *list, void *data)
{
    int i;
    skip_elt_t **next = list->chains;
    skip_elt_t *elt;

    /*
     * Walk chain pointers one level at a time
     */
    for (i = list->max_level; --i >= 0;)
	while (next[i] && list->compare (list, data, ELT_DATA(next[i])) > 0)
	{
	    next = next[i]->next;
	}
    /*
     * Here we are
     */
    elt = next[0];
    if (elt && list->compare (list, data, ELT_DATA (elt)) == 0)
	return ELT_DATA (elt);

    return NULL;
}

void
_cairo_skip_list_delete (cairo_skip_list_t *list, void *data)
{
    skip_elt_t **update[MAX_LEVEL], *prev[MAX_LEVEL];
    skip_elt_t *elt, **next;
    int	i;

    /*
     * Find links along each chain
     */
    next = list->chains;
    for (i = list->max_level; --i >= 0; )
    {
	for (; (elt = next[i]); next = elt->next)
	{
	    if (list->compare (list, ELT_DATA (elt), data) >= 0)
		break;
	}
        update[i] = &next[i];
	if (next == list->chains)
	    prev[i] = NULL;
	else
	    prev[i] = NEXT_TO_ELT (next);
    }
    elt = next[0];
    assert (list->compare (list, ELT_DATA (elt), data) == 0);
    for (i = 0; i < list->max_level && *update[i] == elt; i++) {
	*update[i] = elt->next[i];
	if (elt->next[i] && elt->next[i]->prev_index == i)
	    elt->next[i]->prev = prev[i];
    }
    while (list->max_level > 0 && list->chains[list->max_level - 1] == NULL)
	list->max_level--;
    free_elt (list, elt);
}

void
_cairo_skip_list_delete_given (cairo_skip_list_t *list, skip_elt_t *given)
{
    skip_elt_t **update[MAX_LEVEL], *prev[MAX_LEVEL];
    skip_elt_t *elt, **next;
    int	i;

    /*
     * Find links along each chain
     */
    if (given->prev)
	next = given->prev->next;
    else
	next = list->chains;
    for (i = given->prev_index + 1; --i >= 0; )
    {
	for (; (elt = next[i]); next = elt->next)
	{
	    if (elt == given)
		break;
	}
        update[i] = &next[i];
	if (next == list->chains)
	    prev[i] = NULL;
	else
	    prev[i] = NEXT_TO_ELT (next);
    }
    elt = next[0];
    assert (elt == given);
    for (i = 0; i < (given->prev_index + 1) && *update[i] == elt; i++) {
	*update[i] = elt->next[i];
	if (elt->next[i] && elt->next[i]->prev_index == i)
	    elt->next[i]->prev = prev[i];
    }
    while (list->max_level > 0 && list->chains[list->max_level - 1] == NULL)
	list->max_level--;
    free_elt (list, elt);
}
