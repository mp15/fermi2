#include <assert.h>
#include <pthread.h>
#include "rld0.h"

static inline void set_bit(uint64_t *bits, uint64_t k)
{
	uint64_t *p = &bits[k>>6];
	k = 1ull << (k&0x3f);
	__sync_or_and_fetch(p, k);
}

static void set_bits(const rld_t *e, const uint64_t *sub, uint64_t *bits, int start, int step)
{
	uint64_t i, k, *ok;
	int c;
	ok = alloca(8 * e->asize);
	for (i = start; i < e->mcnt[1]; i += step) {
		if ((sub[i>>6]>>(i&0x3f)&1) == 0) continue;
		for (k = i;;) {
			set_bit(bits, k);
			c = rld_rank1a(e, k, ok);
			if (c == 0) break;
			else k = e->cnt[c] + ok[c] - 1;
		}
	}
}

static rld_t *gen_idx(rld_t *e0, uint64_t *bits, int is_comp)
{
	int c = 0, c0 = -1;
	int64_t l, i, k = 0, len = 0;
	rld_t *e;
	rlditr_t ritr, witr;

	e = rld_init(e0->asize, e0->sbits);
	rld_itr_init(e, &witr, 0);
	rld_itr_init(e0, &ritr, 0);
	while ((l = rld_dec(e0, &ritr, &c, 1)) >= 0) {
		for (i = 0; i < l; ++i, ++k) {
			if ((bits[k>>6]>>(k&0x3f)&1) == !is_comp) {
				if (c != c0) {
					if (len) rld_enc(e, &witr, len, c0);
					c0 = c, len = 1;
				} else ++len;
			}
		}
	}
	if (len) rld_enc(e, &witr, len, c0);
	assert(k == e0->mcnt[0]);
	rld_destroy(e0);
	rld_enc_finish(e, &witr);
	return e;
}

typedef struct {
	const rld_t *e;
	const uint64_t *sub;
	uint64_t *bits;
	int n_threads;
} shared_t;

void kt_for(int n_threads, void (*func)(void*,int,int), void *data, int n);

static void worker(void *data, int i, int tid)
{
	shared_t *d = (shared_t*)data;
	set_bits(d->e, d->sub, d->bits, i, d->n_threads);
}

rld_t *fm_sub(rld_t *e, const uint64_t *sub, int n_threads, int is_comp)
{
	shared_t d;
	rld_t *r;
	d.bits = calloc((e->mcnt[0] + 63) / 64, 8);
	d.sub = sub, d.e = e, d.n_threads = n_threads;
	kt_for(n_threads, worker, &d, n_threads);
	r = gen_idx(e, d.bits, is_comp);
	free(d.bits);
	return r;
}

#include <unistd.h>

int main_sub(int argc, char *argv[])
{
	int c, is_comp = 0, n_threads = 1;
	rld_t *e;
	FILE *fp;
	uint64_t n_seqs, *sub;

	while ((c = getopt(argc, argv, "ct:")) >= 0) {
		if (c == 'c') is_comp = 1;
		else if (c == 't') n_threads = atoi(optarg);
	}
	if (optind + 2 >= argc) {
		fprintf(stderr, "Usage: fermi2 sub [-c] [-t nThreads=1] <reads.rld> <bits.bin>\n");
		return 1;
	}
	e = rld_restore(argv[optind]);
	fp = fopen(argv[optind+1], "rb");
	fread(&n_seqs, 8, 1, fp);
	if (n_seqs != e->mcnt[1]) {
		fprintf(stderr, "[E::%s] unmatched index and the bit array\n", __func__);
		rld_destroy(e);
		return 1;
	}
	sub = malloc((n_seqs + 63) / 64 * 8);
	fread(sub, 8, (n_seqs + 63) / 64, fp);
	fclose(fp);
	e = fm_sub(e, sub, n_threads, is_comp);
	free(sub);
	rld_dump(e, "-");
	rld_destroy(e);
	return 0;
}
