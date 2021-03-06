#include "eosapi.hpp"
#include "pyobject.hpp"
#include <eosio/utilities/key_conversion.hpp>
#include <eosio/chain/chain_id_type.hpp>
#include <fc/io/json.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/crypto/signature.hpp>
#include <fc/crypto/sha1.hpp>
#include <fc/io/raw.hpp>
#include <eosio/chain/symbol.hpp>
#include <fc/crypto/public_key.hpp>

#include <vector>

using namespace std;

string eosapi_get_abi(string& account);

static fc::microseconds abi_serializer_max_time = fc::microseconds(100*1000);
static uint32_t tx_max_net_usage = 0;

uint64_t s2n_(std::string& str) {
   try {
      return name(str).value;
   } catch (...) {
   }
   return 0;
}

void n2s_(uint64_t n, std::string& s) {
   s = name(n).to_string();
}

std::map<std::string, std::shared_ptr<abi_serializer>> abi_cache;

void pack_args_(string& account, uint64_t action, std::string& _args, std::string& _binargs) {
   fc::variant args = fc::json::from_string(_args);

   try {
      auto itr = abi_cache.find(account);
      if (itr == abi_cache.end()) {
         std::string _rawabi = eosapi_get_abi(account);
         abi_def abi = fc::json::from_string(_rawabi).as<abi_def>();
         abi_cache[account] = std::make_shared<abi_serializer>(abi, abi_serializer_max_time);
      }
      abi_serializer& abis = *abi_cache[account];

      auto action_type = abis.get_action_type(action);
      EOS_ASSERT(!action_type.empty(), action_validate_exception, "Unknown action ${action}", ("action", action));

      auto binargs = abis.variant_to_binary(action_type, args, abi_serializer_max_time);
      _binargs = std::string(binargs.begin(), binargs.end());
   } FC_LOG_AND_DROP();
}

void unpack_args_(string& account, uint64_t action, std::string& _binargs, std::string& _args ) {
   bytes binargs = bytes(_binargs.data(), _binargs.data() + _binargs.size());

   try {
      auto itr = abi_cache.find(account);
      if (itr == abi_cache.end()) {
         std::string _rawabi = eosapi_get_abi(account);
         abi_def abi = fc::json::from_string(_rawabi).as<abi_def>();
         abi_cache[account] = std::make_shared<abi_serializer>(abi, abi_serializer_max_time);
      }
      abi_serializer& abis = *abi_cache[account];

      auto args = abis.binary_to_variant( abis.get_action_type( action ), binargs, abi_serializer_max_time );
      _args = fc::json::to_string(args);
   } FC_LOG_AND_DROP();
}

bool set_abi_(string& account, string& _abi) {
   try {
      abi_def abi = fc::json::from_string(_abi).as<abi_def>();
      abi_cache[account] = std::make_shared<abi_serializer>(abi, abi_serializer_max_time);
      return true;
   } FC_LOG_AND_DROP();
   return false;
}

bool clear_abi_cache_(string& account) {
   auto itr = abi_cache.find(account);
   if (itr != abi_cache.end()) {
      abi_cache.erase(itr);
      return true;
   }
   return false;
}

void pack_abi_(std::string& _abi, std::string& out) {
   try {
      auto abi = fc::raw::pack(fc::json::from_string(_abi).as<abi_def>());
      out = std::string(abi.begin(), abi.end());
   } FC_LOG_AND_DROP();
}

PyObject* gen_transaction_(vector<chain::action>& v, int expiration, std::string& reference_block_id) {
   packed_transaction::compression_type compression = packed_transaction::none;

   try {
      signed_transaction trx;
      for(auto& action: v) {
         trx.actions.push_back(action);
      }

      trx.expiration = fc::time_point::now() + fc::seconds(expiration);;

/*
      fc::variant v(reference_block_id);
      chain::block_id_type id;
      fc::from_variant(v, id);
*/
      chain::block_id_type id(reference_block_id);
      trx.set_reference_block(id);

//      trx.max_kcpu_usage = (tx_max_cpu_usage + 1023)/1024;
      trx.max_net_usage_words = (tx_max_net_usage + 7)/8;
      std::string result = fc::json::to_string(fc::variant(trx));
      return py_new_string(result);
   } FC_LOG_AND_DROP();

   return py_new_none();
}

PyObject* sign_transaction_(std::string& trx_json_to_sign, std::string& str_private_key, std::string& chain_id) {
   try {
      signed_transaction trx = fc::json::from_string(trx_json_to_sign).as<signed_transaction>();

      auto priv_key = fc::crypto::private_key::regenerate(*utilities::wif_to_key(str_private_key));

      fc::variant v(chain_id);
      chain::chain_id_type id(chain_id);
//      fc::from_variant(v, id);

      trx.sign(priv_key, id);
      std::string s = fc::json::to_string(fc::variant(trx));
      return py_new_string(s);
   } FC_LOG_AND_DROP();
   return py_new_none();
}


PyObject* pack_transaction_(std::string& _signed_trx, int compress) {
   try {
      signed_transaction signed_trx = fc::json::from_string(_signed_trx).as<signed_transaction>();
      packed_transaction::compression_type type;
      if (compress) {
         type = packed_transaction::compression_type::zlib;
      } else {
         type = packed_transaction::compression_type::none;
      }

      auto packed_trx = packed_transaction(signed_trx, type);
      std::string s = fc::json::to_string(packed_trx);
      return py_new_string(s);
   } FC_LOG_AND_DROP();
   return py_new_none();
}

PyObject* unpack_transaction_(std::string& trx) {
   try {
      vector<char> s(trx.c_str(), trx.c_str()+trx.size());
      auto st = fc::raw::unpack<transaction>(s);
      std::string ss = fc::json::to_string(st);
      return py_new_string(ss);
   } FC_LOG_AND_DROP();
   return py_new_none();
}

PyObject* create_key_() {
   auto pk    = private_key_type::generate();
   auto privs = std::string(pk);
   auto pubs  = std::string(pk.get_public_key());

   PyDict dict;
   std::string key;

   key = "public";
   dict.add(key, pubs);

   key = "private";
   dict.add(key, privs);
   return dict.get();
}

PyObject* get_public_key_(std::string& wif_key) {
   try {
      private_key_type priv(wif_key);

      std::string pub_key = std::string(priv.get_public_key());
      return py_new_string(pub_key);
   } FC_LOG_AND_DROP();
   return py_new_none();
}

void from_base58_( std::string& pub_key, std::string& raw_pub_key ) {
   try {
      auto v = fc::from_base58(pub_key);
      raw_pub_key = string(v.data(), v.size());
   } FC_LOG_AND_DROP();
}

void to_base58_( std::string& raw_pub_key, std::string& pub_key ) {
   try {
      std::vector<char> v(raw_pub_key.c_str(), raw_pub_key.c_str()+raw_pub_key.size());
      pub_key = fc::to_base58( v );
   } FC_LOG_AND_DROP();
}

void recover_key_( string& _digest, string& _sig, string& _pub ) {
   try {
//      ilog("+++++${n}", ("n", _sig));
      auto digest = fc::sha256(_digest);
      auto s = fc::crypto::signature(_sig);
      _pub = string(fc::crypto::public_key( s, digest, false ));
   } FC_LOG_AND_DROP();
}

void sign_digest_(string& _priv_key, string& _digest, string& out) {
    try {
        chain::private_key_type priv_key(_priv_key);
        chain::digest_type digest(_digest.c_str(), _digest.size());
        auto sign = priv_key.sign(digest);
        out = string(sign);
    } FC_LOG_AND_DROP();
}

uint64_t string_to_symbol_(int precision, string& str) {
   try {
      return string_to_symbol(precision, str.c_str());
   } FC_LOG_AND_DROP();
   return 0;
}

void set_public_key_prefix_(const string& prefix) {
   fc::crypto::config::public_key_legacy_prefix = prefix;
}

void get_public_key_prefix_(string& prefix) {
   prefix = fc::crypto::config::public_key_legacy_prefix;
}


