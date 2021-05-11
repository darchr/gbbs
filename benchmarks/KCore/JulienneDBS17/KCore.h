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

//#include "gbbs/bucket.h"
//#include "gbbs/edge_map_reduce.h"
//#include "gbbs/pbbslib/dyn_arr.h"
#include "gbbs/gbbs.h"
#include "gbbs/julienne.h"

namespace gbbs {

#ifdef ACCESS_OBSERVER
    struct observer access_observer("livejournal-kcore.txt");
    std::stringstream annotation;
#endif

template <class Graph>
inline sequence<uintE> KCore(Graph &G, size_t num_buckets = 16) {
    const size_t n = G.n;
    auto D = sequence<uintE>(
        n, [&](size_t i) { return G.get_vertex(i).out_degree(); });
    auto em = hist_table<uintE, uintE>(std::make_tuple(UINT_E_MAX, 0),
                                       (size_t)G.m / 50);
    auto b = make_vertex_buckets(n, D, increasing, num_buckets);
    timer bt;

    size_t finished = 0, rho = 0, k_max = 0;
    while (finished != n) {
        bt.start();
        auto bkt = b.next_bucket();
        bt.stop();
        auto active = vertexSubset(n, bkt.identifiers);
        uintE k = bkt.id;
        finished += active.size();
        k_max = std::max(k_max, bkt.id);

        auto apply_f = [&](const std::tuple<uintE, uintE> &p)
            -> const std::optional<std::tuple<uintE, uintE>> {
            uintE v = std::get<0>(p), edgesRemoved = std::get<1>(p);
            uintE deg = D[v];
            if (deg > k) {
                uintE new_deg = std::max(deg - edgesRemoved, k);
                D[v] = new_deg;
                return wrap(v, b.get_bucket(new_deg));
            }
            return std::nullopt;
        };

        auto cond_f = [](const uintE &u) { return true; };
        vertexSubsetData<uintE> moved =
            nghCount(G, active, cond_f, apply_f, em, no_dense);
        bt.start();
        if (moved.dense()) {
            b.update_buckets(moved.get_fn_repr(), n);
        } else {
            b.update_buckets(moved.get_fn_repr(), moved.size());
        }

        bt.stop();
        moved.del();
        active.del();
        rho++;
    }
    std::cout << "### rho = " << rho << " k_{max} = " << k_max << "\n";
    debug(bt.reportTotal("bucket time"););
    b.del();
    return D;
}

template <class W> struct kcore_fetch_add {
    uintE *er;
    uintE *D;
    uintE k;
    kcore_fetch_add(uintE *_er, uintE *_D, uintE _k) : er(_er), D(_D), k(_k) {}
    inline std::optional<uintE> update(const uintE &s, const uintE &d,
                                       const W &w) {
        er[d]++;
        if (er[d] == 1) {
            return std::optional<uintE>((uintE)0);
        }
        return std::nullopt;
    }
    inline std::optional<uintE> updateAtomic(const uintE &s, const uintE &d,
                                             const W &wgh) {
        if (pbbslib::fetch_and_add(&er[d], (uintE)1) == 1) {
            return std::optional<uintE>((uintE)0);
        }
        return std::nullopt;
    }
    inline bool cond(uintE d) { return D[d] > k; }
};

// LOOK IN HERE
template <class Graph>
inline sequence<uintE> KCore_FA(Graph &G, size_t num_buckets = 16) {
 #ifdef ACCESS_OBSERVER
        annotation << "BEGIN: Kcore_FA() Init,GraphAddress="
                   << &G
                   << ",GraphSize="
                   << sizeof(G)
                   << ",VertexArrayAddress="
                   << G.v_data
                   << ",VertexArraySize="
                   << (sizeof(G.v_data[0]) * G.num_vertices())
                   << ",EdgeArrayAddress="
                   << (int*)&(G.e0[0])
                   << ",EdgeArraySize="
                   << (sizeof(G.e0[0]) * G.num_edges());
        access_observer.write(
            annotation.str()
        );
        std::stringstream().swap(annotation);
#endif
    using W = typename Graph::weight_type;
    const size_t n = G.n;
    // Grabs degree of a vertex
    auto D = sequence<uintE>(
        n, [&](size_t i) { return G.get_vertex(i).out_degree(); });
    auto ER = sequence<uintE>(n, [&](size_t i) { return 0; });

    // Grouping vertices by degree
    auto b = make_vertex_buckets(n, D, increasing, num_buckets);

    size_t finished = 0;
    size_t rho = 0;
    size_t k_max = 0;
   // Go through every degree bucket and update?
    while (finished != n) {
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: Bucket Init";
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        auto bkt = b.next_bucket();
        auto active = vertexSubset(n, bkt.identifiers);
        // Bucket degree???
        uintE k = bkt.id;
        // Will eventually go through all nodes
        finished += active.size();
        k_max = std::max(k_max, bkt.id);

        auto apply_f = [&](const uintE v, uintE &bkt_to_modify) -> void {
            uintE deg = D[v];
            uintE edgesRemoved = ER[v];
            ER[v] = 0;
            uintE new_deg = std::max(deg - edgesRemoved, k);
            D[v] = new_deg;
            bkt_to_modify = b.get_bucket(deg, new_deg);
        };

// Functions of interest
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: edgeMap() Init"
                   << ",SparseAddress="
                   << active.s
                   << ",SparseSize="
                   << (sizeof(active.s[0]) * active.size())
                   << ",DenseAddress="
                   << active.d
                   << ",DenseSize="
                   << active.numVertices()
                   << ",CurrentlyDense="
                   << (active.dense() ? "True" : "False");
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        auto moved = edgeMapData<uintE>(
            G, active, kcore_fetch_add<W>(ER.begin(), D.begin(), k));
//#ifdef ACCESS_OBSERVER
//        annotation << "END: edgeMap()"
//                   << ",SparseAddress="
//                   << active.s
//                   << ",SparseSize="
//                   << (sizeof(active.s[0]) * active.size())
//                   << ",DenseAddress="
//                   << active.d
//                   << ",DenseSize="
//                   << active.numVertices()
//                   << ",CurrentlyDense="
//                   << (active.dense() ? "True" : "False");
//        access_observer.write(annotation.str());
//        std::stringstream().swap(annotation);
//#endif
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: vertexMap()"
                   << ",SparseAddress="
                   << moved.s
                   << ",SparseSize="
                   << (sizeof(moved.s[0]) * moved.size())
                   << ",DenseAddress="
                   << moved.d
                   << ",DenseSize="
                   << moved.numVertices()
                   << ",CurrentlyDense="
                   << (moved.dense() ? "True" : "False");
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        vertexMap(moved, apply_f);
//#ifdef ACCESS_OBSERVER
//        annotation << "END: vertexMap()"
//                   << ",SparseAddress="
//                   << moved.s
//                   << ",SparseSize="
//                   << (sizeof(moved.s[0]) * moved.size())
//                   << ",DenseAddress="
//                   << moved.d
//                   << ",DenseSize="
//                   << moved.numVertices()
//                   << ",CurrentlyDense="
//                   << (moved.dense() ? "True" : "False");
//        access_observer.write(annotation.str());
//        std::stringstream().swap(annotation);
//#endif

// This is also interesting
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: update_buckets()";
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        if (moved.dense()) {
            b.update_buckets(moved.get_fn_repr(), n);
        } else {
            b.update_buckets(moved.get_fn_repr(), moved.size());
        }
        moved.del();
        active.del();
        rho++;
    }
#ifdef ACCESS_OBSERVER
    annotation << "END: Kcore_FA()";
    access_observer.write(
        annotation.str()
    );
    std::stringstream().swap(annotation);
    access_observer.save();
#endif
    b.del();
    std::cout << "### rho = " << rho << " k_{max} = " << k_max << "\n";
    return D;
}

template <class Graph>
inline pbbslib::dyn_arr<uintE> DegeneracyOrder(Graph &G,
                                               size_t num_buckets = 16) {
    const size_t n = G.n;
    auto D = sequence<uintE>(
        n, [&](size_t i) { return G.get_vertex(i).out_degree(); });

    auto em = EdgeMap<uintE, Graph>(G, std::make_tuple(UINT_E_MAX, 0),
                                    (size_t)G.m / 50);
    auto b = make_vertex_buckets(n, D, increasing, num_buckets);
    timer bt;

    auto degeneracy_order = pbbslib::dyn_arr<uintE>(n);

    size_t finished = 0, rho = 0, k_max = 0;
    while (finished != n) {
        bt.start();
        auto bkt = b.next_bucket();
        bt.stop();
        auto active = vertexSubset(n, bkt.identifiers);
        uintE k = bkt.id;
        finished += active.size();
        k_max = std::max(k_max, bkt.id);

        auto active_seq = pbbs::delayed_seq<uintE>(
            active.size(), [&](size_t i) { return active.s[i]; });
        degeneracy_order.copyIn(active_seq, active.size());

        auto apply_f = [&](const std::tuple<uintE, uintE> &p)
            -> const std::optional<std::tuple<uintE, uintE>> {
            uintE v = std::get<0>(p), edgesRemoved = std::get<1>(p);
            uintE deg = D[v];
            if (deg > k) {
                uintE new_deg = std::max(deg - edgesRemoved, k);
                D[v] = new_deg;
                return wrap(v, b.get_bucket(new_deg));
            }
            return std::nullopt;
        };

        vertexSubsetData<uintE> moved =
            em.template edgeMapCount_sparse<uintE>(active, apply_f);
        bt.start();
        if (moved.dense()) {
            b.update_buckets(moved.get_fn_repr(), n);
        } else {
            b.update_buckets(moved.get_fn_repr(), moved.size());
        }

        bt.stop();
        moved.del();
        active.del();
        rho++;
    }
    std::cout << "### rho = " << rho << " k_{max} = " << k_max << "\n";
    debug(bt.reportTotal("bucket time"););
    b.del();
    return degeneracy_order;
}

} // namespace gbbs
