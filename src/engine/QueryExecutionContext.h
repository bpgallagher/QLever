// Copyright 2011, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Björn Buchhold (buchhold@informatik.uni-freiburg.de)

#pragma once

#include <string>
#include <vector>
#include "../util/LRUCache.h"
#include "../util/Log.h"
#include "./ResultTable.h"
#include "./Engine.h"
#include "./IndexMock.h"
#include "./Constants.h"

using std::string;
using std::vector;

typedef ad_utility::LRUCache<string, ResultTable> SubtreeCache;

// Execution context for queries.
// Holds references to index and engine, implements caching.
class QueryExecutionContext {
public:

  QueryExecutionContext(const IndexMock& index, const Engine& engine) :
      _subtreeCache(NOF_SUBTREES_TO_CACHE),
      _index(index), _engine(engine) {
  }

  ResultTable* getCachedResultForQueryTree(
      const string& queryAsString) {
    return &_subtreeCache[queryAsString];
  }

  const Engine& getEngine() const {
    return _engine;
  }

  const IndexMock& getIndex() const {
    return _index;
  }

private:

  SubtreeCache _subtreeCache;
  const IndexMock& _index;
  const Engine& _engine;
};
