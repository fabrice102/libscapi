#ifndef OT_M_EXTENSION_KOS_H__
#define OT_M_EXTENSION_KOS_H__
#include "ot.h"
#include "co.h"

/** @addtogroup OT
  @{
 */
template<typename IO>
class MOTExtension_KOS: public OTExtension<IO, OTCO, ::MOTExtension_KOS> { public:
	block *open_data = nullptr;
	bool committing = false;
	char dgst[Hash::DIGEST_SIZE];
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::send_pre;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::block_s;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::recv_pre;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::io;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::extended_r;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::qT;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::tT;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::prg;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::pi;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::padded_length;
	using OTExtension<IO, OTCO, ::MOTExtension_KOS>::block_size;

	MOTExtension_KOS(IO * io, bool committing = false, int ssp = 40) :
		OTExtension<IO, OTCO, ::MOTExtension_KOS>(io, ssp) {
			this->committing = committing;
		}

	~MOTExtension_KOS() {
		delete_array_null(open_data);
	}

	bool send_check(int length) {
		if (committing) {
			Hash::hash_once(dgst, &block_s, sizeof(block));
			io->send_data(dgst, Hash::DIGEST_SIZE);
		}

		int extended_length = padded_length(length);
		block seed2, x, t[2], q[2], tmp1, tmp2;
		io->recv_block(&seed2, 1);
		block chi[block_size];
		PRG prg2(&seed2);

		q[0] = zero_block();
		q[1] = zero_block();
		for(int i = 0; i < extended_length/block_size; ++i) {
			prg2.random_block(chi, block_size);
			for(int j = 0; j < block_size; ++j) {
				mul128(qT[i*block_size+j], chi[j], &tmp1, &tmp2);
				q[0] = xorBlocks(q[0], tmp1);
				q[1] = xorBlocks(q[1], tmp2);
			}
		}
		io->recv_block(&x, 1);
		io->recv_block(t, 2);
		mul128(x, block_s, &tmp1, &tmp2);
		q[0] = xorBlocks(q[0], tmp1);
		q[1] = xorBlocks(q[1], tmp2);

		return block_cmp(q, t, 2);	
	}
	void recv_check(const bool* r, int length) {
		if (committing) {
			io->recv_data(dgst, Hash::DIGEST_SIZE);
		}

		int extended_length = padded_length(length);
		block chi[block_size];
		block seed2, x = zero_block(), t[2], tmp1, tmp2;
		prg.random_block(&seed2,1);
		io->send_block(&seed2, 1);
		PRG prg2(&seed2);
		t[0] = t[1] = zero_block();

		for(int i = 0; i < extended_length/block_size; ++i) {
			prg2.random_block(chi, block_size);
			for(int j = 0; j < block_size; ++j) {
				mul128(chi[j], tT[i*block_size+j], &tmp1, &tmp2);
				t[0] = xorBlocks(t[0], tmp1);
				t[1] = xorBlocks(t[1], tmp2);
				if (i*block_size+j < length and r[i*block_size+j])
					x = xorBlocks(x, chi[j]);
				else if (i*block_size+j >= length and extended_r[i*block_size+j-length]) 
					x = xorBlocks(x, chi[j]);
			}
		}
		io->send_block(&x, 1);
		io->send_block(t, 2);
	}
	void got_recv_post(block* data, const bool* r, int length) {
		block res[2];
		if(committing) {
			delete_array_null(open_data);
			open_data = new block[length];
			for(int i = 0; i < length; ++i) {
				io->recv_data(res, 2*sizeof(block));
				if(r[i]) {
					data[i] = xorBlocks(res[1], pi.H(tT[i], 2*i+1));
					open_data[i] = res[0];
				} else {
					data[i] = xorBlocks(res[0], pi.H(tT[i], 2*i));
					open_data[i] = res[1];
				}
			}
		} else {
			for(int i = 0; i < length; ++i) {
				io->recv_data(res, 2*sizeof(block));
				if(r[i]) {
					data[i] = xorBlocks(res[1], pi.H(tT[i], 2*i+1));
				} else {
					data[i] = xorBlocks(res[0], pi.H(tT[i], 2*i));
				}
			}
		}
	}

	void send_impl(const block* data0, const block* data1, int length) {
		send_pre(length);
		if(!send_check(length))	error("OT Extension check failed");
		OTExtension<IO, OTCO, ::MOTExtension_KOS>::got_send_post(data0, data1, length);
	}

	void recv_impl(block* data, const bool* b, int length) {
		recv_pre(b, length);
		recv_check(b, length);
		got_recv_post(data, b, length);
	}

	void send_rot(block * data0, block * data1, int length) {
		send_pre(length);
		if(!send_check(length))error("OT Extension check failed");
		OTExtension<IO, OTCO, ::MOTExtension_KOS>::rot_send_post(data0, data1, length);
	}

	void recv_rot(block* data, const bool* b, int length) {
		recv_pre(b, length);
		recv_check(b, length);
		OTExtension<IO, OTCO, ::MOTExtension_KOS>::rot_recv_post(data, b, length);
	}

	void cot_send_post_new(block* data0, const block* delta, int length) {
		const int bsize = AES_BATCH_SIZE;
		block pad[2*bsize];
		block tmp[2*bsize];
		for(int i = 0; i < length; i+=bsize) {
			for(int j = i; j < i+bsize and j < length; ++j) {
				pad[2*(j-i)] = qT[j];
				pad[2*(j-i)+1] = xorBlocks(qT[j], block_s);
			}
		pi.Hn(pad, pad, 2*i, 2*bsize, tmp);
			for(int j = i; j < i+bsize and j < length; ++j) {
				data0[j] = pad[2*(j-i)];
				pad[2*(j-i)] = xorBlocks(pad[2*(j-i)], delta[j]);
				tmp[j-i] = xorBlocks(pad[2*(j-i)+1], pad[2*(j-i)]);
			}
			io->send_data(tmp, sizeof(block)*min(bsize,length-i));
		}
		delete[] qT;
	}

	void send_cot(block * data0, const block *delta, int length) {
		send_pre(length);
		if(!send_check(length))error("OT Extension check failed");
		cot_send_post_new(data0, delta, length);
	}
	void send_cot(block * data0, block delta, int length) {
		send_pre(length);
		if(!send_check(length))error("OT Extension check failed");
		OTExtension<IO, OTCO, ::MOTExtension_KOS>::cot_send_post(data0, delta, length);
	}

	void recv_cot(block* data, const bool* b, int length) {
		recv_pre(b, length);
		recv_check(b, length);
		OTExtension<IO, OTCO, ::MOTExtension_KOS>::cot_recv_post(data, b, length);
	}

	void open() {
		if (!committing)
			error("Committing not enabled");
		io->send_block(&block_s, 1);		
	}

	void open(block * data, const bool * r, int length) {		
		if (!committing)
			error("Committing not enabled");
		io->recv_block(&block_s, 1);		
		char com_recv[Hash::DIGEST_SIZE];		
		Hash::hash_once(com_recv, &block_s, sizeof(block));		
		if (strncmp(com_recv, dgst, 20)!= 0)
			error("invalid commitment");

		for(int i = 0; i < length; ++i) {	
			tT[i] = xorBlocks(tT[i], block_s);
			if(r[i])
				data[i] = xorBlocks(open_data[i], pi.H(tT[i], 2*i));
			else	
				data[i] = xorBlocks(open_data[i], pi.H(tT[i], 2*i+1));
		}		
	}
};

/**@}*/
#endif// OT_M_EXTENSION_KOS_H__
