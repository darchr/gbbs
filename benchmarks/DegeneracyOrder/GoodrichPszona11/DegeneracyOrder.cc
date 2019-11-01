#include "DegeneracyOrder.h"

template <class Graph>
double DegeneracyOrder_runner(Graph& G, commandLine P) {
  double eps = P.getOptionDoubleValue("-e", 0.1);
  std::cout << "### Application: DegeneracyOrder" << std::endl;
  std::cout << "### Graph: " << P.getArgument(0) << std::endl;
  std::cout << "### Threads: " << num_workers() << std::endl;
  std::cout << "### n: " << G.n << std::endl;
  std::cout << "### m: " << G.m << std::endl;
  std::cout << "### Params: -epsilon = " << eps << std::endl;
  std::cout << "### ------------------------------------" << std::endl;
  assert(P.getOption("-s"));

  // runs the fetch-and-add based implementation if set.
  timer t; t.start();
  auto order = goodrichpszona_degen::DegeneracyOrder(G, eps);
  double tt = t.stop();

  std::cout << "### Running Time (1): " << tt << std::endl;
  std::cout << "### Running Time (2): " << qq << std::endl;
  return tt;
}

generate_symmetric_main(DegeneracyOrder_runner, false);
