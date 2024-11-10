# Updates

## 1.0.0

### New Routing Table Protocol (BMX6)

Inspired by the following article: [BMX6](https://ieeexplore.ieee.org/document/7300903)

#### New parameters added to LoRaMesher.h file.

- LM_QUALITY_WINDOWS_SIZE: This parameter will be used to calculate the link quality. The link quality will be calculated as the average of the last x received hello packets. The default value is 100. This value can be changed in the LoRaMesher.h file.

- LM_MAX_HOPS: This parameter will be used to calculate the maximum number of hops that the packet can do. The default value is 10. This value can be changed in the LoRaMesher.h file.

- LM_REDUCED_FACTOR_HOP_COUNT: This parameter will be used to reduce the metric value with each iteration. The default value is 0.97. This value can be changed in the LoRaMesher.h file.

- LM_MAX_METRIC is the maximum metric value that the packet can have. The default value is 255. Which will be the maximum size of an uint8_t. This value CANNOT be changed since the metric is an uint8_t.

- LM_HELLO_PACKET_DEBOUNCE_TIMEOUT: Timeout duration to prevent sending repeated hello packets; resets on each function call and triggers the packet send after the timeout expires. The default value is 20 seconds. This value can be changed in the LoRaMesher.h file.

#### New types of packets

- Hello Packet: This packet will be used to transmit the link quality to the neighbors. Additionally, the hello packet will send the received link quality (ri) for the neighbors to update their transmitted link quality (ti). A parameter has been added to the hello packet which is the routing table id. This number will be incremented every time the routing table is updated. This way the nodes will know if the routing table is newer or not.

- Routing table request packet: This packet will be used to request the routing table from a specific neighbor.

#### Implementation of the new routing table protocol.

- Now the hello packets will be send periodically to the neighbors. This packet will be as a keep alive message and to transmit to their neighbors their quality link. Additionally, the hello packet will send ri (received link quality), for the neighbors to update their ti (transmit link quality). A new parameter has been added to the hello packet which is the routing table id. This number will be randomly chosen every time the routing table is updated. With this number the nodes will know if their routing table is the same or not.

- The routing table will be send only when a new node is added to the routing table. And if received a routing table from a neighbor, the routing table will be updated only if the received routing table is newer than the current routing table. A new field has been added to the routing table which is the routing table id. This number will be randomly chosen every time the routing table is updated. With this number the nodes will know if their routing table is the same or not.

- The routing table will be send to the neighbors only if the routing table has been updated.

- Received link quality (ri) will be stored in the routing table. The link quality will be calculated as the average of the last x received hello packets. This last x values will be named as the LM_QUALITY_WINDOWS_SIZE. The default value is 100. This value can be changed in the LoRaMesher.h file.
- If timeout is reached, ri will be [0, 255] multiple ri= max(0, ri-(255/LM_QUALITY_WINDOWS_SIZE)) will be done.
- If received a hello packet from a neighbor ri = min(255, ri+(255/LM_QUALITY_WINDOWS_SIZE))
- Routing table will contain a field that will be not send that will say if a packet has been received from the neighbor in the last x seconds. When the routine is executed, this field will be updated to false. If a hello packet is received from the neighbor, this field will be updated to true. If the field is false, the node ri will be updated with the ri= max(0, ri-(255/LM_QUALITY_WINDOWS_SIZE)).

- When receiving a hello packet, the snr and rssi will be stored in the routing table. ONLY INFO. This info will not be used at the moment.

- The routing table entries will be updated with a Vector Metric (VM) which will be calculated by the link quality, the number of hops and the snr and rssi values. This VM will be used to calculate the best path to the destination node.
- mn,l is the metric of the link between the node n and the node l.
- With ql = ri+ti/2

- F(Hope Count) will be calculated as follows:
    - F(HC) = LM_REDUCED_FACTOR_HOP_COUNT * Hop Count * LM_MAX_METRIC
        "doing nothing else than reducing the updated metric value by the minimal possible constant factor of 3% with each iteration, thus every additional hop."


The calculated metric will be calculated as follows:
    - M = min(F(HC), LM_MAX_METRIC / sqrt((LM_MAX_METRIC / mn,l)^2 + (LM_MAX_METRIC / ql)^2))

- A new timeout with lower value has been added to the routing nodes. 
- When a node receives a routing table message from a neighbor, it retrieves the link quality from that neighbor. For each entry in the received message, the node calculates a new metric (M). If this new metric is better than the current metric, the node updates its routing table with the new metric and the corresponding next hop. If the new metric is equal to or worse than the current metric, the node does not update the entry.

- The routing table will have the following parameters, (s) means that the parameter will be send to the neighbors, (n) means that the parameter will be not send to the neighbors.
- The routing table will have the following parameters:
    - Destination (s)
    - Metric (s)
    - Role (s) 
    - Hop Count (s)
    - Next Hop / Via (n)
    - Received Link Quality (n) [Only for the neighbors]
    - Transmitted Link Quality (n) [Only for the neighbors]
    - Timeout (n)

- Whenever a message from a node is received, the routing table timeout will be updated. If the timeout is reached, the node will be removed from the routing table.

- RoutingTableId: Only when a node update the routing table, the node will randomly chose the routing table id. If the node receive a routing table id from a neighbor that is different to my routing table id, the node will try to update the routing table, if some update is made, the routing table id will be randomly generated again. Until the node receives a routing table and no update is made, the routing table id will be the same.


3 nodes starts at the same time:
1st node send a HelloPacket to 2nd and 3rd. 2nd and 3rd receive, update and send the routing table. They have updated their routingtableid and size which are the same for both. If both routing table packets are lost, both will stay in the same "state". Only when the 1st node receive the updated routingTableId, it will ask for a routingTableUpdate. Routing table from 2nd or 3rd will be send. 1st node will receive the routing table, update their routing table size and send the routing table to the neighbors. RoutingTableOnDemand.

##### When the routing table id is updated

- When a new node is added to the routing table.
- When a node is removed from the routing table.
- When a node is updated by me. (Metric, role)

When a node is updated, immediately call to send a HelloPacket with the new routing table id and the new routing table size. This will activate a timeout of LM_HELLO_PACKET_DEBOUNCE_TIMEOUT. If the timeout is reached, send a HelloPacket to the neighbors. This will be done until the timeout of LM_HELLO_PACKET_MAX_DEBOUNCE_TIMEOUT is reached or the timeout is reached.
When received a Routing Table packet, check the routing table id. If the routing table id is different, try to update the routing table. If the routing table is updated, send a HelloPacket to the neighbors. If the routing table is not updated, update the routing table id """"" TODO->??and send a HelloPacket to the neighbors.

##### When a node is removed from the routing table (Timeout)

A timeout for each node is in the routing table. The timeout is defined by the LM_RT_TIMEOUT. When the timeout is reached, the node will be erased from the Routing table. When a node is removed from the routing table, send a HelloPacket to the neighbors with a new routing table id.
When a node receives a hello packet, it will update the timeout for the nodes that are in via of the node that sent the hello packet. 
Every time a Routing table packet is received, after updating the routing table, it checks if any of the nodes with the next hop as the node that sent the routing table packet is not inside the routing table packet. If this is the case, the node will be removed from the routing table. After this, send a HelloPacket to the neighbors with a new routing table id.

#### Further improvements

Add this when multiple routing table packets are send. Aka, the routing table is bigger than the maximum size of the packet. A limit of devices will be shown in the initialization of the protocol.

<!--

- LM_RT_SECONDARY_TIMEOUT: Timeout duration to set a node into a stale mode from the routing table. This value should be lower than the hello packet timeout, since the routing table packet could be send in more than one packet. The default value is 30 seconds. This value can be changed in the LoRaMesher.h file.

- LM_RT_STALE_TIMEOUT: Timeout duration to delete a stale node from the routing table. The default value is 3 times the hello packet. This value can be changed in the LoRaMesher.h file.

A timeout for each node is in the routing table. The timeout is defined by the LM_RT_TIMEOUT. When the timeout is reached, the node will be set as "stale" state in the routing table and added another timeout; LM_RT_STALE_TIMEOUT, after this timeout the node will be erased from the Routing table. When a node is removed from the routing table, send a HelloPacket to the neighbors.
When a node receives a hello packet, it will update the timeout for the nodes that are in via of the node that sent the hello packet. 
When a node receives a routing table packet, all the nodes that are in via of the node that sent the routing table packet will update the timeout with a lower value than the frequency of the hello packet. This value is defined by the LM_RT_SECONDARY_TIMEOUT. Since the routing table packet could be send in more than one packet, the timeout for each node should be lower as the hello packet.

When a node receives a routing table packet, all the nodes that are in via of the node that sent the routing table packet will update the timeout with a lower value than the frequency of the hello packet. This value is defined by the LM_RT_SECONDARY_TIMEOUT. Since the routing table packet could be send in more than one packet, the timeout for each node should be lower as the hello packet.

Given this information the timeout for a unreachable node will be:
- LM_RT_TIMEOUT + LM_RT_SECONDARY_TIMEOUT + LM_RT_STALE_TIMEOUT -->

