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

#include "gbbs/gbbs.h"

namespace gbbs {

#ifdef ACCESS_OBSERVER
    struct observer access_observer("livejournal-bfs-hd.txt");
    std::stringstream annotation;
#endif

template <class W> struct BFS_F {
    uintE *Parents;
    BFS_F(uintE *_Parents) : Parents(_Parents) {}
    inline bool update(const uintE &s, const uintE &d, const W &w) const {
        if (Parents[d] == UINT_E_MAX) {
            Parents[d] = s;
            return 1;
        } else {
            return 0;
        }
    }
    inline bool updateAtomic(const uintE &s, const uintE &d, const W &w) const {
        return (pbbslib::atomic_compare_and_swap(&Parents[d], UINT_E_MAX, s));
    }
    inline bool cond(const uintE &d) const {
        return (Parents[d] == UINT_E_MAX);
    }
};

template <template <class W> class vertex, class W>
inline sequence<uintE> BFS(symmetric_graph<vertex, W> &G, uintE src) {
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: BFS() Init,GraphAddress="
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
    /* Creates Parents array, initialized to all -1, except for src. */
    auto Parents = sequence<uintE>(G.n, [&](size_t i) { return UINT_E_MAX; });
    Parents[src] = src;

    vertexSubset Frontier(G.n, src);
    size_t reachable = 0;
    while (!Frontier.isEmpty()) {
        std::cout << Frontier.size() << "\n";
        reachable += Frontier.size();
#ifdef ACCESS_OBSERVER
        annotation << "BEGIN: edgeMap() Init"
                   << ",SparseAddress="
                   << Frontier.s
                   << ",SparseSize="
                   << (sizeof(Frontier.s[0]) * Frontier.size())
                   << ",DenseAddress="
                   << Frontier.d
                   << ",DenseSize="
                   << Frontier.numVertices()
                   << ",CurrentlyDense="
                   << (Frontier.dense() ? "True" : "False");
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        vertexSubset output =
            neighbor_map(G, Frontier, BFS_F<W>(Parents.begin()), -1,
                         sparse_blocked | dense_parallel);
//#ifdef ACCESS_OBSERVER
//        annotation << "END: edgeMap()"
//                   << ",SparseAddress="
//                   << Frontier.s
//                   << ",SparseSize="
//                   << (sizeof(Frontier.s[0]) * Frontier.size())
//                   << ",DenseAddress="
//                   << Frontier.d
//                   << ",DenseSize="
//                   << Frontier.numVertices()
//                   << ",CurrentlyDense="
//                   << (Frontier.dense() ? "True" : "False");
//        access_observer.write(annotation.str());
//        std::stringstream().swap(annotation);
//#endif

        Frontier.del();
        Frontier = output;
    }
    Frontier.del();
    std::cout << "Reachable: " << reachable << "\n";
#ifdef ACCESS_OBSERVER
    annotation << "END: BFS()";
    access_observer.write(
        annotation.str()
    );
    std::stringstream().swap(annotation);
    access_observer.save();
#endif
    return Parents;
}

template <template <class W> class vertex, class W>
inline sequence<uintE> BFS(asymmetric_graph<vertex, W> &G, uintE src) {
    /* Creates Parents array, initialized to all -1, except for src. */
    auto Parents = sequence<uintE>(G.n, [&](size_t i) { return UINT_E_MAX; });
    Parents[src] = src;

#ifdef ACCESS_OBSERVER
        annotation << "Graph type: " << "asymmetric";
        access_observer.write(
            annotation.str()
        );
        std::stringstream().swap(annotation);
#endif
    vertexSubset Frontier(G.n, src);
    size_t reachable = 0;
    while (!Frontier.isEmpty()) {
        std::cout << Frontier.size() << "\n";
        reachable += Frontier.size();
#ifdef ACCESS_OBSERVER
        annotation << "edgeMap() Init,"; /*<< sizeof(G) << ","
                   << sizeof(*(G.v_out_data)) << "," << sizeof(*(G.v_in_data))
                   << "," << sizeof(*(G.out_edges_0)) << ","
                   << sizeof(*(G.in_edges_0)) << "," << sizeof(G.out_edges_0[1])
                   << "," << sizeof(Frontier) << ","
                   << Frontier.get_out_degrees() << ","
                   << (Frontier.dense() ? "True" : "False");*/
        access_observer.write(annotation.str());
        std::stringstream().swap(annotation);
#endif
        vertexSubset output =
            neighbor_map(G, Frontier, BFS_F<W>(Parents.begin()), -1,
                         sparse_blocked | dense_parallel);

        Frontier.del();
        Frontier = output;
    }
    Frontier.del();
    std::cout << "Reachable: " << reachable << "\n";
#ifdef ACCESS_OBSERVER
    annotation << "END: BFS()";
    access_observer.write(
        annotation.str()
    );
    std::stringstream().swap(annotation);
    access_observer.save();
#endif
    return Parents;
}

} // namespace gbbs
