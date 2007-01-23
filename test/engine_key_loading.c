
/*
 * TPM engine key loading tests.
 *
 * Kent Yoder <kyoder@users.sf.net>
 *
 */

#include <stdio.h>
#include <string.h>

#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <openssl/rand.h>

#include <tss/platform.h>
#include <tss/tcpa_defines.h>
#include <tss/tcpa_typedef.h>
#include <tss/tcpa_struct.h>
#include <tss/tss_defines.h>
#include <tss/tss_typedef.h>
#include <tss/tss_structs.h>
#include <tss/tss_error.h>
#include <tss/tspi.h>


#define ERR(x, ...)	fprintf(stderr, "%s:%d " x "\n", __FILE__, __LINE__, ##__VA_ARGS__)

char null_sha1_hash[] = { 0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
			  0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09 };

/* The tests assume that the SRK secret is a hash of 0 bytes */

struct eng_cmd
{
	char *name;
	long long_arg;
	void *void_arg;
};

/* Test setting the SRK plain password explicitly (there should be no prompt) */
struct eng_cmd post_test_pin_only = { "PIN", 0, NULL };
/* Test using a popup secret */
struct eng_cmd post_test_popup = { "SECRET_MODE", (long)TSS_SECRET_MODE_POPUP, NULL };
/* Test setting the mode to plain, then a NULL secret */
struct eng_cmd post_test_plain[] = { { "SECRET_MODE", (long)TSS_SECRET_MODE_PLAIN, NULL },
				      { "PIN", 0, NULL } };
/* Test passing in a SHA1 hashed secret */
struct eng_cmd post_test_sha1[] = { { "SECRET_MODE", (long)TSS_SECRET_MODE_SHA1, NULL },
				     { "PIN", 0, null_sha1_hash } };

struct eng_cmd *test_cmds[] = { &post_test_pin_only, post_test_plain, post_test_sha1,
				&post_test_popup };
int test_num[] = { 1, 1, 2, 2 };

#define DATA_SIZE	33
#define KEY_SIZE_BITS	512

int
run_test()
{
	RSA *rsa;
	char signature[256], data_to_sign[DATA_SIZE], data_recovered[DATA_SIZE];
	int sig_size;

	if (RAND_bytes(data_to_sign, sizeof(data_to_sign)) != 1) {
		ERR_print_errors_fp(stderr);
		return 1;
	}

	rsa = RSA_generate_key(KEY_SIZE_BITS, 65537, NULL, NULL);
	if (!rsa)
		return 1;

	if ((sig_size = RSA_public_encrypt(sizeof(data_to_sign), data_to_sign,
					   signature, rsa, RSA_PKCS1_PADDING)) == -1) {
		ERR_print_errors_fp(stderr);
		RSA_free(rsa);
		return 1;
	}

	if ((sig_size = RSA_private_decrypt(sig_size, signature, data_recovered,
					    rsa, RSA_PKCS1_PADDING)) != DATA_SIZE) {
		ERR_print_errors_fp(stderr);
		RSA_free(rsa);
		return 1;
	}

	if (memcmp(data_recovered, data_to_sign, DATA_SIZE)) {
		ERR("recovered data doesn't match!");
		RSA_free(rsa);
		return 1;
	}

	if ((sig_size = RSA_private_encrypt(sizeof(data_to_sign), data_to_sign,
					    signature, rsa, RSA_PKCS1_PADDING)) == -1) {
		ERR_print_errors_fp(stderr);
		RSA_free(rsa);
		return 1;
	}

	if ((sig_size = RSA_public_decrypt(sig_size, signature, data_recovered,
					    rsa, RSA_PKCS1_PADDING)) != DATA_SIZE) {
		ERR_print_errors_fp(stderr);
		RSA_free(rsa);
		return 1;
	}

	if (memcmp(data_recovered, data_to_sign, DATA_SIZE)) {
		ERR("recovered data doesn't match!");
		RSA_free(rsa);
		return 1;
	}

	RSA_free(rsa);

	return 0;
}

int
main()
{
	struct eng_cmd *post_cmds;
	int post_num, failure = 0, i;
	ENGINE *e;
        const char *engine_id = "tpm";


        ENGINE_load_builtin_engines();

	e = ENGINE_by_id(engine_id);
	if (!e) {
		/* the engine isn't available */
		ERR_print_errors_fp(stderr);
		ERR("ENGINE_by_id failed.");
		return 1;
	}

	if (!ENGINE_init(e)) {
		/* the engine couldn't initialise, release 'e' */
		ERR_print_errors_fp(stderr);
		ERR("ENGINE_init failed.");
		ENGINE_free(e);
		ENGINE_finish(e);
		return 2;
	}
	if (!ENGINE_set_default_RSA(e) || !ENGINE_set_default_RAND(e)) {
		/* This should only happen when 'e' can't initialise, but the previous
		 * statement suggests it did. */
		ERR_print_errors_fp(stderr);
		ERR("ENGINE_init failed.");
		ENGINE_free(e);
		ENGINE_finish(e);
		return 3;
	}

	/* ENGINE_init() returned a functional reference, so free the */
	/* structural reference with ENGINE_free */
	ENGINE_free(e);

	for (i = 0; i < 4 && !failure; i++) {
		post_cmds = test_cmds[i];
		post_num = test_num[i];
		/* Process post-initialize commands */
		while (post_num--) {
			if (!ENGINE_ctrl_cmd(e, post_cmds->name, post_cmds->long_arg,
					     post_cmds->void_arg, NULL, 0)) {
				ERR_print_errors_fp(stderr);
				ERR("Post command %d failed", i);
				failure = 1;
				ENGINE_finish(e);
				return 4;
			}
			post_cmds++;
		}
#if 0
		if (!ENGINE_load_private_key(e, key_paths[i], NULL, NULL)) {
			ERR_print_errors_fp(stderr);
			failure = 1;
			continue;
		}
#endif
		failure = run_test();
	}

	/* Release the functional reference from ENGINE_init() */
	ENGINE_finish(e);
	e = NULL;

	return failure ? 5 : 0;
}