#include <cmath>
#include <stdio.h>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <string.h>
#include <fcntl.h>
#include "sstring.h"
#include "pat.h"
#include "filebuf.h"
#include "formats.h"
#include "util.h"
#include "str_util.h"
#include "tokenize.h"
using namespace std;
static uint32_t genRandSeed(
	const BTDnaString &qry,
	const BTString &qual,
	const BTString &name,
	uint32_t seed)
{
	uint32_t rseed = (seed + 101) * 59 * 61 * 67 * 71 * 73 * 79 * 83;
	size_t qlen = qry.length();
	for (size_t i = 0; i < qlen; i++)
	{
		int p = (int)qry[i];
		assert_leq(p, 4);
		size_t off = ((i & 15) << 1);
		rseed ^= ((uint32_t)p << off);
	}
	for (size_t i = 0; i < qlen; i++)
	{
		int p = (int)qual[i];
		assert_leq(p, 255);
		size_t off = ((i & 3) << 3);
		rseed ^= (p << off);
	}
	size_t namelen = name.length();
	for (size_t i = 0; i < namelen; i++)
	{
		int p = (int)name[i];
		if (p == '/')
			break;
		assert_leq(p, 255);
		size_t off = ((i & 3) << 3);
		rseed ^= (p << off);
	}
	return rseed;
}
PatternSource *PatternSource::patsrcFromStrings(
	const PatternParams &p,
	const EList<string> &qs)
{
	switch (p.format)
	{
	case FASTA:
		return new FastaPatternSource(qs, p, p.interleaved);
	case FASTA_CONT:
		return new FastaContinuousPatternSource(qs, p);
	case RAW:
		return new RawPatternSource(qs, p);
	case FASTQ:
		return new FastqPatternSource(qs, p, p.interleaved);
	case BAM:
		return new BAMPatternSource(qs, p);
	case TAB_MATE5:
		return new TabbedPatternSource(qs, p, false);
	case TAB_MATE6:
		return new TabbedPatternSource(qs, p, true);
	case CMDLINE:
		return new VectorPatternSource(qs, p);
	case QSEQ:
		return new QseqPatternSource(qs, p);
#ifdef USE_SRA
	case SRA_FASTA:
	case SRA_FASTQ:
		return new SRAPatternSource(qs, p);
#endif
	default:
	{
		cerr << "Internal error; bad patsrc format: " << p.format << endl;
		throw 1;
	}
	}
}
void PatternSourcePerThread::finalize(Read &ra)
{
	ra.mate = 1;
	ra.rdid = buf_.rdid();
	ra.seed = genRandSeed(ra.patFw, ra.qual, ra.name, pp_.seed);
	ra.finalize();
	if (pp_.fixName)
	{
		ra.fixMateName(1);
	}
}
void PatternSourcePerThread::finalizePair(Read &ra, Read &rb)
{
	ra.mate = 1;
	rb.mate = 2;
	ra.rdid = rb.rdid = buf_.rdid();
	ra.seed = genRandSeed(ra.patFw, ra.qual, ra.name, pp_.seed);
	rb.seed = genRandSeed(rb.patFw, rb.qual, rb.name, pp_.seed);
	ra.finalize();
	rb.finalize();
	if (pp_.fixName)
	{
		ra.fixMateName(1);
		rb.fixMateName(2);
	}
}
pair<bool, bool> PatternSourcePerThread::nextReadPair()
{
	if (buf_.exhausted())
	{
		pair<bool, int> res = nextBatch();
		if (res.first && res.second == 0)
		{
			return make_pair(false, true);
		}
		last_batch_ = res.first;
		last_batch_size_ = res.second;
		assert_eq(0, buf_.cur_buf_);
	}
	else
	{
		buf_.next();
		assert_gt(buf_.cur_buf_, 0);
	}
	assert(!buf_.read_a().readOrigBuf.empty());
	assert(buf_.read_a().empty());
	if (!parse(buf_.read_a(), buf_.read_b()))
	{
		return make_pair(false, false);
	}
	if (!buf_.read_b().patFw.empty())
	{
		trim(buf_.read_a());
		trim(buf_.read_b());
		finalizePair(buf_.read_a(), buf_.read_b());
	}
	else
	{
		trim(buf_.read_a());
		finalize(buf_.read_a());
	}
	bool this_is_last = buf_.cur_buf_ == static_cast<unsigned int>(last_batch_size_ - 1);
	return make_pair(true, this_is_last ? last_batch_ : false);
}
pair<bool, int> SoloPatternComposer::nextBatch(PerThreadReadBuf &pt)
{
	size_t cur = cur_;
	while (cur < src_->size())
	{
		pair<bool, int> res;
		do
		{
			res = (*src_)[cur]->nextBatch(
				pt,
				true,
				lock_);
		} while (!res.first && res.second == 0);
		if (res.second == 0)
		{
			ThreadSafe ts(mutex_m);
			if (cur + 1 > cur_)
			{
				cur_++;
			}
			cur = cur_;
			continue;
		}
		return res;
	}
	assert_leq(cur, src_->size());
	return make_pair(true, 0);
}
pair<bool, int> DualPatternComposer::nextBatch(PerThreadReadBuf &pt)
{
	size_t cur = cur_;
	while (cur < srca_->size())
	{
		if ((*srcb_)[cur] == NULL)
		{
			pair<bool, int> res = (*srca_)[cur]->nextBatch(
				pt,
				true,
				lock_);
			if (res.second == 0 && cur < srca_->size() - 1)
			{
				ThreadSafe ts(mutex_m);
				if (cur + 1 > cur_)
					cur_++;
				cur = cur_;
				continue;
			}
			return make_pair(res.first && cur == srca_->size() - 1, res.second);
		}
		else
		{
			pair<bool, int> resa, resb;
			{
				ThreadSafe ts(mutex_m);
				resa = (*srca_)[cur]->nextBatch(
					pt,
					true,
					false);
				resb = (*srcb_)[cur]->nextBatch(
					pt,
					false,
					false);
				assert_eq((*srca_)[cur]->readCount(),
						  (*srcb_)[cur]->readCount());
			}
			if (resa.second < resb.second)
			{
				cerr << "Error, fewer reads in file specified with -1 "
					 << "than in file specified with -2" << endl;
				throw 1;
			}
			else if (resa.second == 0 && resb.second == 0)
			{
				ThreadSafe ts(mutex_m);
				if (cur + 1 > cur_)
				{
					cur_++;
				}
				cur = cur_;
				continue;
			}
			else if (resb.second < resa.second)
			{
				cerr << "Error, fewer reads in file specified with -2 "
					 << "than in file specified with -1" << endl;
				throw 1;
			}
			assert_eq(resa.first, resb.first);
			assert_eq(resa.second, resb.second);
			return make_pair(resa.first && cur == srca_->size() - 1, resa.second);
		}
	}
	assert_leq(cur, srca_->size());
	return make_pair(true, 0);
}
PatternComposer *PatternComposer::setupPatternComposer(
	const EList<string> &si,
	const EList<string> &m1,
	const EList<string> &m2,
	const EList<string> &m12,
	const EList<string> &q,
	const EList<string> &q1,
	const EList<string> &q2,
#ifdef USE_SRA
	const EList<string> &sra_accs,
#endif
	PatternParams &p,
	bool verbose)
{
	EList<PatternSource *> *a = new EList<PatternSource *>();
	EList<PatternSource *> *b = new EList<PatternSource *>();
	for (size_t i = 0; i < m12.size(); i++)
	{
		const EList<string> *qs = &m12;
		EList<string> tmp;
		if (p.fileParallel)
		{
			qs = &tmp;
			tmp.push_back(m12[i]);
			assert_eq(1, tmp.size());
		}
		a->push_back(PatternSource::patsrcFromStrings(p, *qs));
		b->push_back(NULL);
		p.interleaved = false;
		if (!p.fileParallel)
		{
			break;
		}
	}
#ifdef USE_SRA
	for (size_t i = 0; i < sra_accs.size(); i++)
	{
		const EList<string> *qs = &sra_accs;
		EList<string> tmp;
		if (p.fileParallel)
		{
			qs = &tmp;
			tmp.push_back(sra_accs[i]);
			assert_eq(1, tmp.size());
		}
		a->push_back(PatternSource::patsrcFromStrings(p, *qs));
		b->push_back(NULL);
		if (!p.fileParallel)
		{
			break;
		}
	}
#endif
	for (size_t i = 0; i < m1.size(); i++)
	{
		const EList<string> *qs = &m1;
		EList<string> tmpSeq;
		EList<string> tmpQual;
		if (p.fileParallel)
		{
			qs = &tmpSeq;
			tmpSeq.push_back(m1[i]);
			assert_eq(1, tmpSeq.size());
		}
		a->push_back(PatternSource::patsrcFromStrings(p, *qs));
		if (!p.fileParallel)
		{
			break;
		}
	}
	for (size_t i = 0; i < m2.size(); i++)
	{
		const EList<string> *qs = &m2;
		EList<string> tmpSeq;
		EList<string> tmpQual;
		if (p.fileParallel)
		{
			qs = &tmpSeq;
			tmpSeq.push_back(m2[i]);
			assert_eq(1, tmpSeq.size());
		}
		b->push_back(PatternSource::patsrcFromStrings(p, *qs));
		if (!p.fileParallel)
		{
			break;
		}
	}
	assert_eq(a->size(), b->size());
	for (size_t i = 0; i < si.size(); i++)
	{
		const EList<string> *qs = &si;
		PatternSource *patsrc = NULL;
		EList<string> tmpSeq;
		EList<string> tmpQual;
		if (p.fileParallel)
		{
			qs = &tmpSeq;
			tmpSeq.push_back(si[i]);
			assert_eq(1, tmpSeq.size());
		}
		patsrc = PatternSource::patsrcFromStrings(p, *qs);
		assert(patsrc != NULL);
		a->push_back(patsrc);
		b->push_back(NULL);
		if (!p.fileParallel)
		{
			break;
		}
	}
	PatternComposer *patsrc = NULL;
	patsrc = new DualPatternComposer(a, b, p);
	return patsrc;
}
void PatternComposer::free_EList_pmembers(const EList<PatternSource *> &elist)
{
	for (size_t i = 0; i < elist.size(); i++)
		if (elist[i] != NULL)
			delete elist[i];
}
pair<bool, int> CFilePatternSource::nextBatchImpl(
	PerThreadReadBuf &pt,
	bool batch_a)
{
	bool done = false;
	unsigned nread = 0;
	pt.setReadId(readCnt_);
	while (true)
	{
		do
		{
			pair<bool, int> ret = nextBatchFromFile(pt, batch_a, nread);
			done = ret.first;
			nread = ret.second;
		} while (!done && nread == 0);
		if (done && filecur_ < infiles_.size())
		{
			open();
			resetForNextFile();
			filecur_++;
			if (nread == 0 || (nread < pt.max_buf_))
			{
				continue;
			}
			done = false;
		}
		break;
	}
	assert_geq(nread, 0);
	readCnt_ += nread;
	return make_pair(done, nread);
}
pair<bool, int> CFilePatternSource::nextBatch(
	PerThreadReadBuf &pt,
	bool batch_a,
	bool lock)
{
	if (lock)
	{
		ThreadSafe ts(mutex);
		return nextBatchImpl(pt, batch_a);
	}
	else
	{
		return nextBatchImpl(pt, batch_a);
	}
}
void CFilePatternSource::open()
{
	if (is_open_)
	{
		is_open_ = false;
		if (compressed_)
		{
			gzclose(zfp_);
			zfp_ = NULL;
		}
		else
		{
			fclose(fp_);
			fp_ = NULL;
		}
	}
	while (filecur_ < infiles_.size())
	{
		if (infiles_[filecur_] == "-")
		{
			compressed_ = true;
			int fd = dup(fileno(stdin));
			zfp_ = gzdopen(fd, "rb");
			if (zfp_ == NULL)
			{
				close(fd);
			}
		}
		else
		{
			const char *filename = infiles_[filecur_].c_str();
			int fd = ::open(filename, O_RDONLY);
			bool is_fifo = false;
#ifndef _WIN32
			struct stat st;
			if (fstat(fd, &st) != 0)
			{
				perror("stat");
			}
			is_fifo = S_ISFIFO(st.st_mode) != 0;
#endif
			if (pp_.format != BAM && (is_fifo || is_gzipped_file(fd)))
			{
				zfp_ = gzdopen(fd, "r");
				compressed_ = true;
			}
			else
			{
				fp_ = fdopen(fd, "rb");
			}
			if ((compressed_ && zfp_ == NULL) || (!compressed_ && fp_ == NULL))
			{
				if (fd != -1)
				{
					close(fd);
				}
				if (!errs_[filecur_])
				{
					cerr << "Warning: Could not open read file \""
						 << filename
						 << "\" for reading; skipping..." << endl;
					errs_[filecur_] = true;
				}
				filecur_++;
				continue;
			}
		}
		is_open_ = true;
		if (compressed_)
		{
#if ZLIB_VERNUM < 0x1235
			cerr << "Warning: gzbuffer added in zlib v1.2.3.5. Unable to change "
					"buffer size from default of 8192."
				 << endl;
#else
			gzbuffer(zfp_, 128 * 1024);
#endif
		}
		else
		{
			setvbuf(fp_, buf_, _IOFBF, 64 * 1024);
		}
		return;
	}
	cerr << "Error: No input read files were valid" << endl;
	exit(1);
	return;
}
VectorPatternSource::VectorPatternSource(
	const EList<string> &seqs,
	const PatternParams &p) : PatternSource(p),
							  cur_(p.skip),
							  skip_(p.skip),
							  paired_(false),
							  tokbuf_(),
							  bufs_()
{
	const size_t seqslen = seqs.size();
	for (size_t i = 0; i < seqslen; i++)
	{
		tokbuf_.clear();
		tokenize(seqs[i], ":", tokbuf_, 2);
		assert_gt(tokbuf_.size(), 0);
		assert_leq(tokbuf_.size(), 2);
		bufs_.expand();
		bufs_.back().clear();
		itoa10<TReadId>(static_cast<TReadId>(i), nametmp_);
		bufs_.back().install(nametmp_);
		bufs_.back().append('\t');
		bufs_.back().append(tokbuf_[0].c_str());
		bufs_.back().append('\t');
		if (tokbuf_.size() > 1)
		{
			bufs_.back().append(tokbuf_[1].c_str());
		}
		else
		{
			const size_t len = tokbuf_[0].length();
			for (size_t i = 0; i < len; i++)
			{
				bufs_.back().append('I');
			}
		}
	}
}
pair<bool, int> VectorPatternSource::nextBatchImpl(
	PerThreadReadBuf &pt,
	bool batch_a)
{
	pt.setReadId(cur_);
	EList<Read> &readbuf = batch_a ? pt.bufa_ : pt.bufb_;
	size_t readi = 0;
	for (; readi < pt.max_buf_ && cur_ < bufs_.size(); readi++, cur_++)
	{
		readbuf[readi].readOrigBuf = bufs_[cur_];
	}
	readCnt_ += readi;
	return make_pair(cur_ == bufs_.size(), readi);
}
pair<bool, int> VectorPatternSource::nextBatch(
	PerThreadReadBuf &pt,
	bool batch_a,
	bool lock)
{
	if (lock)
	{
		ThreadSafe ts(mutex);
		return nextBatchImpl(pt, batch_a);
	}
	else
	{
		return nextBatchImpl(pt, batch_a);
	}
}
bool VectorPatternSource::parse(Read &ra, Read &rb, TReadId rdid) const
{
	assert(ra.empty());
	assert(!ra.readOrigBuf.empty());
	int c = '\t';
	size_t cur = 0;
	const size_t buflen = ra.readOrigBuf.length();
	for (int endi = 0; endi < 2 && c == '\t'; endi++)
	{
		Read &r = ((endi == 0) ? ra : rb);
		assert(r.name.empty());
		if (endi < 1 || paired_)
		{
			c = ra.readOrigBuf[cur++];
			while (c != '\t' && cur < buflen)
			{
				r.name.append(c);
				c = ra.readOrigBuf[cur++];
			}
			assert_eq('\t', c);
			if (cur >= buflen)
			{
				return false;
			}
		}
		else if (endi > 0)
		{
			rb.name = ra.name;
		}
		assert(r.patFw.empty());
		c = ra.readOrigBuf[cur++];
		int nchar = 0;
		while (c != '\t' && cur < buflen)
		{
			if (isalpha(c))
			{
				assert_in(toupper(c), "ACGTN");
				if (nchar++ >= pp_.trim5)
				{
					assert_neq(0, asc2dnacat[c]);
					r.patFw.append(asc2dna[c]);
				}
			}
			c = ra.readOrigBuf[cur++];
		}
		assert_eq('\t', c);
		if (cur >= buflen)
		{
			return false;
		}
		r.trimmed5 = (int)(nchar - r.patFw.length());
		r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
		assert(r.qual.empty());
		c = ra.readOrigBuf[cur++];
		int nqual = 0;
		while (c != '\t' && c != '\n' && c != '\r')
		{
			if (c == ' ')
			{
				wrongQualityFormat(r.name);
				return false;
			}
			char cadd = charToPhred33(c, false, false);
			if (++nqual > pp_.trim5)
			{
				r.qual.append(cadd);
			}
			if (cur >= buflen)
				break;
			c = ra.readOrigBuf[cur++];
		}
		if (nchar > nqual)
		{
			tooFewQualities(r.name);
			return false;
		}
		else if (nqual > nchar)
		{
			tooManyQualities(r.name);
			return false;
		}
		r.qual.trimEnd(pp_.trim3);
		assert(c == '\t' || c == '\n' || c == '\r' || cur >= buflen);
		assert_eq(r.patFw.length(), r.qual.length());
	}
	ra.parsed = true;
	if (!rb.parsed && !rb.readOrigBuf.empty())
	{
		return parse(rb, ra, rdid);
	}
	return true;
}
pair<bool, int> FastaPatternSource::nextBatchFromFile(
	PerThreadReadBuf &pt,
	bool batch_a, unsigned readi)
{
	int c;
	EList<Read> *readbuf = batch_a ? &pt.bufa_ : &pt.bufb_;
	if (first_)
	{
		c = getc_wrapper();
		if (c == EOF)
		{
			return make_pair(true, 0);
		}
		while (c == '\r' || c == '\n')
		{
			c = getc_wrapper();
		}
		if (c != '>')
		{
			cerr << "Error: reads file does not look like a FASTA file" << endl;
			throw 1;
		}
		first_ = false;
	}
	bool done = false;
	while (readi < pt.max_buf_ && !done)
	{
		Read::TBuf &buf = (*readbuf)[readi].readOrigBuf;
		buf.clear();
		buf.append('>');
		while (true)
		{
			c = getc_wrapper();
			if (c < 0 || c == '>')
			{
				done = c < 0;
				break;
			}
			buf.append(c);
		}
		if (interleaved_)
		{
			batch_a = !batch_a;
			readbuf = batch_a ? &pt.bufa_ : &pt.bufb_;
			readi = batch_a ? readi + 1 : readi;
		}
		else
		{
			readi++;
		}
	}
	if (done && (*readbuf)[readi - 1].readOrigBuf.length() == 1)
	{
		readi--;
	}
	return make_pair(done, readi);
}
bool FastaPatternSource::parse(Read &r, Read &rb, TReadId rdid) const
{
	assert(!r.readOrigBuf.empty());
	assert(r.empty());
	int c = -1;
	size_t cur = 1;
	const size_t buflen = r.readOrigBuf.length();
	assert(r.name.empty());
	while (cur < buflen)
	{
		c = r.readOrigBuf[cur++];
		if (c == '\n' || c == '\r')
		{
			do
			{
				c = r.readOrigBuf[cur++];
			} while ((c == '\n' || c == '\r') && cur < buflen);
			break;
		}
		r.name.append(c);
	}
	if (cur >= buflen)
	{
		return false;
	}
	int nchar = 0;
	assert(r.patFw.empty());
	assert(c != '\n' && c != '\r');
	assert_lt(cur, buflen);
	while (cur < buflen)
	{
		if (c == '.')
		{
			c = 'N';
		}
		if (isalpha(c))
		{
			if (nchar++ >= pp_.trim5)
			{
				r.patFw.append(asc2dna[c]);
			}
		}
		assert_lt(cur, buflen);
		c = r.readOrigBuf[cur++];
		if ((c == '\n' || c == '\r') && cur < buflen && r.readOrigBuf[cur] != '>')
		{
			c = r.readOrigBuf[cur++];
		}
	}
	r.trimmed5 = (int)(nchar - r.patFw.length());
	r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
	for (size_t i = 0; i < r.patFw.length(); i++)
	{
		r.qual.append('I');
	}
	if (r.name.empty())
	{
		char cbuf[20];
		itoa10<TReadId>(static_cast<TReadId>(rdid), cbuf);
		r.name.install(cbuf);
	}
	r.parsed = true;
	if (!rb.parsed && !rb.readOrigBuf.empty())
	{
		return parse(rb, r, rdid);
	}
	return true;
}
pair<bool, int> FastaContinuousPatternSource::nextBatchFromFile(
	PerThreadReadBuf &pt,
	bool batch_a, unsigned readi)
{
	int c = -1;
	EList<Read> &readbuf = batch_a ? pt.bufa_ : pt.bufb_;
	while (readi < pt.max_buf_)
	{
		c = getc_wrapper();
		if (c < 0)
		{
			break;
		}
		if (c == '>')
		{
			resetForNextFile();
			c = getc_wrapper();
			bool sawSpace = false;
			while (c != '\n' && c != '\r')
			{
				if (!sawSpace)
				{
					sawSpace = isspace(c);
				}
				if (!sawSpace)
				{
					name_prefix_buf_.append(c);
				}
				c = getc_wrapper();
			}
			while (c == '\n' || c == '\r')
			{
				c = getc_wrapper();
			}
			if (c < 0)
			{
				break;
			}
			name_prefix_buf_.append('_');
		}
		int cat = asc2dnacat[c];
		if (cat >= 2)
			c = 'N';
		if (cat == 0)
		{
			continue;
		}
		else
		{
			buf_[bufCur_++] = c;
			if (bufCur_ == 1024)
			{
				bufCur_ = 0;
			}
			if (eat_ > 0)
			{
				eat_--;
				if (!beginning_)
				{
					cur_++;
				}
				continue;
			}
			readbuf[readi].readOrigBuf = name_prefix_buf_;
			itoa10<TReadId>(cur_ - last_, name_int_buf_);
			readbuf[readi].readOrigBuf.append(name_int_buf_);
			readbuf[readi].readOrigBuf.append('\t');
			for (size_t i = 0; i < length_; i++)
			{
				if (length_ - i <= bufCur_)
				{
					c = buf_[bufCur_ - (length_ - i)];
				}
				else
				{
					c = buf_[bufCur_ - (length_ - i) + 1024];
				}
				readbuf[readi].readOrigBuf.append(c);
			}
			eat_ = freq_ - 1;
			cur_++;
			beginning_ = false;
			readi++;
		}
	}
	return make_pair(c < 0, readi);
}
bool FastaContinuousPatternSource::parse(
	Read &ra,
	Read &rb,
	TReadId rdid) const
{
	assert(ra.empty());
	assert(rb.empty());
	assert(!ra.readOrigBuf.empty());
	assert(rb.readOrigBuf.empty());
	int c = '\t';
	size_t cur = 0;
	const size_t buflen = ra.readOrigBuf.length();
	c = ra.readOrigBuf[cur++];
	while (c != '\t' && cur < buflen)
	{
		ra.name.append(c);
		c = ra.readOrigBuf[cur++];
	}
	assert_eq('\t', c);
	if (cur >= buflen)
	{
		return false;
	}
	assert(ra.patFw.empty());
	int nchar = 0;
	while (cur < buflen)
	{
		c = ra.readOrigBuf[cur++];
		if (isalpha(c))
		{
			assert_in(toupper(c), "ACGTN");
			if (nchar++ >= pp_.trim5)
			{
				assert_neq(0, asc2dnacat[c]);
				ra.patFw.append(asc2dna[c]);
			}
		}
	}
	ra.trimmed5 = (int)(nchar - ra.patFw.length());
	ra.trimmed3 = (int)(ra.patFw.trimEnd(pp_.trim3));
	assert(ra.qual.empty());
	const size_t len = ra.patFw.length();
	for (size_t i = 0; i < len; i++)
	{
		ra.qual.append('I');
	}
	return true;
}
pair<bool, int> FastqPatternSource::nextBatchFromFile(
	PerThreadReadBuf &pt,
	bool batch_a, unsigned readi)
{
	int c = -1;
	EList<Read> *readbuf = batch_a ? &pt.bufa_ : &pt.bufb_;
	if (first_)
	{
		c = getc_wrapper();
		if (c == EOF)
		{
			return make_pair(true, 0);
		}
		while (c == '\r' || c == '\n')
		{
			c = getc_wrapper();
		}
		if (c != '@')
		{
			cerr << "Error: reads file does not look like a FASTQ file" << endl;
			throw 1;
		}
		first_ = false;
		(*readbuf)[readi].readOrigBuf.append('@');
	}
	bool done = false, aborted = false;
	while (readi < pt.max_buf_ && !done)
	{
		Read::TBuf &buf = (*readbuf)[readi].readOrigBuf;
		int newlines = 4;
		while (newlines)
		{
			c = getc_wrapper();
			done = c < 0;
			if (c == '\n' || (done && newlines == 1))
			{
				newlines--;
				c = '\n';
			}
			else if (done)
			{
				if (newlines == 4)
				{
					newlines = 0;
				}
				else
				{
					aborted = true;
				}
				break;
			}
			buf.append(c);
		}
		if (c > 0)
		{
			if (interleaved_)
			{
				batch_a = !batch_a;
				readbuf = batch_a ? &pt.bufa_ : &pt.bufb_;
				readi = batch_a ? readi + 1 : readi;
			}
			else
			{
				readi++;
			}
		}
	}
	if (aborted)
	{
		readi--;
	}
	return make_pair(done, readi);
}
bool FastqPatternSource::parse(Read &r, Read &rb, TReadId rdid) const
{
	assert(!r.readOrigBuf.empty());
	assert(r.empty());
	int c;
	size_t cur = 1;
	const size_t buflen = r.readOrigBuf.length();
	assert(r.name.empty());
	while (true)
	{
		assert_lt(cur, buflen);
		c = r.readOrigBuf[cur++];
		if (c == '\n' || c == '\r')
		{
			do
			{
				c = r.readOrigBuf[cur++];
			} while (c == '\n' || c == '\r');
			break;
		}
		r.name.append(c);
	}
	int nchar = 0;
	assert(r.patFw.empty());
	while (c != '+')
	{
		if (c == '.')
		{
			c = 'N';
		}
		if (isalpha(c))
		{
			if (nchar++ >= pp_.trim5)
			{
				r.patFw.append(asc2dna[c]);
			}
		}
		assert(cur < r.readOrigBuf.length());
		c = r.readOrigBuf[cur++];
	}
	r.trimmed5 = (int)(nchar - r.patFw.length());
	r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
	assert_eq('+', c);
	do
	{
		assert(cur < r.readOrigBuf.length());
		c = r.readOrigBuf[cur++];
	} while (c != '\n' && c != '\r');
	while (cur < buflen && (c == '\n' || c == '\r'))
	{
		c = r.readOrigBuf[cur++];
	}
	assert(r.qual.empty());
	if (nchar > 0)
	{
		int nqual = 0;
		if (pp_.intQuals)
		{
			int cur_int = 0;
			while (c != '\t' && c != '\n' && c != '\r')
			{
				cur_int *= 10;
				cur_int += (int)(c - '0');
				c = r.readOrigBuf[cur++];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				{
					char cadd = intToPhred33(cur_int, pp_.solexa64);
					cur_int = 0;
					assert_geq(cadd, 33);
					if (++nqual > pp_.trim5)
					{
						r.qual.append(cadd);
					}
				}
			}
		}
		else
		{
			c = charToPhred33(c, pp_.solexa64, pp_.phred64);
			if (nqual++ >= r.trimmed5)
			{
				r.qual.append(c);
			}
			while (cur < r.readOrigBuf.length())
			{
				c = r.readOrigBuf[cur++];
				if (c == ' ')
				{
					wrongQualityFormat(r.name);
					return false;
				}
				if (c == '\r' || c == '\n')
				{
					break;
				}
				c = charToPhred33(c, pp_.solexa64, pp_.phred64);
				if (nqual++ >= r.trimmed5)
				{
					r.qual.append(c);
				}
			}
			r.qual.trimEnd(r.trimmed3);
			if (r.qual.length() < r.patFw.length())
			{
				tooFewQualities(r.name);
				return false;
			}
			else if (r.qual.length() > r.patFw.length())
			{
				tooManyQualities(r.name);
				return false;
			}
		}
	}
	if (r.name.empty())
	{
		char cbuf[20];
		itoa10<TReadId>(static_cast<TReadId>(rdid), cbuf);
		r.name.install(cbuf);
	}
	r.parsed = true;
	if (!rb.parsed && !rb.readOrigBuf.empty())
	{
		return parse(rb, r, rdid);
	}
	return true;
}
const int BAMPatternSource::offset[] = {
	0,
	4,
	8,
	9,
	10,
	12,
	14,
	16,
	20,
	24,
	28,
	32,
};
const uint8_t BAMPatternSource::EOF_MARKER[] = {
	0x1f, 0x8b, 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
	0x06, 0x00, 0x42, 0x43, 0x02, 0x00, 0x1b, 0x00, 0x03, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint16_t BAMPatternSource::nextBGZFBlockFromFile(BGZF &b)
{
	if (fread(&b.hdr, sizeof(b.hdr), 1, fp_) != 1)
	{
		if (feof(fp_))
			return 0;
		std::cerr << "Error while reading BAM header" << std::endl;
		exit(EXIT_FAILURE);
	}
	uint8_t *extra = new uint8_t[b.hdr.xlen];
	if (fread(extra, b.hdr.xlen, 1, fp_) != 1)
	{
		std::cerr << "Error while reading BAM extra subfields" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (memcmp(EOF_MARKER, &b.hdr, sizeof(b.hdr)) == 0 &&
		memcmp(EOF_MARKER + sizeof(b.hdr), extra, 6) == 0)
	{
		delete[] extra;
		return 0;
	}
	uint16_t bsize = 0;
	for (uint16_t i = 0; i < b.hdr.xlen;)
	{
		if (extra[0] == 66 && extra[1] == 67)
		{
			bsize = *((uint16_t *)(extra + 4));
			bsize -= (b.hdr.xlen + 19);
			break;
		}
		i = i + 2;
		uint16_t sub_field_len = *((uint16_t *)(extra + 2));
		i = i + 2 + sub_field_len;
	}
	delete[] extra;
	if (bsize == 0)
		return 0;
	if (fread(b.cdata, bsize, 1, fp_) != 1)
	{
		std::cerr << "Error while reading BAM CDATA (compressed data)" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (fread(&b.ftr, sizeof(b.ftr), 1, fp_) != 1)
	{
		std::cerr << "Error while reading BAM footer" << std::endl;
		exit(EXIT_FAILURE);
	}
	return bsize;
}
std::pair<bool, int> BAMPatternSource::nextBatch(PerThreadReadBuf &pt, bool batch_a, bool lock)
{
	bool done = false;
	uint16_t cdata_len;
	unsigned nread = 0;
	do
	{
		if (bam_batch_indexes_[pt.tid_] >= bam_batches_[pt.tid_].size())
		{
			BGZF block;
			std::vector<uint8_t> &batch = bam_batches_[pt.tid_];
			if (lock)
			{
				ThreadSafe ts(mutex);
				if (first_)
				{
					nextBGZFBlockFromFile(block);
					first_ = false;
				}
				cdata_len = nextBGZFBlockFromFile(block);
			}
			else
			{
				if (first_)
				{
					nextBGZFBlockFromFile(block);
					first_ = false;
				}
				cdata_len = nextBGZFBlockFromFile(block);
			}
			if (cdata_len == 0)
			{
				done = nread == 0;
				break;
			}
			bam_batch_indexes_[pt.tid_] = 0;
			batch.resize(block.ftr.isize);
			int ret_code = decompress_bgzf_block(&batch[0], block.ftr.isize, block.cdata, cdata_len);
			if (ret_code != Z_OK)
			{
				return make_pair(true, 0);
			}
			uLong crc = crc32(0L, Z_NULL, 0);
			crc = crc32(crc, &batch[0], batch.size());
			assert(crc == block.ftr.crc32);
		}
		std::pair<bool, int> ret = get_alignments(pt, batch_a, nread, lock);
		done = ret.first;
	} while (!done && nread < pt.max_buf_);
	if (lock)
	{
		ThreadSafe ts(mutex);
		pt.setReadId(readCnt_);
		readCnt_ += nread;
	}
	else
	{
		pt.setReadId(readCnt_);
		readCnt_ += nread;
	}
	return make_pair(done, nread);
}
std::pair<bool, int> BAMPatternSource::get_alignments(PerThreadReadBuf &pt, bool batch_a, unsigned &readi, bool lock)
{
	size_t &i = bam_batch_indexes_[pt.tid_];
	bool done = false;
	bool read1 = true;
	while (readi < pt.max_buf_)
	{
		if (i >= bam_batches_[pt.tid_].size())
		{
			return make_pair(false, readi);
		}
		uint16_t flag;
		int32_t block_size;
		EList<Read> &readbuf = pp_.align_paired_reads && !read1 ? pt.bufb_ : pt.bufa_;
		memcpy(&block_size, &bam_batches_[pt.tid_][0] + i, sizeof(block_size));
		if (block_size <= 0)
		{
			return make_pair(done, readi);
		}
		i += sizeof(block_size);
		memcpy(&flag, &bam_batches_[pt.tid_][0] + i + offset[BAMField::flag], sizeof(flag));
		if (!pp_.align_paired_reads && ((flag & 0x40) != 0 || (flag & 0x80) != 0))
		{
			readbuf[readi].readOrigBuf.clear();
			i += block_size;
			continue;
		}
		if (pp_.align_paired_reads && ((flag & 0x40) == 0 && (flag & 0x80) == 0))
		{
			readbuf[readi].readOrigBuf.clear();
			i += block_size;
			continue;
		}
		if (pp_.align_paired_reads &&
			(((flag & 0x40) != 0 && i + block_size == bam_batches_[pt.tid_].size()) ||
			 ((flag & 0x80) != 0 && i == sizeof(block_size))))
		{
			if (lock)
			{
				ThreadSafe ts(orphan_mates_mutex_);
				get_or_store_orhaned_mate(pt.bufa_, pt.bufb_, readi, &bam_batches_[pt.tid_][0] + i, block_size);
				i += block_size;
			}
			else
			{
				get_or_store_orhaned_mate(pt.bufa_, pt.bufb_, readi, &bam_batches_[pt.tid_][0] + i, block_size);
				i += block_size;
			}
		}
		else
		{
			readbuf[readi].readOrigBuf.resize(block_size);
			memcpy(readbuf[readi].readOrigBuf.wbuf(), &bam_batches_[pt.tid_][0] + i, block_size);
			i += block_size;
			read1 = !read1;
			readi = (pp_.align_paired_reads &&
					 pt.bufb_[readi].readOrigBuf.length() == 0)
						? readi
						: readi + 1;
		}
	}
	return make_pair(done, readi);
}
void BAMPatternSource::get_or_store_orhaned_mate(EList<Read> &buf_a, EList<Read> &buf_b, unsigned &readi, const uint8_t *mate, size_t mate_len)
{
	const char *read_name =
		(const char *)(mate + offset[BAMField::read_name]);
	size_t i;
	uint32_t hash = hash_str(read_name);
	orphan_mate_t *empty_slot = NULL;
	for (i = 0; i < orphan_mates.size(); i++)
	{
		if (empty_slot == NULL && orphan_mates[i].empty())
			empty_slot = &orphan_mates[i];
		if (orphan_mates[i].hash == hash)
			break;
	}
	if (i == orphan_mates.size())
	{
		if (empty_slot == NULL)
		{
			orphan_mates.push_back(orphan_mate_t());
			empty_slot = &orphan_mates.back();
		}
		empty_slot->hash = hash;
		if (empty_slot->cap < mate_len)
		{
			delete[] empty_slot->data;
			empty_slot->data = NULL;
		}
		if (empty_slot->data == NULL)
		{
			empty_slot->data = new uint8_t[mate_len];
			empty_slot->cap = mate_len;
		}
		memcpy(empty_slot->data, mate, mate_len);
		empty_slot->size = mate_len;
	}
	else
	{
		uint8_t flag;
		Read &ra = buf_a[readi];
		Read &rb = buf_b[readi];
		memcpy(&flag, mate + offset[BAMField::flag], sizeof(flag));
		if ((flag & 0x40) != 0)
		{
			ra.readOrigBuf.resize(mate_len);
			memcpy(ra.readOrigBuf.wbuf(), mate, mate_len);
			rb.readOrigBuf.resize(orphan_mates[i].size);
			memcpy(rb.readOrigBuf.wbuf(), orphan_mates[i].data, orphan_mates[i].size);
		}
		else
		{
			rb.readOrigBuf.resize(mate_len);
			memcpy(rb.readOrigBuf.wbuf(), mate, mate_len);
			ra.readOrigBuf.resize(orphan_mates[i].size);
			memcpy(ra.readOrigBuf.wbuf(), orphan_mates[i].data, orphan_mates[i].size);
		}
		readi++;
		orphan_mates[i].reset();
	}
}
int BAMPatternSource::decompress_bgzf_block(uint8_t *dst, size_t dst_len, uint8_t *src, size_t src_len)
{
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = src_len;
	strm.next_in = src;
	strm.avail_out = dst_len;
	strm.next_out = dst;
	int ret = inflateInit2(&strm, -8);
	if (ret != Z_OK)
	{
		return ret;
	}
	ret = inflate(&strm, Z_FINISH);
	if (ret != Z_STREAM_END)
	{
		return ret;
	}
	return inflateEnd(&strm);
}
bool BAMPatternSource::parse(Read &ra, Read &rb, TReadId rdid) const
{
	uint8_t l_read_name;
	int32_t l_seq;
	uint16_t n_cigar_op;
	const char *buf = ra.readOrigBuf.buf();
	int block_size = ra.readOrigBuf.length();
	memcpy(&l_read_name, buf + offset[BAMField::l_read_name], sizeof(l_read_name));
	memcpy(&n_cigar_op, buf + offset[BAMField::n_cigar_op], sizeof(n_cigar_op));
	memcpy(&l_seq, buf + offset[BAMField::l_seq], sizeof(l_seq));
	int off = offset[BAMField::read_name];
	ra.name.install(buf + off, l_read_name - 1);
	off += (l_read_name + sizeof(uint32_t) * n_cigar_op);
	const char *seq = buf + off;
	off += (l_seq + 1) / 2;
	const char *qual = buf + off;
	for (int i = 0; i < l_seq; i++)
	{
		if (i < pp_.trim5)
		{
			ra.trimmed5 += 1;
		}
		else
		{
			ra.qual.append(qual[i] + 33);
			int base = "=ACMGRSVTWYHKDBN"[static_cast<uint8_t>(seq[i / 2]) >> 4 * (1 - (i % 2)) & 0xf];
			ra.patFw.append(asc2dna[base]);
		}
	}
	ra.trimmed3 = (int)(ra.patFw.trimEnd(pp_.trim3));
	ra.qual.trimEnd(ra.trimmed3);
	if (pp_.preserve_tags)
	{
		off += l_seq;
		ra.preservedOptFlags.install(buf + off, block_size - off);
	}
	ra.parsed = true;
	if (!rb.parsed && rb.readOrigBuf.length() != 0)
	{
		return parse(rb, ra, rdid);
	}
	return true;
}
pair<bool, int> TabbedPatternSource::nextBatchFromFile(
	PerThreadReadBuf &pt,
	bool batch_a, unsigned readi)
{
	int c = getc_wrapper();
	while (c >= 0 && (c == '\n' || c == '\r'))
	{
		c = getc_wrapper();
	}
	EList<Read> &readbuf = batch_a ? pt.bufa_ : pt.bufb_;
	for (; readi < pt.max_buf_ && c >= 0; readi++)
	{
		readbuf[readi].readOrigBuf.clear();
		while (c >= 0 && c != '\n' && c != '\r')
		{
			readbuf[readi].readOrigBuf.append(c);
			c = getc_wrapper();
		}
		while (c >= 0 && (c == '\n' || c == '\r') && readi < pt.max_buf_ - 1)
		{
			c = getc_wrapper();
		}
	}
	return make_pair(c < 0, readi);
}
bool TabbedPatternSource::parse(Read &ra, Read &rb, TReadId rdid) const
{
	assert(ra.empty());
	assert(rb.empty());
	assert(!ra.readOrigBuf.empty());
	assert(rb.readOrigBuf.empty());
	int c = '\t';
	size_t cur = 0;
	const size_t buflen = ra.readOrigBuf.length();
	for (int endi = 0; endi < 2 && c == '\t'; endi++)
	{
		Read &r = ((endi == 0) ? ra : rb);
		assert(r.name.empty());
		if (endi < 1 || secondName_)
		{
			c = ra.readOrigBuf[cur++];
			while (c != '\t' && cur < buflen)
			{
				r.name.append(c);
				c = ra.readOrigBuf[cur++];
			}
			assert_eq('\t', c);
			if (cur >= buflen)
			{
				return false;
			}
		}
		else if (endi > 0)
		{
			rb.name = ra.name;
		}
		assert(r.patFw.empty());
		c = ra.readOrigBuf[cur++];
		int nchar = 0;
		while (c != '\t' && cur < buflen)
		{
			if (isalpha(c))
			{
				assert_in(toupper(c), "ACGTN");
				if (nchar++ >= pp_.trim5)
				{
					assert_neq(0, asc2dnacat[c]);
					r.patFw.append(asc2dna[c]);
				}
			}
			c = ra.readOrigBuf[cur++];
		}
		assert_eq('\t', c);
		if (cur >= buflen)
		{
			return false;
		}
		r.trimmed5 = (int)(nchar - r.patFw.length());
		r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
		assert(r.qual.empty());
		c = ra.readOrigBuf[cur++];
		int nqual = 0;
		if (pp_.intQuals)
		{
			int cur_int = 0;
			while (c != '\t' && c != '\n' && c != '\r' && cur < buflen)
			{
				cur_int *= 10;
				cur_int += (int)(c - '0');
				c = ra.readOrigBuf[cur++];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				{
					char cadd = intToPhred33(cur_int, pp_.solexa64);
					cur_int = 0;
					assert_geq(cadd, 33);
					if (++nqual > pp_.trim5)
					{
						r.qual.append(cadd);
					}
				}
			}
		}
		else
		{
			while (c != '\t' && c != '\n' && c != '\r')
			{
				if (c == ' ')
				{
					wrongQualityFormat(r.name);
					return false;
				}
				char cadd = charToPhred33(c, pp_.solexa64, pp_.phred64);
				if (++nqual > pp_.trim5)
				{
					r.qual.append(cadd);
				}
				if (cur >= buflen)
					break;
				c = ra.readOrigBuf[cur++];
			}
		}
		if (nchar > nqual)
		{
			tooFewQualities(r.name);
			return false;
		}
		else if (nqual > nchar)
		{
			tooManyQualities(r.name);
			return false;
		}
		r.qual.trimEnd(pp_.trim3);
		assert(c == '\t' || c == '\n' || c == '\r' || cur >= buflen);
		assert_eq(r.patFw.length(), r.qual.length());
	}
	return true;
}
pair<bool, int> RawPatternSource::nextBatchFromFile(
	PerThreadReadBuf &pt,
	bool batch_a,
	unsigned readi)
{
	int c = getc_wrapper();
	while (c >= 0 && (c == '\n' || c == '\r'))
	{
		c = getc_wrapper();
	}
	EList<Read> &readbuf = batch_a ? pt.bufa_ : pt.bufb_;
	for (; readi < pt.max_buf_ && c >= 0; readi++)
	{
		readbuf[readi].readOrigBuf.clear();
		while (c >= 0 && c != '\n' && c != '\r')
		{
			readbuf[readi].readOrigBuf.append(c);
			c = getc_wrapper();
		}
		while (c >= 0 && (c == '\n' || c == '\r'))
		{
			c = getc_wrapper();
		}
	}
	if (c >= 0 && c != '\n' && c != '\r')
	{
		ungetc_wrapper(c);
	}
	return make_pair(c < 0, readi);
}
bool RawPatternSource::parse(Read &r, Read &rb, TReadId rdid) const
{
	assert(r.empty());
	assert(!r.readOrigBuf.empty());
	int c = '\n';
	size_t cur = 0;
	const size_t buflen = r.readOrigBuf.length();
	assert(r.patFw.empty());
	int nchar = 0;
	while (cur < buflen)
	{
		c = r.readOrigBuf[cur++];
		assert(c != '\r' && c != '\n');
		if (isalpha(c))
		{
			assert_in(toupper(c), "ACGTN");
			if (nchar++ >= pp_.trim5)
			{
				assert_neq(0, asc2dnacat[c]);
				r.patFw.append(asc2dna[c]);
			}
		}
	}
	assert_eq(cur, buflen);
	r.trimmed5 = (int)(nchar - r.patFw.length());
	r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
	char cbuf[20];
	itoa10<TReadId>(rdid, cbuf);
	r.name.install(cbuf);
	assert(r.qual.empty());
	const size_t len = r.patFw.length();
	for (size_t i = 0; i < len; i++)
	{
		r.qual.append('I');
	}
	r.parsed = true;
	if (!rb.parsed && !rb.readOrigBuf.empty())
	{
		return parse(rb, r, rdid);
	}
	return true;
}
void wrongQualityFormat(const BTString &read_name)
{
	cerr << "Error: Encountered one or more spaces while parsing the quality "
		 << "string for read " << read_name << ".  If this is a FASTQ file "
		 << "with integer (non-ASCII-encoded) qualities, try re-running with "
		 << "the --integer-quals option." << endl;
	throw 1;
}
void tooFewQualities(const BTString &read_name)
{
	cerr << "Error: Read " << read_name << " has more read characters than "
		 << "quality values." << endl;
	throw 1;
}
void tooManyQualities(const BTString &read_name)
{
	cerr << "Error: Read " << read_name << " has more quality values than read "
		 << "characters." << endl;
	throw 1;
}
#ifdef USE_SRA
std::pair<bool, int> SRAPatternSource::nextBatchImpl(
	PerThreadReadBuf &pt,
	bool batch_a)
{
	EList<Read> &readbuf = batch_a ? pt.bufa_ : pt.bufb_;
	size_t readi = 0;
	bool done = false;
	for (; readi < pt.max_buf_; readi++)
	{
		if (!sra_its_[pt.tid_]->nextRead() || !sra_its_[pt.tid_]->nextFragment())
		{
			done = true;
			break;
		}
		const ngs::StringRef rname = sra_its_[pt.tid_]->getReadId();
		const ngs::StringRef ra_seq = sra_its_[pt.tid_]->getFragmentBases();
		const ngs::StringRef ra_qual = sra_its_[pt.tid_]->getFragmentQualities();
		readbuf[readi].readOrigBuf.install(rname.data(), rname.size());
		readbuf[readi].readOrigBuf.append('\t');
		readbuf[readi].readOrigBuf.append(ra_seq.data(), ra_seq.size());
		readbuf[readi].readOrigBuf.append('\t');
		readbuf[readi].readOrigBuf.append(ra_qual.data(), ra_qual.size());
		if (sra_its_[pt.tid_]->nextFragment())
		{
			const ngs::StringRef rb_seq = sra_its_[pt.tid_]->getFragmentBases();
			const ngs::StringRef rb_qual = sra_its_[pt.tid_]->getFragmentQualities();
			readbuf[readi].readOrigBuf.append('\t');
			readbuf[readi].readOrigBuf.append(rb_seq.data(), rb_seq.size());
			readbuf[readi].readOrigBuf.append('\t');
			readbuf[readi].readOrigBuf.append(rb_qual.data(), rb_qual.size());
		}
		readbuf[readi].readOrigBuf.append('\n');
	}
	pt.setReadId(readCnt_);
	{
		ThreadSafe ts(mutex);
		readCnt_ += readi;
	}
	return make_pair(done, readi);
}
std::pair<bool, int> SRAPatternSource::nextBatch(
	PerThreadReadBuf &pt,
	bool batch_a,
	bool lock)
{
	if (lock)
	{
		return nextBatchImpl(pt, batch_a);
	}
	else
	{
		return nextBatchImpl(pt, batch_a);
	}
}
bool SRAPatternSource::parse(Read &ra, Read &rb, TReadId rdid) const
{
	assert(ra.empty());
	assert(rb.empty());
	assert(!ra.readOrigBuf.empty());
	assert(rb.readOrigBuf.empty());
	int c = '\t';
	size_t cur = 0;
	const size_t buflen = ra.readOrigBuf.length();
	for (int endi = 0; endi < 2 && c == '\t'; endi++)
	{
		Read &r = ((endi == 0) ? ra : rb);
		assert(r.name.empty());
		if (endi < 1)
		{
			c = ra.readOrigBuf[cur++];
			while (c != '\t' && cur < buflen)
			{
				r.name.append(c);
				c = ra.readOrigBuf[cur++];
			}
			assert_eq('\t', c);
			if (cur >= buflen)
			{
				return false;
			}
		}
		else if (endi > 0)
		{
			rb.name = ra.name;
		}
		assert(r.patFw.empty());
		c = ra.readOrigBuf[cur++];
		int nchar = 0;
		while (c != '\t' && cur < buflen)
		{
			if (isalpha(c))
			{
				assert_in(toupper(c), "ACGTN");
				if (nchar++ >= pp_.trim5)
				{
					assert_neq(0, asc2dnacat[c]);
					r.patFw.append(asc2dna[c]);
				}
			}
			c = ra.readOrigBuf[cur++];
		}
		assert_eq('\t', c);
		if (cur >= buflen)
		{
			return false;
		}
		r.trimmed5 = (int)(nchar - r.patFw.length());
		r.trimmed3 = (int)(r.patFw.trimEnd(pp_.trim3));
		assert(r.qual.empty());
		c = ra.readOrigBuf[cur++];
		int nqual = 0;
		if (pp_.intQuals)
		{
			int cur_int = 0;
			while (c != '\t' && c != '\n' && c != '\r' && cur < buflen)
			{
				cur_int *= 10;
				cur_int += (int)(c - '0');
				c = ra.readOrigBuf[cur++];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				{
					char cadd = intToPhred33(cur_int, pp_.solexa64);
					cur_int = 0;
					assert_geq(cadd, 33);
					if (++nqual > pp_.trim5)
					{
						r.qual.append(cadd);
					}
				}
			}
		}
		else
		{
			while (c != '\t' && c != '\n' && c != '\r')
			{
				if (c == ' ')
				{
					wrongQualityFormat(r.name);
					return false;
				}
				char cadd = charToPhred33(c, pp_.solexa64, pp_.phred64);
				if (++nqual > pp_.trim5)
				{
					r.qual.append(cadd);
				}
				if (cur >= buflen)
					break;
				c = ra.readOrigBuf[cur++];
			}
		}
		if (nchar > nqual)
		{
			tooFewQualities(r.name);
			return false;
		}
		else if (nqual > nchar)
		{
			tooManyQualities(r.name);
			return false;
		}
		r.qual.trimEnd(pp_.trim3);
		assert(c == '\t' || c == '\n' || c == '\r' || cur >= buflen);
		assert_eq(r.patFw.length(), r.qual.length());
	}
	return true;
}
void SRAPatternSource::open()
{
	const string &sra_acc = sra_accs_[sra_acc_cur_];
	string version = "effaln";
	ncbi::NGS::setAppVersionString(version);
	assert(!sra_acc.empty());
	try
	{
		ngs::ReadCollection sra_run = ncbi::NGS::openReadCollection(sra_acc);
		size_t MAX_ROW = sra_run.getReadCount();
		pp_.upto -= pp_.skip;
		if (pp_.upto <= MAX_ROW)
		{
			MAX_ROW = pp_.upto;
		}
		if (MAX_ROW < 0)
		{
			return;
		}
		size_t window_size = MAX_ROW / sra_its_.size();
		size_t remainder = MAX_ROW % sra_its_.size();
		size_t i = 0, start = 1;
		if (pp_.skip > 0)
		{
			start = pp_.skip + 1;
			readCnt_ = pp_.skip;
		}
		while (i < sra_its_.size())
		{
			sra_its_[i] = new ngs::ReadIterator(sra_run.getReadRange(start, window_size, ngs::Read::all));
			assert(sra_its_[i] != NULL);
			i++;
			start += window_size;
			if (i == sra_its_.size() - 1)
			{
				window_size += remainder;
			}
		}
	}
	catch (...)
	{
		cerr << "Warning: Could not access \"" << sra_acc << "\" for reading; skipping..." << endl;
	}
}
#endif
