// Copyright 2014 The Souper Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "souper/KVStore/KVStore.h"

#include "llvm/Support/CommandLine.h"
#include "hiredis.h"

#include <sstream>

using namespace llvm;
using namespace souper;

static cl::opt<unsigned> RedisPort("souper-redis-port", cl::init(6379),
    cl::desc("Redis server port (default=6379)"));

namespace souper {

class KVStore::KVImpl {
  redisContext *Ctx = 0;

private:
  // checks if current redis database is compatible with current version of souper
  bool checkCompatibility();

public:
  KVImpl();
  ~KVImpl();
  void hIncrBy(llvm::StringRef Key, llvm::StringRef Field, int Incr);
  bool hGet(llvm::StringRef Key, llvm::StringRef Field, std::string &Value);
  void hSet(llvm::StringRef Key, llvm::StringRef Field, llvm::StringRef Value);
};

KVStore::KVImpl::KVImpl() {
  const char *hostname = "127.0.0.1";
  struct timeval Timeout = { 1, 500000 }; // 1.5 seconds
  Ctx = redisConnectWithTimeout(hostname, RedisPort, Timeout);
  if (!Ctx) {
    llvm::report_fatal_error("Can't allocate redis context\n");
  }
  if (Ctx->err) {
    llvm::report_fatal_error((llvm::StringRef)"Redis connection error: " +
                             Ctx->errstr + "\n");
  }

  if (!checkCompatibility()) {
    llvm::report_fatal_error("Redis database on port %d is incompatible.", RedisPort);
  }
}

KVStore::KVImpl::~KVImpl() {
  redisFree(Ctx);
}

bool KVStore::KVImpl::checkCompatibility() {
  assert(Ctx && "Cannot check compatibility on an uninitialized database.");

  redisReply *reply = static_cast<redisReply*>(redisCommand(Ctx, "GET cachetype"));
  if (!reply || Ctx->err) {
    llvm::report_fatal_error((llvm::StringRef)"Redis error: " + Ctx->errstr);
  }

  // get all current command line used
  StringMap<cl::Option*> &Opts =  cl::getRegisteredOptions();
  std::vector<std::string> ActiveOptions;
  for (auto &K : Opts.keys()) {
    if (Opts[K]->getNumOccurrences()) {
      ActiveOptions.emplace_back(K.str());
    }
  }
  std::sort(ActiveOptions.begin(), ActiveOptions.end());
  std::ostringstream ActiveOptionsOSS;
  const char *delim = ",";
  std::copy(ActiveOptions.begin(), ActiveOptions.end(),
	    std::ostream_iterator<std::string>(ActiveOptionsOSS, delim));
  std::string ActiveOptionsStr = ActiveOptionsOSS.str();

  bool compat = true;
  switch(reply->type) {
  case REDIS_REPLY_NIL:
    // no version set
    freeReplyObject(reply);
    reply = static_cast<redisReply*>(redisCommand(Ctx, "SET cachetype %s", ActiveOptionsStr));
    // TODO: Factor out all such snippets
    if (!reply || Ctx->err) {
      llvm::report_fatal_error((llvm::StringRef)"Redis error: " + Ctx->errstr);
    }
    break;
  case REDIS_REPLY_STRING:
  {
    llvm::StringRef value = reply->str;
    if (value != ActiveOptionsStr) {
      compat = false;
    }
  }
  break;
  default:
    compat = false;
  }

  return compat;
}

void KVStore::KVImpl::hIncrBy(llvm::StringRef Key, llvm::StringRef Field,
                              int Incr) {
  redisReply *reply = (redisReply *)redisCommand(Ctx, "HINCRBY %s %s 1",
                                                 Key.data(), Field.data());
  if (!reply || Ctx->err) {
    llvm::report_fatal_error((llvm::StringRef)"Redis error: " + Ctx->errstr);
  }
  if (reply->type != REDIS_REPLY_INTEGER) {
    llvm::report_fatal_error(
        "Redis protocol error for static profile, didn't expect reply type "
        + std::to_string(reply->type));
  }
  freeReplyObject(reply);
}

bool KVStore::KVImpl::hGet(llvm::StringRef Key, llvm::StringRef Field,
                           std::string &Value) {
  redisReply *reply = (redisReply *)redisCommand(Ctx, "HGET %s %s", Key.data(),
                                                 Field.data());
  if (!reply || Ctx->err) {
    llvm::report_fatal_error((llvm::StringRef)"Redis error: " + Ctx->errstr);
  }
  if (reply->type == REDIS_REPLY_NIL) {
    freeReplyObject(reply);
    return false;
  } else if (reply->type == REDIS_REPLY_STRING) {
    Value = reply->str;
    freeReplyObject(reply);
    return true;
  } else {
    llvm::report_fatal_error(
        "Redis protocol error for cache lookup, didn't expect reply type " +
        std::to_string(reply->type));
  }
}

void KVStore::KVImpl::hSet(llvm::StringRef Key, llvm::StringRef Field,
                              llvm::StringRef Value) {
  redisReply *reply = (redisReply *)redisCommand(Ctx, "HSET %s %s %s",
      Key.data(), Field.data(), Value.data());
  if (!reply || Ctx->err) {
    llvm::report_fatal_error((llvm::StringRef)"Redis error: " + Ctx->errstr);
  }
  if (reply->type != REDIS_REPLY_INTEGER) {
    llvm::report_fatal_error(
        "Redis protocol error for cache fill, didn't expect reply type " +
        std::to_string(reply->type));
  }
  freeReplyObject(reply);
}

KVStore::KVStore() : Impl (new KVImpl) {}

KVStore::~KVStore() {}

void KVStore::hIncrBy(llvm::StringRef Key, llvm::StringRef Field, int Incr) {
  Impl->hIncrBy(Key, Field, Incr);
}

bool KVStore::hGet(llvm::StringRef Key, llvm::StringRef Field,
                   std::string &Value) {
  return Impl->hGet(Key, Field, Value);
}

void KVStore::hSet(llvm::StringRef Key, llvm::StringRef Field,
                   llvm::StringRef Value) {
  Impl->hSet(Key, Field, Value);
}

}
