/*
 * Created by Xiangfu Song on 10/21/2016.
 * Email: bintasong@gmail.com
 * 
 */
#ifndef DISTSSE_SERVER_H
#define DISTSSE_SERVER_H

#include <grpc++/grpc++.h>

#include "DistSSE.grpc.pb.h"

#include "DistSSE.Util.h"

#include "logger.h"

#include <unordered_set>


#define min(x ,y) ( x < y ? x : y)

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::Status;

using namespace CryptoPP;

byte iv_s[17] = "0123456789abcdef";

namespace DistSSE{

class DistSSEServiceImpl final : public RPC::Service {
private:

	rocksdb::Options options;	
	static rocksdb::DB* ss_db;
	rocksdb::DB* cache_db;

	int MAX_THREADS;
	
	SHA256 hasher;

public:
	DistSSEServiceImpl(const std::string db_path, const std::string cache_path, int concurrent){
		signal(SIGINT, abort);

    	options.create_if_missing = true;

	    Util::set_db_common_options(options);

		rocksdb::Status s1 = rocksdb::DB::Open(options, db_path, &ss_db);

		MAX_THREADS = concurrent; //std::thread::hardware_concurrency();

		test_hash();

	}

	static void abort( int signum )
	{
		delete ss_db;
		// delete cache_db; 
		logger::log(logger::INFO)<< "Existing ... "<< signum <<std::endl;
	   	exit(signum);
	}

	static bool store(rocksdb::DB* &db, const std::string l, const std::string e){
		rocksdb::Status s; 		
		rocksdb::WriteOptions write_option = rocksdb::WriteOptions();
		//write_option.sync = true;
		//write_option.disableWAL = false;
		{
			// std::lock_guard<std::mutex> lock(ssdb_write_mtx);		
			s = db->Put(write_option, l, e);
			// db->SyncWAL();
		}
		return s.ok();
	}

	static std::string get(rocksdb::DB* &db, const std::string l){
		std::string tmp;
		rocksdb::Status s;
		{
			// std::lock_guard<std::mutex> lock(ssdb_write_mtx);
			s = db->Get(rocksdb::ReadOptions(), l, &tmp);
		}
		if (s.ok())	return tmp;
		else return "";
	}

	void test_hash(){
		std::string s1 = hash("fuck");
		std::string s2 = hash("fuck");
		std::cout<< Util::str2hex(s1) + ", " + Util::str2hex(s2)  <<std::endl;
		assert(s1.compare(s2) == 0);
	}

	std::string hash( const std::string in ) {
		//SHA256 h;
		hasher.Restart();
		byte buf[SHA256::DIGESTSIZE];
		hasher.CalculateDigest(buf, (byte*) (in.c_str()), in.length() );
		return std::string((const char*)buf, (size_t)SHA256::DIGESTSIZE);
	}

	static void parse (std::string str, std::string& op, std::string& ind, std::string& key) {
		op = str.substr(0, 1);		
		ind = str.substr(1, 8); // TODO
		key = str.substr(9, AES128_KEY_LEN);
	}
	
	void recover_st(std::string old_st, std::string key, std::string& new_st) {
		try {

			CFB_Mode< AES >::Decryption d;

			d.SetKeyWithIV((byte*) key.c_str(), AES128_KEY_LEN, iv_s, (size_t)AES::BLOCKSIZE); 
			
			byte tmp_new_st[old_st.length()];

			d.ProcessData(tmp_new_st, (byte*) old_st.c_str(), old_st.length());
			
			new_st = std::string((const char*)tmp_new_st, old_st.length());
		}
		catch(const CryptoPP::Exception& e)
		{
			std::cerr << "in generate_st() " << e.what()<< std::endl;
			exit(1);
		}
	}

	// only used for expriment
	static void search_log(std::string kw, double search_time, double get_time, int result_size) { 
		// std::ofstream out( "search.slog", std::ios::out|std::ios::app);
		
		byte k_s[17]  = "0123456789abcdef";
		byte iv_s[17] = "0123456789abcdef";

		std::string keyword = Util::dec_token(k_s, AES128_KEY_LEN, iv_s, kw);
			
		std::string word = keyword == "" ? "cached" : keyword;
		
		double time_per_entry = result_size == 0 ? search_time : ( search_time / result_size ) ;

		std::cout <<  word + "\t" + std::to_string(result_size)+ "\t" + std::to_string(get_time) + 
		              "\t" + std::to_string(search_time) + "\t" + std::to_string(time_per_entry)  << std::endl;
	}


	void search(std::string tw, std::string st, size_t uc, std::unordered_set<std::string>& ID) {
	
		std::vector<std::string> op_ind;

		std::string op, ind, rand_key;
		std::string _st, l, e, value;
		// std::string cache_ind;

		size_t counter = 0;

		struct timeval t1, t2, t3, t4;

		gettimeofday(&t1, NULL);

		std::unordered_set<std::string> result_set;
		std::unordered_set<std::string> delete_set;
	    _st = st;
		
		double get_time = 0.0;

		for(size_t i = 1; i <= uc; i++) {

			l = hash(tw + _st + "1");
			
			gettimeofday(&t3, NULL);

			e = get(ss_db, l);

			gettimeofday(&t4, NULL);

			get_time +=  ((t4.tv_sec - t3.tv_sec) * 1000000.0 + t4.tv_usec - t3.tv_usec) /1000.0;
			if(e == "") {
				logger::log(logger::ERROR)<< "FUCKING ERROR!"  <<std::endl;
				break;
			}
			//assert(e != "");

			value = Util::Xor( e, hash(tw + _st + "2") );

            parse(value, op, ind, _st); // At present, |st| = |key|, so we just store st too prevent envole P^-1(st_i)

			/*if(op == "1")*/ result_set.insert(ind); // TODO
			//logger::log(logger::INFO) << "hi-------------- "<< value <<std::endl;
			//else result_set->erase(ind);
			
			// remove or add 
			/*if (op == "0") {
				delete_set.insert(ind);		
			}
			else if(op == "1") {
				std::set<std::string>::iterator it = delete_set.find(ind);
				if (it != delete_set.end() ) {
					delete_set.erase(ind);				
				}else{
					result_set.insert(ind);				
				}
			}*/

			// recover_st( _st, rand_key, _st );
		}
		gettimeofday(&t2, NULL);

		double search_time =  ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) /1000.0;

		search_log(tw, search_time, get_time, uc);		
	
		std::string ID_string = "";
		for (std::unordered_set<std::string>::iterator it=ID.begin(); it!=ID.end(); ++it){
    		ID_string += Util::str2hex(*it) + "|";
		}
	}

// server RPC
	// search() 实现搜索操作
	Status search(ServerContext* context, const SearchRequestMessage* request,
                  ServerWriter<SearchReply>* writer)  {

		std::string st = request->kw();
		std::string tw = request->tw();	
		size_t uc = request->uc();
		
		struct timeval t1, t2;
		
		std::unordered_set<std::string> ID;

		// gettimeofday(&t1, NULL);
		search(tw, st, uc, ID);
		// gettimeofday(&t2, NULL);

  		//logger::log(logger::INFO) <<"ID.size():"<< ID.size() <<" ,search time: "<< ((t2.tv_sec - t1.tv_sec) * 1000000.0 + t2.tv_usec - t1.tv_usec) /1000.0/ID.size()<<" ms" <<std::endl;

	/*	SearchReply reply;
		
		for(int i = 0; i < ID.size(); i++){
			reply.set_ind(std::to_string(i));
			writer->Write(reply);
		}
	*/
	    return Status::OK;
  	}
	
	void random_put(int num) {

			AutoSeededRandomPool prng;
			int ind_len = AES::BLOCKSIZE; // AES::BLOCKSIZE = 16
			byte tmp[ind_len];

         	for(int i = 0; i < num; i++) {
				prng.GenerateBlock(tmp, sizeof(tmp));
				std::string key = (std::string((const char*)tmp, ind_len));
				std::string value = (std::string((const char*)tmp, ind_len / 2));
				ss_db->Put(rocksdb::WriteOptions(), key, value);
			}
	}

	// update()实现单次更新操作
	Status update(ServerContext* context, const UpdateRequestMessage* request, ExecuteStatus* response) {
		
		std::string l = request->l();
		std::string e = request->e();

		bool status = store(ss_db, l, e);
		if(!status) {
			response->set_status(false);
			return Status::CANCELLED;
		}
		
		response->set_status(true);

		return Status::OK;
	}
	
	// batch_update()实现批量更新操作
	Status batch_update(ServerContext* context, ServerReader< UpdateRequestMessage >* reader, ExecuteStatus* response) {
		std::string l;
		std::string e;
		UpdateRequestMessage request;

		//std::cout<< "in batch update" <<std::endl;
		while (reader->Read(&request)){
			l = request.l();
			e = request.e();
			store(ss_db, l, e);
		}

		response->set_status(true);
		return Status::OK;
	}
};

}// namespace DistSSE


// static member must declare out of main function

rocksdb::DB* DistSSE::DistSSEServiceImpl::ss_db;
// std::mutex DistSSE::DistSSEServiceImpl::ssdb_write_mtx;

void RunServer(std::string db_path, std::string cache_path, int concurrent) {
  std::string server_address("127.0.0.1:50051");
  DistSSE::DistSSEServiceImpl service(db_path, cache_path, concurrent);

  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

  builder.RegisterService(&service);

  std::unique_ptr<Server> server(builder.BuildAndStart());
  DistSSE::logger::log(DistSSE::logger::INFO) << "Server listening on " << server_address << std::endl;

  server->Wait();
}

#endif // DISTSSE_SERVER_H
