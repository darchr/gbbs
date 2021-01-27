// This code is part of the project "Theoretically Efficient Parallel Graph
// Algorithms Can Be Fast and Scalable", presented at Symposium on Parallelism
// in Algorithms and Architectures, 2018.
// Copyright (c) 2018 Laxman Dhulipala, Guy Blelloch, and Julian Shun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all  copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#pragma once

#include <queue>
#include <vector>
#include "clustered_graph.h"
#include "gbbs/gbbs.h"

namespace gbbs {
namespace greedy_exact {

template <class Weights,
          // provides get_weight : () -> Weights::weight_type which is the
          // datatype that is stored for each edge incident to a _cluster_. This
          // could involve more than simply storing the underlying weight, or
          // could internally be a representation like gbbs::empty.
          template <class WW> class w_vertex,
          class IW,  // the weight type of the underlying graph
          class LinkageFn>
void HAC(symmetric_graph<w_vertex, IW>& G, Weights& weights, LinkageFn& linkage) {
  using W = typename Weights::weight_type;  // potentially a more complex type than IW

  using pq_elt = std::tuple<uintE, uintE, W>;
  using edge = std::tuple<uintE, W>;

  using clustered_graph = clustering::clustered_graph<Weights, IW, w_vertex>;
  using cluster_id = clustering::cluster_id;

  size_t n = G.n;

  // This object stores a representation of both the original clusters and the
  // clusters formed throughout the clustering process.
  auto CG = clustered_graph(G, weights);

  // PQ stores O(n) values --- one per cluster. The PQ values are (cluster,
  // cluster, weight) triples.
  auto pq_cmp = [](const pq_elt& l, const pq_elt& r) {
    return std::get<2>(l) < std::get<2>(r);
  };
  std::priority_queue<pq_elt, std::vector<pq_elt>, decltype(pq_cmp)> pq(pq_cmp);

  //Compute the min-weight edge incident to each original vertex
  parlay::sequence<edge> min_neighbors(
      n, std::make_tuple(UINT_E_MAX, std::numeric_limits<W>::max()));
  parallel_for(0, n, [&](size_t i) {
      auto clust = CG.get_cluster(i);
      auto edge_option = clust.minWeightEdge();
      if (edge_option.has_value()) {
        min_neighbors[i] = edge_option.value();
      }
  }, 1);

  // Insert the min-weight edges computed in the previous step into the PQ.
  timer pusht;
  pusht.start();
  for (size_t i = 0; i < n; i++) {
    if (std::get<0>(min_neighbors[i]) != UINT_E_MAX) {
      auto[v, wgh] = min_neighbors[i];
      pq.push({i, v, wgh});
    }
  }
  pusht.stop();
  pusht.reportTotal("push time");
  std::cout << "pq_size = " << pq.size() << std::endl;

  size_t clusters_remaining = pq.size();
  timer get_t;
  timer unite_t;
  while (clusters_remaining > 0 && pq.size() > 0) {
    auto [u, v, wgh] = pq.top();
    pq.pop();

    // std::cout << "popped u = " << u << " v = " << v << " wgh = " << wgh << " from pq." << std::endl;

    // Check if either u or v is already clustered.
    if (CG.is_clustered(u) || CG.is_clustered(v)) {
      continue;  // Edge is no longer valid; continue.
    }

    // std::cout << "uniting u = " << u << " and v = " << v << std::endl;
    // Otherwise, merge u and v into the same cluster.
    unite_t.start();
    cluster_id uv = CG.unite_clusters(u, v, wgh, linkage);
    unite_t.stop();

    // Extract the min-weight edge from the new cluster.
    auto uv_c = CG.get_cluster(uv);
    auto edge_option = uv_c.minWeightEdge();
    if (edge_option.has_value()) {
      auto [ngh, wgh] = edge_option.value();
      pq.push({uv, ngh, wgh});
    }
  }
  get_t.reportTotal("get time");
  unite_t.reportTotal("unite_clusters time");

  gbbs::clustering::union_t.reportTotal("total union time");
}

}  // namespace greedy_exact
}  // namespace gbbs
