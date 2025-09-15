/**
 * @file routing_table_factory.cpp
 * @brief Factory implementation for creating routing tables
 */

#include "distance_vector_routing_table.hpp"
#include "protocols/lora_mesh/interfaces/i_routing_table.hpp"

namespace loramesher {
namespace protocols {
namespace lora_mesh {

std::unique_ptr<IRoutingTable> CreateDistanceVectorRoutingTable(
    AddressType node_address, size_t max_nodes) {
    return std::make_unique<DistanceVectorRoutingTable>(node_address,
                                                        max_nodes);
}

}  // namespace lora_mesh
}  // namespace protocols
}  // namespace loramesher