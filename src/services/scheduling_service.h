// (c) Llorenç Cerdà Alabern, octubre 2025
// Broadcast colission free scheduling algorithm as in the paper July 30, 2025
// Graaf Library: https://github.com/bobluppes/graaf

#include "graaflib/types.h"
#include "graaflib/graph.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

using namespace std;
typedef uint16_t vertex_t;
typedef graaf::vertex_id_t vid_t; // graaf::vertex_id_t is size_t
typedef size_t edge_t;
typedef graaf::undirected_graph<const vertex_t, edge_t> graph_t;
typedef vector<vid_t> vvid_t;

class scheduler {
public:
  scheduler() { };
  graph_t g;
  map<vid_t, vvid_t> hop2n;
  map<vid_t, int> sched;
  void set_hop2n_neighbors();
  void estimated_maximum_c2();
  void print_hop2n();
  void centralized_scheduling();
  void print_vertex_id();
  void print_MC2();
  void print_scheduling();
  void remove_node_and_reschedule(vid_t);
  void add_node_and_reschedule(vid_t node, vvid_t neighbors);
  
private:
  vid_t argmax_not_in_M(vvid_t);
  void assign_slot_to_node(vid_t);
  vvid_t MC2;
};

template<typename V>
bool is_in_vector(vector<V> v, V n) {
  return find(v.begin(), v.end(), n) != v.end();
}

template<typename K, typename V>
bool is_in_map(map<K,V> m, V v) {
  for (auto it = m.begin(); it != m.end(); ++it)
    if (it->second == v)
      return true;
  return false;
}

