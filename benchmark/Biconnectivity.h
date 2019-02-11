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

#include "CC.h"
#include "lib/dyn_arr.h"
#include "lib/random.h"
#include "lib/sample_sort.h"
#include "lib/sparse_table.h"
#include "lib/speculative_for.h"
#include "ligra.h"
#include "oldlib/benchIO.h"

namespace bc {
constexpr uintE TOP_BIT = ((uintE)INT_E_MAX) + 1;
constexpr uintE VAL_MASK = INT_E_MAX;
};  // namespace bc

using labels = std::tuple<uintE, uintE>;

// Used to compute the size of each subtree (leaffix)
struct AugF {
  uintE* sizes;
  intE* cts;
  AugF(uintE* _sizes, intE* _cts) : sizes(_sizes), cts(_cts) {}
  bool update(const uintE& s, const uintE& d) {
    sizes[d] += sizes[s];
    cts[d]--;
    if (!cts[d]) {
      return true;
    }
    return false;
  }
  bool updateAtomic(const uintE& s, const uintE& d) {
    writeAdd(&sizes[d], sizes[s]);
    intE res = writeAdd(&cts[d], -1);
    return (res == 0);
  }
  bool cond(const uintE& d) { return true; }
};

// Used to compute the (low, hi) values of each subtree (leaffix)
struct MinMaxF {
  labels* MM;  // min and max
  intE* cts;
  MinMaxF(labels* _MM, intE* _cts) : MM(_MM), cts(_cts) {}
  bool update(const uintE& s, const uintE& d) {
    labels lab_s = MM[s];
    uintE low = std::get<0>(lab_s), high = std::get<1>(lab_s);
    if (low < std::get<0>(MM[d])) {
      std::get<0>(MM[d]) = low;
    }
    if (high > std::get<1>(MM[d])) {
      std::get<1>(MM[d]) = high;
    }
    uintE ct = cts[d] - 1;
    cts[d] = ct;
    return ct == 0;
  }
  bool updateAtomic(const uintE& s, const uintE& d) {
    labels lab_s = MM[s];
    uintE low = std::get<0>(lab_s), high = std::get<1>(lab_s);
    if (low < std::get<0>(MM[d])) {
      writeMin(&std::get<0>(MM[d]), low);
    }
    if (high > std::get<1>(MM[d])) {
      writeMax(&std::get<1>(MM[d]), high);
    }
    intE res = writeAdd(&cts[d], -1);
    return (res == 0);
  }
  bool cond(const uintE& d) { return true; }
};

template <template <typename W> class vertex, class W, class Seq>
inline std::tuple<labels*, uintE*, uintE*> preorder_number(graph<vertex<W>>& GA,
                                                           uintE* Parents,
                                                           Seq& Sources) {
  using w_vertex = vertex<W>;
  size_t n = GA.n;
  using edge = std::tuple<uintE, uintE>;
  auto out_edges = sequence<edge>(
      n, [](size_t i) { return std::make_tuple(UINT_E_MAX, UINT_E_MAX); });
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i) {
    uintE p_i = Parents[i];
    if (p_i != i) {
      out_edges[i] = std::make_tuple(p_i, i);
    }
  });

  auto edges = pbbs::filter(
      out_edges, [](const edge& e) { return std::get<0>(e) != UINT_E_MAX; });
  out_edges.clear();
  auto sort_tup = [](const edge& l, const edge& r) { return l < r; };
  pbbs::sample_sort(edges.start(), edges.size(), sort_tup);

  auto starts = sequence<uintE>(n + 1, [](size_t i) { return UINT_E_MAX; });
  par_for(0, edges.size(), pbbs::kSequentialForThreshold, [&] (size_t i) {
    if (i == 0 || std::get<0>(edges[i]) != std::get<0>(edges[i - 1])) {
      starts[std::get<0>(edges[i])] = i;
    }
  });
  starts[n] = edges.size();

  timer seq;
  seq.start();
  for (long i = starts.size() - 1; i >= 0; i--) {
    if (starts[i] == UINT_E_MAX) {
      starts[i] = starts[i + 1];
    }
  }
  seq.stop();
  seq.reportTotal("seq time");

  auto nghs = sequence<uintE>(
      edges.size(), [&](size_t i) { return std::get<1>(edges[i]); });
  edges.clear();

  timer augs;
  augs.start();
  // Create directed BFS tree
  using vtx = asymmetricVertex<pbbs::empty>;
  auto v = pbbs::new_array_no_init<vtx>(n);
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i) {
    uintE out_off = starts[i];
    uintE out_deg = starts[i + 1] - out_off;
    v[i].setOutDegree(out_deg);
    v[i].setOutNeighbors(
        (std::tuple<uintE, pbbs::empty>*)(nghs.start() + out_off));
    uintE parent = Parents[i];
    if (parent != i) {
      v[i].setInDegree(1);
      v[i].setInNeighbors(((std::tuple<uintE, pbbs::empty>*)(Parents + i)));
    } else {
      v[i].setInDegree(0);
      v[i].setInNeighbors(nullptr);
    }
  });
  auto Tree = graph<vtx>(v, n, nghs.size(), []() {});

  // 1. Leaffix for Augmented Sizes
  auto aug_sizes = sequence<uintE>(n, [](size_t i) { return 1; });
  auto cts =
      sequence<intE>(n, [&](size_t i) { return Tree.V[i].getOutDegree(); });
  auto leaf_f = [&](size_t i) {
    auto s_i = starts[i];
    auto s_n = starts[i + 1];
    size_t deg = s_n - s_i;
    return (Parents[i] != i) && deg == 0;
  };
  auto leaf_im = make_sequence<bool>(n, leaf_f);
  auto leafs = pbbs::pack_index<uintE>(leaf_im);

  auto vs = vertexSubset(n, leafs.size(), leafs.start());
  size_t rds = 0, tv = 0;
  while (!vs.isEmpty()) {
    rds++;
    tv += vs.size();
    // histogram or write-add parents, produce next em.
    auto output = edgeMap(
        Tree, vs, wrap_em_f<pbbs::empty>(AugF(aug_sizes.start(), cts.start())),
        -1, in_edges | sparse_blocked);
    if (rds > 1) {
      vs.del();  // don't delete leafs.
    }
    vs = output;
    output.toSparse();
  }
  augs.stop();
  augs.reportTotal("aug size time");

  // Optional: Prefix sum over sources with aug-size (if we want distinct
  // preorder #s)
  auto s_copy = Sources.copy(Sources); //Sources.copy();

  timer pren;
  pren.start();
  auto PN = sequence<uintE>(n);
  vs = vertexSubset(n, s_copy.size(), s_copy.get_array());
  par_for(0, Sources.size(), pbbs::kSequentialForThreshold, [&] (size_t i) {
    uintE v = s_copy[i];
    PN[v] = 0;
  });
  rds = 0;
  tv = 0;
  while (!vs.isEmpty()) {
    rds++;
    tv += vs.size();
    auto offsets = sequence<uintE>(vs.size(), [&](size_t i) {
      uintE v = vs.s[i];
      return Tree.V[v].getOutDegree();
    });
    auto tot = pbbs::scan_add(offsets, offsets);
    auto next_vs = sequence<uintE>(tot);
    par_for(0, vs.size(), pbbs::kSequentialForThreshold, [&] (size_t i) {
      uintE v = vs.s[i];
      uintE off = offsets[i];
      uintE deg_v = Tree.V[v].getOutDegree();
      uintE preorder_number = PN[v] + 1;

      // should be tuned
      if (deg_v < 4000) {
        // Min and max in any vertex are [PN[v], PN[v] + aug_sizes[v])
        for (size_t j = 0; j < deg_v; j++) {
          uintE ngh = Tree.V[v].getOutNeighbor(j);
          PN[ngh] = preorder_number;
          preorder_number += aug_sizes[ngh];
          next_vs[off + j] = ngh;
        }
      } else {
        auto A = sequence<uintE>(deg_v);
        par_for(0, deg_v, [&] (size_t j) {
          uintE ngh = Tree.V[v].getOutNeighbor(j);
          A[j] = aug_sizes[ngh];
        });
        pbbs::scan_add(A, A);
        par_for(0, deg_v, [&] (size_t j) {
          uintE ngh = Tree.V[v].getOutNeighbor(j);
          uintE pn = preorder_number + A[j];
          PN[ngh] = pn;
          next_vs[off + j] = ngh;
        });
      }
    });
    vs.del();
    vs = vertexSubset(n, next_vs.size(), next_vs.get_array());
  }
  pren.stop();
  pren.reportTotal("preorder number from sizes time");

  timer map_e;
  map_e.start();
  // Map all edges and update labels.
  auto MM = sequence<labels>(n, [&](size_t i) {
    uintE pn_i = PN[i];
    return std::make_tuple(pn_i, pn_i + aug_sizes[i] - 1);
  });
  // Note that we have to exclude tree edges: it's all edges spliced in with
  // the exception of tree edges.
  auto map_f = [&](const uintE& u, const uintE& v, const W& wgh) {
    if (u < v) {  // see if assigning roles helps temp loc later.
      if (u == Parents[v] || v == Parents[u]) {
        return;
      }
      uintE p_u = PN[u];
      uintE p_v = PN[v];
      if (p_u < std::get<0>(MM[v])) {
        writeMin(&std::get<0>(MM[v]), p_u);
      } else if (p_u > std::get<1>(MM[v])) {
        writeMax(&std::get<1>(MM[v]), p_u);
      }
      if (p_v < std::get<0>(MM[u])) {
        writeMin(&std::get<0>(MM[u]), p_v);
      } else if (p_v > std::get<1>(MM[u])) {
        writeMax(&std::get<1>(MM[u]), p_v);
      }
    }
  };
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i)
                  { GA.V[i].mapOutNgh(i, map_f); });
  map_e.stop();
  map_e.reportTotal("map edges time");

  timer leaff;
  leaff.start();
  // 1. Leaffix to update min/max
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i)
                  { cts[i] = Tree.V[i].getOutDegree(); });

  vs = vertexSubset(n, leafs.size(), leafs.get_array());
  rds = 0, tv = 0;
  while (!vs.isEmpty()) {
    rds++;
    tv += vs.size();
    // histogram or write-add parents, produce next em.
    auto output = edgeMap(
        Tree, vs, wrap_em_f<pbbs::empty>(MinMaxF(MM.start(), cts.start())), -1,
        in_edges | sparse_blocked);
    vs.del();
    vs = output;
  }
  // Delete tree
  pbbs::free_array(v);
  nghs.clear();
  leaff.stop();
  leaff.reportTotal("leaffix to update min max time");

  // Return the preorder numbers, the (min, max) for each subtree and the
  // augmented size for each subtree.
  return std::make_tuple(MM.get_array(), PN.get_array(), aug_sizes.get_array());
}

// Deterministic version
struct BC_BFS_F {
  uintE* Parents;
  BC_BFS_F(uintE* _Parents) : Parents(_Parents) {}
  inline bool update(uintE s, uintE d) {  // Update
    Parents[d] = s;
    return true;
  }
  inline bool updateAtomic(uintE s, uintE d) {  // Atomic version of Update
    return CAS(&Parents[d], UINT_E_MAX, s);
  }
  // Cond function checks if vertex has been visited yet
  inline bool cond(uintE d) { return (Parents[d] == UINT_E_MAX); }
};

template <template <typename W> class vertex, class W, class VS>
inline uintE* multi_bfs(graph<vertex<W>>& GA, VS& frontier) {
  size_t n = GA.n;
  auto Parents = sequence<uintE>(n, [](size_t i) { return UINT_E_MAX; });
  frontier.toSparse();
  par_for(0, frontier.size(), 2000, [&] (size_t i) {
    uintE v = frontier.s[i];
    Parents[v] = v;
  });
  while (!frontier.isEmpty()) {
    vertexSubset output =
        edgeMap(GA, frontier, wrap_em_f<W>(BC_BFS_F(Parents.start())), -1,
                sparse_blocked);
    frontier.del();
    frontier = output;
  }
  return Parents.get_array();
}

template <class Seq>
inline sequence<uintE> cc_sources(Seq& labels) {
  size_t n = labels.size();
  auto flags = sequence<uintE>(n + 1, [&](size_t i) { return UINT_E_MAX; });
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i) {
    uintE label = labels[i];
    writeMin(&flags[label], (uintE)i);
  });
  // Get min from each component
  return pbbs::filter(flags, [](uintE v) { return v != UINT_E_MAX; });
}

template <template <class W> class vertex, class W>
inline std::tuple<uintE*, uintE*> critical_connectivity(
    graph<vertex<W>>& GA, uintE* Parents, labels* MM_A, uintE* PN_A,
    uintE* aug_sizes_A, char* out_f) {
  timer ccc;
  ccc.start();
  size_t n = GA.n;
  auto MM = sequence<labels>(MM_A, n);
  auto PN = sequence<uintE>(PN_A, n);
  auto aug_sizes = sequence<uintE>(aug_sizes_A, n);

  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i) {
    uintE pi = Parents[i];
    if (pi != i) {
      labels clab = MM[i];
      uintE first_p = PN[pi];
      uintE last_p = first_p + aug_sizes[pi];  // not inclusive
      if ((first_p <= std::get<0>(clab)) &&
          (std::get<1>(clab) < last_p)) {  // critical
        Parents[i] |= bc::TOP_BIT;
      }
    }
  });

  auto not_critical_edge = [&](const uintE& u, const uintE& v) {
    uintE e = Parents[u];
    uintE p_u = (e & bc::VAL_MASK);
    if (v == p_u) {
      return !(e & bc::TOP_BIT);
    }
    e = Parents[v];
    uintE p_v = (e & bc::VAL_MASK);
    if (p_v == u) {
      return !(e & bc::TOP_BIT);
    }
    return true;
  };

  timer ccpred;
  ccpred.start();
  // 1. Pack out all critical edges
  auto active = pbbs::new_array_no_init<bool>(n);
  par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i)
                  { active[i] = true; });
  auto vs_active = vertexSubset(n, n, active);
  auto pack_predicate = [&](const uintE& src, const uintE& ngh, const W& wgh) {
    return not_critical_edge(src, ngh);
  };
  edgeMapFilter(GA, vs_active, pack_predicate, pack_edges | no_output);
  vs_active.del();

  // 2. Run CC on the graph with the critical edges removed to compute
  // a unique label for each biconnected component
  auto cc = cc::CC(GA, 0.2, true);
  ccpred.stop();
  ccpred.reportTotal("cc pred time");

  // Note that counting components here will count initially isolated vertices
  // as distinct components.
  //  auto flags = sequence<uintE>(n+1, [&] (size_t i) { return 0; });
  //  parallel_for(size_t i=0; i<n; i++) {
  //    if (!flags[cc[i]]) {
  //      flags[cc[i]] = 1;
  //    }
  //  }
  //  pbbs::scan_add(flags, flags);
  //  size_t n_cc = flags[n];
  //  std::cout << "num biconnected components, including isolated vertices = "
  //  << flags[n] << "\n";

  if (out_f) {
    std::cout << "Writing labels to file: " << out_f << "\n";
    std::ofstream out(out_f, std::ofstream::out);
    if (!out.is_open()) {
      std::cout << "Unable to open file " << out_f << "\n";
      exit(0);
    }

    auto tups = sequence<std::pair<uintE, uintE>>(n);
    par_for(0, n, pbbs::kSequentialForThreshold, [&] (size_t i)
                    { tups[i] = std::make_pair(Parents[i] & bc::VAL_MASK, cc[i]); });

    benchIO::writeArrayToStream(out, tups.start(), n);
    //    for (size_t i = 0; i < n; i++) {
    //      out << (Parents[i] & bc::VAL_MASK) << " " << cc[i] << "\n";
    //    }
    out.close();
  }
  std::cout << "BC done"
            << "\n";
  pbbs::free_array(MM_A);
  pbbs::free_array(PN_A);
  pbbs::free_array(aug_sizes_A);
  ccc.stop();
  ccc.reportTotal("critical conn time");
  return std::make_tuple(Parents, cc.get_array());
}

// CC -> BFS from one source from each component = set of BFS trees in a single
// array
template <class vertex>
inline std::tuple<uintE*, uintE*> Biconnectivity(graph<vertex>& GA,
                                                 char* out_f = 0) {
  size_t n = GA.n;

  timer fcc;
  fcc.start();
  sequence<uintE> Components = cc::CC(GA, 0.2, false);
  fcc.stop();
  fcc.reportTotal("first cc");

  timer sc;
  sc.start();
  auto Sources = cc_sources(Components);
  Components.clear();

  auto Sources_copy = Sources.copy(Sources); //Sources.copy();
  auto Centers = vertexSubset(n, Sources.size(), Sources.get_array());
  auto Parents = multi_bfs(GA, Centers);
  sc.stop();
  sc.reportTotal("sc, multibfs time");

  // Returns ((min, max), preorder#, and augmented sizes) of each subtree.
  timer pn;
  pn.start();

  labels* min_max;
  uintE* preorder_num;
  uintE* aug_sizes;
  std::tie(min_max, preorder_num, aug_sizes) =
      preorder_number(GA, Parents, Sources_copy);
  pn.stop();
  pn.reportTotal("preorder time");

  return critical_connectivity(GA, Parents, min_max, preorder_num, aug_sizes,
                               out_f);
}