#include "scheduling_service.h"

string print_vid(vid_t i) {
  stringstream stream;
  stream << "0x" << setfill('0') << setw(4) << hex << i;
  return stream.str();
}

void scheduler::print_vertex_id() {
  cout << "- node/ids: "<< endl;
  for (const auto &v : g.get_vertices()) {
    cout << "node: " << print_vid(v.first) << ", id: " << v.second << "\n";
    // print_vertex_id(print_vid(v.first), v.second);
  }
}

void scheduler::print_MC2() {
  cout << "- MC2: ";
  for (const auto &v : MC2) {
    cout << v << ", ";
  }
  cout << endl;
}

void scheduler::print_scheduling() {
  cout << "- Scheduling (node_id/slot): " << endl;
  for (const auto &n : g.get_vertices())
  // for (const auto& [n, s] : sched)
    cout << n.second << ": " << sched[n.second] << endl;
}

void scheduler::print_hop2n() {
  cout << "- N2 sets: " << endl;
  for (const auto &n : g.get_vertices()) {
  // for (auto const &n : hop2n) {
    cout << "node " << n.second << ": " ;
    for (auto v = hop2n[n.second].begin(); v != hop2n[n.second].end(); ++v) {
      cout << *v;
      if (v + 1 != hop2n[n.second].end())
        cout << ", ";
      else
        cout << endl;
    }
  }
}

// get N2
void scheduler::set_hop2n_neighbors() {
  hop2n.clear();
  for (const auto &n : g.get_vertices()) {
    // add the node itself
    hop2n[n.second].push_back(n.second);
    // add 1 hop neighbors
    for (const auto &n1 : g.get_neighbors(n.second)) {
      if (!is_in_vector(hop2n[n.second], n1)) {
        hop2n[n.second].push_back(n1);
      }
    }
    // add 2 hop neighbors
    for (const auto &n1 : g.get_neighbors(n.second)) {
      for (const auto &n2 : g.get_neighbors(n1)) {
        if (!is_in_vector(hop2n[n.second], n2)) {
          hop2n[n.second].push_back(n2);
        }
      }
    }
  }
  // sort
  for (auto const &n : hop2n) {
    sort(hop2n[n.first].begin(), hop2n[n.first].end());
  }
}

vid_t scheduler::argmax_not_in_M(vvid_t intersec) {
  int maxi = 0;
  vid_t maxn;
  vvid_t i;
  for (auto const &n : hop2n) {
    i.clear();
    if (!is_in_vector(MC2, n.first)) {
      set_intersection(intersec.begin(), intersec.end(), hop2n[n.first].begin(),
                       hop2n[n.first].end(), back_inserter(i));
      if (i.size() > maxi) {
        maxi = i.size();
        maxn = n.first;
      }
    }
  }
  return maxn;
}

void scheduler::estimated_maximum_c2() {
  MC2.clear();
  int maxlen = 0;
  vid_t argmax;
  for (const auto &n : g.get_vertices()) {
    if (hop2n[n.second].size() > maxlen) {
      maxlen = hop2n[n.second].size();
      argmax = n.second;
    }
  }
  vvid_t intersec(hop2n[argmax]);
  vvid_t tmp;
  MC2.push_back(argmax);
  while (intersec.size() > MC2.size()) {
    vid_t n = argmax_not_in_M(intersec);
    MC2.push_back(n);
    tmp.clear();
    set_intersection(intersec.begin(), intersec.end(), hop2n[n].begin(),
                     hop2n[n].end(), back_inserter(tmp));
    intersec = tmp;
  }
  sort(MC2.begin(), MC2.end());
}

void scheduler::assign_slot_to_node(vid_t n) {
  vector<int> tabu ;
  int sched_slot = 0;
  for (auto const &v : hop2n[n]) tabu.push_back(sched[v]);
  while(is_in_vector(tabu, sched_slot))
    sched_slot++;
  sched[n] = sched_slot;
}

void scheduler::centralized_scheduling() {
  queue<vid_t> s ;
  int sched_slot = 0;
  set_hop2n_neighbors();
  estimated_maximum_c2();
  if (MC2.empty())
    estimated_maximum_c2();
  for(auto n: g.get_vertices())
    sched[n.second] = -1 ;
  // assign c2 
  for(auto n : MC2) {
    sched[n] = sched_slot;
    sched_slot += 1;
    s.push(n);
  }
  while (is_in_map(sched, -1)) {
    vid_t neighborhood = s.front(); s.pop();
    for(auto n : g.get_neighbors(neighborhood)) {
      if(sched[n] == -1) {
        s.push(n);
        assign_slot_to_node(n);
      }
    }
  }
}

void scheduler::remove_node_and_reschedule(vid_t n) {
  g.remove_vertex(n);
  // set_hop2n_neighbors();
  // assign_slot_to_node(n);
}

void scheduler::add_node_and_reschedule(vid_t node, vvid_t neighbors) {
  vid_t n = g.add_vertex(node);
  for (const auto &v : neighbors) 
    g.add_edge(n, v, 1);
  set_hop2n_neighbors();
  assign_slot_to_node(n);
}

// 
// Usage example
// 
/*
void build_graph(graph_t &g) {
  int num_of_nodes = 14;
  vvid_t vid(num_of_nodes);
  for (int i = 0; i < num_of_nodes; ++i) {
    vid[i] = g.add_vertex(i);
    //   cout << "vid(" << i << "): " << vid[i] << endl;
  }
  for (const auto &v : vvid_t({vid[1],vid[2],vid[3],vid[4]}))
    g.add_edge(vid[0], v, 1);
  g.add_edge(vid[4], vid[5], 1);
  g.add_edge(vid[5], vid[6], 1);
  g.add_edge(vid[6], vid[13], 1);
  g.add_edge(vid[13], vid[12], 1);
  g.add_edge(vid[12], vid[11], 1);
  g.add_edge(vid[11], vid[7], 1);
  for (const auto &v : vvid_t({vid[8],vid[9],vid[10]}))
    g.add_edge(vid[7], v, 1);
}

int main() {
  scheduler sched;
  // build graph
  build_graph(sched.g);
  // compute the scheduling
  sched.centralized_scheduling();
  // print sets
  sched.print_vertex_id();
  sched.print_hop2n();
  sched.print_MC2();
  sched.print_scheduling();
  // remove node id=3
  vid_t n = 3 ;
  cout << "removing node id = " << n << endl;
  sched.remove_node_and_reschedule(n);
  // print sets
  sched.print_vertex_id();
  sched.print_hop2n();
  sched.print_scheduling();
  // add node, neighbors id (0,1,2,5,6)
  int add_n = sched.g.vertex_count()+1;
  cout << "adding node = " << add_n << endl;
  sched.add_node_and_reschedule(add_n, vvid_t({0,1,2,5,6}));
  // print sets
  sched.print_vertex_id();
  sched.print_hop2n();
  sched.print_scheduling();
}
*/

// Local Variables:
// mode: c++
// c-file-style: "Stroustrup"
// c-basic-offset: 2
// End:
