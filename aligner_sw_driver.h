#ifndef ALIGNER_SW_DRIVER_H_
#define ALIGNER_SW_DRIVER_H_
#include <utility>
#include "ds.h"
#include "aligner_seed.h"
#include "aligner_sw.h"
#include "aligner_cache.h"
#include "reference.h"
#include "group_walk.h"
#include "aln_idx.h"
#include "mem_ids.h"
#include "aln_sink.h"
#include "pe.h"
#include "ival_list.h"
#include "simple_func.h"
#include "random_util.h"
struct SeedPos
{
	SeedPos() : fw(false), offidx(0), rdoff(0), seedlen(0) {}
	SeedPos(
		bool fw_,
		uint32_t offidx_,
		uint32_t rdoff_,
		uint32_t seedlen_)
	{
		init(fw_, offidx_, rdoff_, seedlen_);
	}
	void init(
		bool fw_,
		uint32_t offidx_,
		uint32_t rdoff_,
		uint32_t seedlen_)
	{
		fw = fw_;
		offidx = offidx_;
		rdoff = rdoff_;
		seedlen = seedlen_;
	}
	bool operator<(const SeedPos &o) const
	{
		if (offidx < o.offidx)
			return true;
		if (offidx > o.offidx)
			return false;
		if (rdoff < o.rdoff)
			return true;
		if (rdoff > o.rdoff)
			return false;
		if (seedlen < o.seedlen)
			return true;
		if (seedlen > o.seedlen)
			return false;
		if (fw && !o.fw)
			return true;
		if (!fw && o.fw)
			return false;
		return false;
	}
	bool operator>(const SeedPos &o) const
	{
		if (offidx < o.offidx)
			return false;
		if (offidx > o.offidx)
			return true;
		if (rdoff < o.rdoff)
			return false;
		if (rdoff > o.rdoff)
			return true;
		if (seedlen < o.seedlen)
			return false;
		if (seedlen > o.seedlen)
			return true;
		if (fw && !o.fw)
			return false;
		if (!fw && o.fw)
			return true;
		return false;
	}
	bool operator==(const SeedPos &o) const
	{
		return fw == o.fw && offidx == o.offidx &&
			   rdoff == o.rdoff && seedlen == o.seedlen;
	}
	bool fw;
	uint32_t offidx;
	uint32_t rdoff;
	uint32_t seedlen;
};
struct SATupleAndPos
{
	SATuple sat;
	SeedPos pos;
	size_t origSz;
	size_t nlex;
	size_t nrex;
	bool operator<(const SATupleAndPos &o) const
	{
		if (sat < o.sat)
			return true;
		if (sat > o.sat)
			return false;
		return pos < o.pos;
	}
	bool operator==(const SATupleAndPos &o) const
	{
		return sat == o.sat && pos == o.pos;
	}
};
class RowSampler
{
public:
	RowSampler(int cat = 0) : elim_(cat), masses_(cat)
	{
		mass_ = 0.0f;
	}
	void init(
		const EList<SATupleAndPos, 16> &salist,
		size_t sai,
		size_t saf,
		bool lensq,
		bool szsq)
	{
		assert_gt(saf, sai);
		elim_.resize(saf - sai);
		elim_.fill(false);
		mass_ = 0.0f;
		masses_.resize(saf - sai);
		for (size_t i = sai; i < saf; i++)
		{
			size_t len = salist[i].nlex + salist[i].nrex + 1;
			double num = (double)len;
			if (lensq)
			{
				num *= num;
			}
			double denom = (double)salist[i].sat.size();
			if (szsq)
			{
				denom *= denom;
			}
			masses_[i - sai] = num / denom;
			mass_ += masses_[i - sai];
		}
	}
	void finishedRange(size_t i)
	{
		assert_lt(i, masses_.size());
		elim_[i] = true;
		mass_ -= masses_[i];
	}
	size_t next(RandomSource &rnd)
	{
		double rd = rnd.nextFloat() * mass_;
		double mass_sofar = 0.0f;
		size_t sz = masses_.size();
		size_t last_unelim = std::numeric_limits<size_t>::max();
		for (size_t i = 0; i < sz; i++)
		{
			if (!elim_[i])
			{
				last_unelim = i;
				mass_sofar += masses_[i];
				if (rd < mass_sofar)
				{
					return i;
				}
			}
		}
		assert_neq(std::numeric_limits<size_t>::max(), last_unelim);
		return last_unelim;
	}
protected:
	double mass_;
	EList<bool> elim_;
	EList<double> masses_;
};
enum
{
	EXTEND_EXHAUSTED_CANDIDATES = 1,
	EXTEND_POLICY_FULFILLED,
	EXTEND_PERFECT_SCORE,
	EXTEND_EXCEEDED_SOFT_LIMIT,
	EXTEND_EXCEEDED_HARD_LIMIT
};
struct ExtendRange
{
	void init(size_t off_, size_t len_, size_t sz_)
	{
		off = off_;
		len = len_;
		sz = sz_;
	}
	size_t off;
	size_t len;
	size_t sz;
};
class SwDriver
{
	typedef PList<TIndexOffU, CACHE_PAGE_SZ> TSAList;
public:
	SwDriver(size_t bytes) : satups_(DP_CAT),
							 gws_(DP_CAT),
							 seenDiags1_(DP_CAT),
							 seenDiags2_(DP_CAT),
							 redAnchor_(DP_CAT),
							 redMate1_(DP_CAT),
							 redMate2_(DP_CAT),
							 pool_(bytes, CACHE_PAGE_SZ, DP_CAT),
							 salistEe_(DP_CAT),
							 gwstate_(GW_CAT) {}
	int extendSeeds(
		Read &rd,
		bool mate1,
		SeedResults &sh,
		const Ebwt &ebwtFw,
		const Ebwt *ebwtBw,
		const BitPairReference &ref,
		SwAligner &swa,
		const Scoring &sc,
		int seedmms,
		int seedlen,
		int seedival,
		TAlScore &minsc,
		int nceil,
		size_t maxhalf,
		bool doUngapped,
		size_t maxIters,
		size_t maxUg,
		size_t maxDp,
		size_t maxUgStreak,
		size_t maxDpStreak,
		bool doExtend,
		bool enable8,
		size_t cminlen,
		size_t cpow2,
		bool doTri,
		int tighten,
		AlignmentCacheIface &ca,
		RandomSource &rnd,
		WalkMetrics &wlm,
		SwMetrics &swmSeed,
		PerReadMetrics &prm,
		AlnSinkWrap *mhs,
		bool reportImmediately,
		bool &exhaustive);
	int extendSeedsPaired(
		Read &rd,
		Read &ord,
		bool anchor1,
		bool oppFilt,
		SeedResults &sh,
		const Ebwt &ebwtFw,
		const Ebwt *ebwtBw,
		const BitPairReference &ref,
		SwAligner &swa,
		SwAligner &swao,
		const Scoring &sc,
		const PairedEndPolicy &pepol,
		int seedmms,
		int seedlen,
		int seedival,
		TAlScore &minsc,
		TAlScore &ominsc,
		int nceil,
		int onceil,
		bool nofw,
		bool norc,
		size_t maxhalf,
		bool doUngapped,
		size_t maxIters,
		size_t maxUg,
		size_t maxDp,
		size_t maxEeStreak,
		size_t maxUgStreak,
		size_t maxDpStreak,
		size_t maxMateStreak,
		bool doExtend,
		bool enable8,
		size_t cminlen,
		size_t cpow2,
		bool doTri,
		int tighten,
		AlignmentCacheIface &cs,
		RandomSource &rnd,
		WalkMetrics &wlm,
		SwMetrics &swmSeed,
		SwMetrics &swmMate,
		PerReadMetrics &prm,
		AlnSinkWrap *msink,
		bool swMateImmediately,
		bool reportImmediately,
		bool discord,
		bool mixed,
		bool &exhaustive);
	void nextRead(bool paired, size_t mate1len, size_t mate2len)
	{
		redAnchor_.reset();
		seenDiags1_.reset();
		seenDiags2_.reset();
		seedExRangeFw_[0].clear();
		seedExRangeFw_[1].clear();
		seedExRangeRc_[0].clear();
		seedExRangeRc_[1].clear();
		size_t maxlen = mate1len;
		if (paired)
		{
			redMate1_.reset();
			redMate1_.init(mate1len);
			redMate2_.reset();
			redMate2_.init(mate2len);
			if (mate2len > maxlen)
			{
				maxlen = mate2len;
			}
		}
		redAnchor_.init(maxlen);
	}
protected:
	bool eeSaTups(
		const Read &rd,
		SeedResults &sh,
		const Ebwt &ebwt,
		const BitPairReference &ref,
		RandomSource &rnd,
		WalkMetrics &wlm,
		SwMetrics &swmSeed,
		size_t &nelt_out,
		size_t maxelts,
		bool all);
	void extend(
		const Read &rd,
		const Ebwt &ebwtFw,
		const Ebwt *ebwtBw,
		TIndexOffU topf,
		TIndexOffU botf,
		TIndexOffU topb,
		TIndexOffU botb,
		bool fw,
		size_t off,
		size_t len,
		PerReadMetrics &prm,
		size_t &nlex,
		size_t &nrex);
	void prioritizeSATups(
		const Read &rd,
		SeedResults &sh,
		const Ebwt &ebwtFw,
		const Ebwt *ebwtBw,
		const BitPairReference &ref,
		int seedmms,
		size_t maxelt,
		bool doExtend,
		bool lensq,
		bool szsq,
		size_t nsm,
		AlignmentCacheIface &ca,
		RandomSource &rnd,
		WalkMetrics &wlm,
		PerReadMetrics &prm,
		size_t &nelt_out,
		bool all);
	Random1toN rand_;
	EList<Random1toN, 16> rands_;
	EList<Random1toN, 16> rands2_;
	EList<EEHit, 16> eehits_;
	EList<SATupleAndPos, 16> satpos_;
	EList<SATupleAndPos, 16> satpos2_;
	EList<SATuple, 16> satups_;
	EList<GroupWalk2S<TSlice, 16>> gws_;
	EList<size_t> mateStreaks_;
	RowSampler rowsamp_;
	EList<ExtendRange> seedExRangeFw_[2];
	EList<ExtendRange> seedExRangeRc_[2];
	EIvalMergeListBinned seenDiags1_;
	EIvalMergeListBinned seenDiags2_;
	RedundantAlns redAnchor_;
	RedundantAlns redMate1_;
	RedundantAlns redMate2_;
	SwResult resGap_;
	SwResult oresGap_;
	SwResult resUngap_;
	SwResult oresUngap_;
	SwResult resEe_;
	SwResult oresEe_;
	Pool pool_;
	TSAList salistEe_;
	GroupWalkState gwstate_;
	ASSERT_ONLY(SStringExpandable<char> raw_refbuf_);
	ASSERT_ONLY(SStringExpandable<uint32_t> raw_destU32_);
	ASSERT_ONLY(EList<bool> raw_matches_);
	ASSERT_ONLY(BTDnaString tmp_rf_);
	ASSERT_ONLY(BTDnaString tmp_rdseq_);
	ASSERT_ONLY(BTString tmp_qseq_);
};
#endif
