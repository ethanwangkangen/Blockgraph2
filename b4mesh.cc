#include "b4mesh.h"
#include <iomanip> 

NS_LOG_COMPONENT_DEFINE("B4Mesh");
NS_OBJECT_ENSURE_REGISTERED(B4Mesh);

TypeId B4Mesh::GetTypeId(){
  static TypeId tid = TypeId("B4Mesh")
    .SetParent<Application>()
    .SetGroupName("Application")
    .AddConstructor<B4Mesh>()
    ;
  return tid;
}

// Constructor - Global variables initialization
B4Mesh::B4Mesh(){
  running = false;
  recv_sock = 0;
  node = Ptr<Node>();
  peers = vector<Ipv4Address>();

  /* Group Management System Variables Initialization */
  groupId = string(32, 0);
  group = vector<pair<int, Ipv4Address> > ();
  previous_group = vector<pair<int, Ipv4Address> > ();

  /* Blockgraph Protocol Variables Initializatino */
  groupId_register = vector<string> ();
  missing_block_list = multimap<string, Ipv4Address> ();
  missing_childless_hashes = vector<string> ();
  block_waiting_list = map<string, Block>();
  txn_mempool = map<string, Transaction>();
  recover_branch = multimap<string, int> ();
  mergeBlock = false;
  createBlock = true;
  startMerge = false;
  lastBlock_creation_time = 0;
  change = 0;   
  lastLeader = -1;

  /* Blockgraph parameters */
  timeBetweenTxn = -1;

  /* Variables for the blockchain performances */
  numTxsG = 0; 
  numRTxsG = 0;
  lostTrans = 0;
  lostPacket = 0;
  numDumpingBlock = 0;
  numDumpingTxs = 0;

  sentPacketSizeTotal = 0;
  sentTxnPacketSize = 0;

  missing_list_time = map<string, double> ();
  count_missingBlock = 0;
  total_missingBlockTime = 0;
  waiting_list_time = map<string, double> ();
  count_waitingBlock = 0;
  total_waitingBlockTime = 0;
  pending_transactions_time = map<string, double> ();
  count_pendingTx = 0;
  total_pendingTxTime = 0;
  p_b_t_t = 0.0; 
  p_b_c_t = 0.0; 
  p_t_t_t = 0.0; 
  p_t_c_t = 0.0;
  blockgraph_file = vector  <pair<int, pair <int, int>>> ();
  startmergetime = 0;
  endmergetime = 0;
  mempool_info = vector<pair<pair<double, int>, pair<int, float>>> ();
  txs_perf = vector<pair<pair<double, float>, pair<int, int>>> ();
  time_SendRcv_block = map<int,pair<double,double>> ();
  time_SendRcv_txs = map<int,pair<double,double>> ();
  b4mesh_throughput = map<double,int> ();
  TxsLatency = unordered_multimap <string, pair<string, double>> ();

}

B4Mesh::~B4Mesh(){
}

// Pointer to the oracle app of node i
Ptr<B4MeshOracle> B4Mesh::GetB4MeshOracle(int nodeId){
  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(0);
  Ptr<B4MeshOracle> oracle = app->GetObject<B4MeshOracle>();
  return oracle;
}

// Pointer of the b4mesh app of node i
Ptr<B4Mesh> B4Mesh::GetB4MeshOf(int nodeId){
  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(1);
  Ptr<B4Mesh> b4mesh = app->GetObject<B4Mesh>();
  return b4mesh;
}

void B4Mesh::SetUp(Ptr<Node> node, vector<Ipv4Address> peers, float timeBetweenTxn){
  this->peers = peers;
  this->node = node;
  this->timeBetweenTxn = timeBetweenTxn;

  if (!recv_sock){
    // Open the receiving socket
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    recv_sock = Socket::CreateSocket(node, tid);
  }

  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), 80);
  // Important to use a different port from the oracle module!

  recv_sock->Bind(local);
  recv_sock->SetRecvCallback (MakeCallback (&B4Mesh::ReceivePacket, this));

  GetB4MeshOracle(node->GetId())->SetSendBlock(RecvBlock);               // interface for consenus
  GetB4MeshOracle(node->GetId())->SetIndicateNewLeader(HaveNewLeader);  // interface for consenus
  GetB4MeshOracle(node->GetId())->SetMyB4Mesh(this);

   Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange",
                    MakeCallback (&B4Mesh::CourseChange, this));
}

void B4Mesh::StartApplication(){
  running = true;
  debug_suffix.str("");
  debug_suffix << "Start B4Mesh on node : " << node->GetId() << endl;
  debug(debug_suffix.str());

  Simulator::ScheduleNow(&B4Mesh::GenerateTransactions, this);
  Simulator::Schedule(Seconds(SEC_10_TIMER), &B4Mesh::RecurrentSampling, this);
  Simulator::Schedule(Seconds(SEC_60_TIMER), &B4Mesh::RecurrentTasks, this);
  Simulator::Schedule(Seconds(SEC_10_TIMER), &B4Mesh::TestBlockCreation, this);
}

/**
 * Every 10 seconds, call Ask4MissingBlocks() and ResubmitTransactions().
 * If there are missing blocks, repeat every 5 seconds instead.
 */
void B4Mesh::RecurrentTasks(){
  if (running == false){
    return;
  }

  if (missing_block_list.size() > 0){
    Simulator::Schedule(Seconds(SEC_5_TIMER), 
      &B4Mesh::RecurrentTasks, this);
  } else {
    Simulator::Schedule(Seconds(SEC_10_TIMER), 
        &B4Mesh::RecurrentTasks, this);
  }

  Ask4MissingBlocks();
  RetransmitTransactions();
}

/**
 * Every 5 seconds, call MempoolSampling() and TxsPerformances()
 */
void B4Mesh::RecurrentSampling(){
  if (running == false){
    return;
  }

  MempoolSampling();
  TxsPerformances();

  Simulator::Schedule(Seconds(SEC_5_TIMER), 
        &B4Mesh::RecurrentSampling, this);
}

/**
 * Every <TESTMEMPOOL_TIMER>, if node is leader, check if TestPendingTxs() and 
 * createBlock == True. If so, call GenerateBlocks().
 */
void B4Mesh::TestBlockCreation(){
  if (running == false){
    return;
  }

  Simulator::Schedule(Seconds(TESTMEMPOOL_TIMER), 
        &B4Mesh::TestBlockCreation, this);

  if (GetB4MeshOracle(node->GetId())->IsLeader() == false){
    return;
  }

  if (TestPendingTxs() == true && createBlock == true){
    GenerateBlocks();
  }
}

/**
 * Return IP address of this node
 */
Ipv4Address B4Mesh::GetIpAddress(){
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}


/**
 * Get node's id from the given ip address. Node id is simply its index in 
 * the peers vector, so just return the index. If not found, return -1.
 */
int B4Mesh::GetIdFromIp(Ipv4Address ip){
  for(unsigned int i=0; i<=peers.size(); i++){
    if(peers[i] ==  ip){
      return i;
    }
  }
  return -1;
}

/**
 * Get node's ip address from the given id. Retrieve ip at index id.
 */
Ipv4Address B4Mesh::GetIpAddressFromId(int id){
  Ipv4Address ipv4 = peers[id];
  return ipv4;
}

//  ************* PACKET RELATED METHODS ********************************

/**
 * Extracts the message type from the header.
 *
 * Possible message types:
 * - CHILDLESSBLOCK_REQ
 * - CHILDLESSBLOCK_REP
 * - GROUPBRANCH_REQ
 * - GROUPCHANGE_REQ
 */
int B4Mesh::ExtractMessageType(const string& msg_payload){
  int ret = *((int*)msg_payload.data());
  return ret;
}

/**
 * Callback function that is called when the socket receives a packet.
 *
 * Supported ApplicationPacket types:
 *
 * - TRANSACTION:
 *     Schedule a call to TransactionsTreatment() after a processing delay.
 *
 * - BLOCK:
 *     Schedule a call to BlockTreatment() after a processing delay.
 *
 * - REQUEST_BLOCK:
 *     Packet contains the hash of the requested block and the ID of the requesting node.
 *     Call SendBlockTo() to respond with the requested block.
 *
 * - CHANGE_TOPO:
 *     Can contain the following sub-message types:
 *
 *     - CHILDLESSBLOCK_REQ:
 *         Call SendChildlessHashes() to send a CHILDLESSBLOCK_REP to the requester.
 *
 *     - CHILDLESSBLOCK_REP:
 *         Call ProcessChildlessResponse() to handle the received hashes.
 *
 *     - GROUPBRANCH_REQ:
 *         Call SendBranch4Sync() to send the requested branch to the requester.
 */
void B4Mesh::ReceivePacket(Ptr<Socket> socket){
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from))){
    if (InetSocketAddress::IsMatchingType(from)){
      InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
      Ipv4Address ip = iaddr.GetIpv4();

      // debug_suffix.str("");
      // debug_suffix << " Received Packet : New packet of " << packet->GetSize() << "B from Node " << ip;
      // debug(debug_suffix.str());

      string parsedPacket;
      char* packetInfo = new char[packet->GetSize()];
      packet->CopyData(reinterpret_cast<uint8_t*>(packetInfo),
          packet->GetSize());
      string parsed_packet = string(packetInfo, packet->GetSize());
      delete[] packetInfo;

      try{
        ApplicationPacket p(parsed_packet);
        // For traces purposes
        b4mesh_throughput[Simulator::Now().GetSeconds()] = p.CalculateSize();

        /* ------------ TRANSACTION TREATMENT ------------------ */
        if (p.GetService() == ApplicationPacket::TRANSACTION){
          // debug_suffix.str("");
          // debug_suffix << "Received Packet : TRANSACTION from: " <<  GetIdFromIp(ip) << endl;
          // debug(debug_suffix.str());

          Transaction t(p.GetPayload());
          // For traces purposes
          TraceTxsRcv(stoi(t.GetHash()),Simulator::Now().GetSeconds());
          // Transaction Processing Delay
          float process_time =GetExecTimeTXtreatment(blockgraph.GetBlocksCount());
          // For traces purposes
          p_t_t_t += process_time;

          Simulator::Schedule(MilliSeconds(process_time),
                &B4Mesh::TransactionsTreatment, this, t);
        }

        // ------------ BLOCK TREATMENT ------------------
        else if (p.GetService() == ApplicationPacket::BLOCK){
          debug_suffix.str("");
          debug_suffix << "Received Packet : BLOCK from: " <<  GetIdFromIp(ip) << endl;
          debug(debug_suffix.str());

          Block b(p.GetPayload());

          // For traces purposes
          TraceBlockRcv(Simulator::Now().GetSeconds(), stoi(b.GetHash()));
          // Block Processing delay
          float process_time = GetExecTimeBKtreatment(blockgraph.GetBlocksCount()) ;
          p_b_t_t += process_time;
          Simulator::Schedule(MilliSeconds(process_time),
                &B4Mesh::BlockTreatment, this, b);
        }
        /* ------------ REQUEST_BLOCK TREATMENT ------------------ */
        else if (p.GetService() == ApplicationPacket::REQUEST_BLOCK){
          debug_suffix.str("");
          debug_suffix << "Received Packet : REQUEST_BLOCK from: " <<  GetIdFromIp(ip) << endl;
          debug(debug_suffix.str());

          string req_block_hash = p.GetPayload();
          SendBlockTo(req_block_hash, ip);
        }
        /* ------------ CHANGE_TOPO TREATMENT ------------------ */
        else if (p.GetService() == ApplicationPacket::CHANGE_TOPO){
          int message_type = ExtractMessageType(p.GetPayload());

          /* ----------- CHILDLESSBLOCK_REQ TREATMENT ------------*/
          if (message_type == CHILDLESSBLOCK_REQ){
            debug_suffix.str("");
            debug_suffix << "Received Packet : CHILDLESSBLOCK_REQ from: " <<  GetIdFromIp(ip) << endl;
            debug(debug_suffix.str());

            SendChildlessHashes(ip);
          }
          /* ----------- CHILDLESSBLOCK_REP TREATMENT ------------*/
          else if (message_type == CHILDLESSBLOCK_REP){
            debug_suffix.str("");
            debug_suffix << "Received Packet : CHILDLESSBLOCK_REP from: " <<  GetIdFromIp(ip) << endl;
            debug(debug_suffix.str());

            ProcessChildlessResponse(p.GetPayload(), ip);
          }
          /* ----------- GROUPBRANCH_REQ TREATMENT ------------*/
          else if (message_type == GROUPBRANCH_REQ){
            debug_suffix.str("");
            debug_suffix << "Received Packet : GROUPBRANCH_REQ from: " <<  GetIdFromIp(ip) << endl;
            debug(debug_suffix.str());

            SendBranch4Sync(p.GetPayload(), ip);
          }
          else {
            debug(" Packet CHANGE_TOPO type unsupported");
          }
        }
        else{
          debug(" Packet type unsupported");
          lostPacket++;
        }
      }
      catch(const exception& e){
        cerr << e.what() << '\n';
        debug(" Packet lost !!! ");
        lostPacket++;
        return;
      }
    }
  }
}

/**
 * Sends packet to node with specified ip address. scheduled=false by default
 */
void B4Mesh::SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool scheduled){
  if (running == false){
    return;
  }

  // if (!scheduled){
  //   float desync = (rand() % 100) / 1000.0;
  //   Simulator::Schedule(Seconds(desync),
  //       &B4Mesh::SendPacket, this, packet, ip, true);
  //   return;
  // }

  Ptr<Packet> pkt = Create<Packet>((const uint8_t*)(packet.Serialize().data()), packet.GetSize());

  // For tracing purposes: Ethan added
  sentPacketSizeTotal += packet.GetSize(); // In B

  if (packet.GetService() == ApplicationPacket::TRANSACTION) {
    sentTxnPacketSize += packet.GetSize(); // In B
  }

  // Open the sending socket
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> source = Socket::CreateSocket (node, tid);
  InetSocketAddress remote = InetSocketAddress (ip, 80);
  source->Connect(remote);

  int res = source->Send(pkt);
  source->Close();

  if (res > 0){
    // debug_suffix.str("");
    // debug_suffix << GetIpAddress() << " sends a packet of size " << pkt->GetSize() << " to " << ip << endl;
    // debug(debug_suffix.str());
  } else{
    // debug_suffix.str("");
    // debug_suffix << GetIpAddress() << " failed to send a packet of size " << pkt->GetSize() << " to " << ip << endl;
    // debug(debug_suffix.str());
  }
}

/**
 * Broadcasts packet to all nodes in the given vector.
 * Currently, this is done by individuals calls to SendPacket().
 * Should try to further optimise this.
 */
void B4Mesh::BroadcastPacket(ApplicationPacket& packet, vector<Ipv4Address> v){
  for (auto& ip : v){
    if(GetIpAddress() != ip)
    SendPacket(packet, ip, false);
  }
}

//  ******* TRANSACTION RELATED METHODS **************** 

/**
 * Function calls itself at regular intervals to continually generate transactions.
 * Interval determined by timeBetweenTxn.
 * Creates a payload of variable size based on TX_PAYLOAD_MIN and TX_PAYLOAD)MAX.
 * Sends payload to RegisterTransaction() which actually creates the transactions,
 * and treats it locally, and sends it to other nodes in group after delay.
 */
void B4Mesh::GenerateTransactions(){
  if (running == false){
    return;
  }

  numTxsG += 1;

  /* Generation of a random variable to define the size of the transaction */
  Ptr<UniformRandomVariable> size_payload = CreateObject<UniformRandomVariable> ();
  size_payload->SetAttribute("Min", DoubleValue(TX_PAYLOAD_MIN));
  size_payload->SetAttribute("Max", DoubleValue(TX_PAYLOAD_MAX));

  RegisterTransaction(string(size_payload->GetValue(), 'a'+node->GetId()));

  double mean = timeBetweenTxn;
  double bound = 0;
  Ptr<ExponentialRandomVariable> x = CreateObject<ExponentialRandomVariable> ();
  x->SetAttribute("Mean", DoubleValue(mean));
  x->SetAttribute("Bound", DoubleValue(bound));
  double interval = x->GetValue();
  
  Simulator::Schedule(Seconds(interval), &B4Mesh::GenerateTransactions, this);
}

/**
 * Takes payload from GenerateTransactions(),
 * Create the transaction, treat it with TransactionsTreatment(), ie. add to local mempool.
 * Then schedule call to BroadcastTransaction(), after processing delay based on size of transaction.
 */
void B4Mesh::RegisterTransaction(string payload){
  Transaction t;
  t.SetPayload(payload);
  t.SetTimestamp(Simulator::Now().GetSeconds());

  // debug_suffix.str("");
  // debug_suffix << "Creating Transaction : " << t.GetHash().data() << endl;
  // debug(debug_suffix.str());

  TransactionsTreatment(t); // Essentially, adding to local mempool

  // Transaction Processing delay
  float process_time = (t.GetSize() / pow(10,3)); // 1 kbytes/s
  p_t_c_t += process_time;

  Simulator::Schedule(MilliSeconds(process_time),
        &B4Mesh::BroadcastTransaction, this, t);
}

/**
 * Takes a transaction, wraps it in ApplicationPacket, sends to all nodes in current group with
 * call to BroadcastPacket().
 */
void B4Mesh::BroadcastTransaction(Transaction t){
  // debug_suffix.str("");
  // debug_suffix << "Sending Transaction : " << t.GetHash().data() << endl;
  // debug(debug_suffix.str());

  string serie = t.Serialize();
  ApplicationPacket packet(ApplicationPacket::TRANSACTION, serie);
  
  bool trace = false;
  if (stoi(t.GetHash().data()) % 5 == 0 ){
    trace = true;
  }
  
  vector<Ipv4Address> groupDestination = vector<Ipv4Address>();
  for (auto& dest : group){
    groupDestination.push_back(dest.second);
    // For traces purposes
    if (trace){
      TraceTxsSend(dest.first, stoi(t.GetHash()), Simulator::Now().GetSeconds());
    }
  }
  BroadcastPacket(packet, groupDestination);
}

/**
 * Called by RegisterTransaction() after generating a transaction with GenerateTransaction,
 * as well as by ReceivePacket() after receiving a Transaction.
 * Checks if transaction is already in mempool, or blockgraph, and if there is space in mempool.
 * If all okay, add it to mempool, ie. map the transaction hash to the transaction.
 */
void B4Mesh::TransactionsTreatment(Transaction t){
  if (running == false){
    return;
  }

  if (!IsTxInMempool(t)){
    // debug("Transaction not in mempool. ");
    if (!blockgraph.IsTxInBG(t)){
      // debug("Transaction not in Blockgraph. ");
      if (IsSpaceInMempool()){
        // debug("Adding transaction in mempool... ");
        txn_mempool[t.GetHash()] = t;
        pending_transactions_time[t.GetHash()] = Simulator::Now().GetSeconds(); // traces purpose
      } else { // No space in mempool.
        // debug("Transaction's Mempool is full\n Dumping transaction...");
        lostTrans++; // For traces purposes
        // TRACE << "MEMPOOL_FULL" << " " << "TXS_LOST" << " " << lostTrans << endl;
      }
    } else { // Transaction already in blockgraph
        // debug("Transaction already present in Blockgraph\n Dumping transaction ...  ");
        numDumpingTxs++;
      //  TRACE << "DUMP_TX" << " " << "TX_IN_BG" << " " << numDumpingTxs << endl;
    }
  } else { // Transaction already in mempool
      // debug("Transaction already present in Memepool\n Dumping transaction ... ");
      numDumpingTxs++;
      // TRACE << "DUMP_TX" << " " << "TX_IN_MEMPOOL" << " " << numDumpingTxs << endl;
  }
}

/**
 * Regular call to this by RecurrentTasks().
 * Rebroadcast all the transactions in the mempool to check that other nodes receive it.
 */
void B4Mesh::RetransmitTransactions(){
  // Retransmission of transactions to be sure that old transactions are registered
  int i = 0;
  if (txn_mempool.size() > 0){
    for (auto mem_i : txn_mempool){
      if (mem_i.second.GetTimestamp() + RETRANSMISSION_TIMER < Simulator::Now().GetSeconds() && i <= 10 ){

        // debug_suffix.str("");
        // debug_suffix << "Retransmission : ----> Transaction: " << mem_i.second.GetHash().data() << " with Timestamp of: " <<  mem_i.second.GetTimestamp() << endl;
        // debug(debug_suffix.str());

        numRTxsG ++;
        BroadcastTransaction(mem_i.second);
      } else {
        break;
      }
    }
  }
}

/**
 * Checks if given transaction is in local mempool.
 */
bool B4Mesh::IsTxInMempool (Transaction t){
  if(txn_mempool.count(t.GetHash()) > 0){
    return true;
  } else {
    return false;
  }
}

/**
 * Checks if gthere is space left in local mempool.
 * Space in mempool defined by SIZE_MEMPOOL.
 */
bool B4Mesh::IsSpaceInMempool (){

  if (SizeMempoolBytes()/1000 < SIZE_MEMPOOL){
    return true;
  }
  else {
    return false;
  }
}

// ****** BLOCK RELATED METHODS *********************


/**
 * Treatment of a received block, either merge or regular.
 * Updates lastLeader to leader of this block.
 * If applicable: erases received block from missing list, adds it to waiting list if parents not in BG.
 * If received a merge block, AddBlockToBlockgraph() and StartSyncProcedure().
 * If received a regular block, check if parents are in Blockgraph.
 * If they aren't, check if they are in the missing list.
 * If aren't as well, then add them to the missing list.
 * If parents weren't in Blockgraph, can't add regular block to BG yet, so
 * just add it to the waiting list.
 * If parents are present, then AddBlockToBlockgraph() and UpdateWaitingList().
 * Updating of childless_hashes and recover_branch handled by AddBlockToBlockgraph().
 */
void B4Mesh::BlockTreatment(Block b){
  if (running == false){
    return;
  }

  debug_suffix.str("");
  debug_suffix << " BlockTreatment: Block made by leader: " << b.GetLeader() << " with hash " << b.GetHash().data() << endl;
  debug(debug_suffix.str());

  lastLeader = b.GetLeader(); // Updating the leader node value 

  if (!blockgraph.HasBlock(b.GetHash())){ // If block not in blockgraph
    debug_suffix.str("");
    debug_suffix << " BlockTreatment: This block "<< b.GetHash().data() << " is not in the blockgraph " << endl;
    debug(debug_suffix.str());

    if (IsBlockInMissingList(b.GetHash())){  // Check if the block is a missing block, if so erase from missing list
      debug(" BlockTreatment: This block is a missing block");

      EraseMissingBlock(b.GetHash());
    }
    
    if (!IsBlockInWaitingList(b.GetHash())){ // If block is not in waiting list
      if (b.IsMergeBlock()) { // Block is a merge block, start sync procedure and add it to blockgraph
        debug_suffix.str("");
        debug_suffix << " BlockTreatment: Block "<< b.GetHash().data() << " is a merge block" << endl;
        debug(debug_suffix.str());

        StartSyncProcedure(b.GetTransactions());
        AddBlockToBlockgraph(b);
      } else { // The block is a regular block, check if can add it to blockgraph
        
        // Checking that the parents of this block are in the BG:
        Ipv4Address ip = GetIpAddressFromId(b.GetLeader());
        vector<string> unknown_parents = FilterNodesNotInBG(b.GetParents()); // For block's parents, filter OUT those already in BG.
        if (unknown_parents.size() > 0){ // One or more parents of this block are not in the BG
          debug(" BlockTreatment: Some of the parents of this block are not in the local BG");
          
          // May 6: Making some logic changes to the below function.
          // I feel that it should be checking if parents are in missing list, not waiting list.
          // Originally, function was GetParentsNotInWl. Changed to FilterNodesNotInML
          unknown_parents = FilterNodesNotInML(unknown_parents); // For the block's parents, further filter OUT those already in missing list
          if (unknown_parents.size() > 0){
            debug(" BlockTreatment: Some of the parents of this block are not in the Missing List");
            
            UpdateMissingList(unknown_parents, ip); // Add unknown parents to the missing list. ip is block's leader
          }

          debug_suffix.str("");
          debug_suffix << " BlockTreatment: Adding new block: " << b.GetHash().data() << " to block_waiting_list: " << endl;
          debug(debug_suffix.str());

          block_waiting_list[b.GetHash()] = b; // Adding the block to the waiting list since parents aren't in BG yet

          //Trace purpose
          waiting_list_time[b.GetHash()] = Simulator::Now().GetSeconds();
          // TRACE << "WAITING_LIST" << " " << "INSERT" << " " << b.GetHash().data() << endl;
        } else { // Block's parents are in BG, so can add Block to BG.
          AddBlockToBlockgraph(b);
          if (block_waiting_list.size() > 0){
            UpdateWaitingList();
          }
        }
      }
    } else { // The block is already present in waiting list
      debug_suffix.str("");
      debug_suffix << " BlockTreatment: Block " << b.GetHash() << " already present in waiting list\n  Dumping block..." << endl;
      debug(debug_suffix.str());

      numDumpingBlock++;
     // TRACE << "DUMP_BLOCK" << " " << "BLOCK_IN_WAITING_LIST" << " " << numDumpingBlock << endl;
    }
  } else { // The block is already present in the local BLOCKGRAPH
    debug_suffix.str("");
    debug_suffix << " BlockTreatment: Block " << b.GetHash().data() << " already present in blockgraph\n  Dumping block..." << endl;
    debug(debug_suffix.str());

    numDumpingBlock++;
    // TRACE << "DUMP_BLOCK" << " " << "BLOCK_IN_BG" << " " << numDumpingBlock << endl;
  }
}

/**
 * Called by ALL nodes (NOT JUST LEADER) after receiving and treating a merge block.
 * The "transactions" here are the "transactions" of a merge block, therefore they are 
 * actually the childless hashes. 
 * For each of the childless hashes, make a call to ChildlessHashTreatment(). TODO: Complete this
 */
void B4Mesh::StartSyncProcedure(vector<Transaction> transactions){
  string::size_type n;
  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  vector<string>::iterator it;

  debug("Starting synchronization process...");
  for (auto &txn : transactions){
    string tmp = txn.GetPayload();
    n = tmp.find(" ");
    int idnode = stoi(tmp.substr(0, n));
    string childless_hash = tmp.substr(n+1);

    ChildlessHashTreatment(childless_hash, GetIpAddressFromId(idnode));
  }

  if (GetB4MeshOracle(node->GetId())->IsLeader() == true){
    Simulator::ScheduleNow(&B4Mesh::SendBranchRequest, this);
  } else {
    Ptr<UniformRandomVariable> randNum = CreateObject<UniformRandomVariable> ();
    randNum->SetAttribute("Min", DoubleValue(0));
    randNum->SetAttribute("Max", DoubleValue(1));
    Simulator::Schedule(Seconds(randNum->GetValue()), &B4Mesh::SendBranchRequest, this);
  }
  //Testing this for edge case
  //if (!createBlock){
  //  createBlock=true;
  //}
}

/**
 * To complete the sync process, send a CHANGE_TOPO Application packet with
 * message type GROUPBRANCH_REQ.
 * For EACH of the missing childless hashes, send this request to all the nodes that have it (recover_branch),
 * but limited to a certain number based on the total size of the network. 
 */
void B4Mesh::SendBranchRequest(){
  group_branch_hdr_t branch_req;
  branch_req.msg_type = GROUPBRANCH_REQ;
  string serie_branch_b_h = "";

  int request_count = 0;
  for (auto &cb : missing_childless_hashes){ // For each of the childless hash,
    // int request_count = 0; // Counter for number of requests sent
    // This was originally placed outside the loop, but I feel that logically the limit should be set for each 
    // childless hash (ie each branch), and not all. If not what if one branch uses up the entire limit?

    if (recover_branch.count(cb) > 0){ 
      auto range = recover_branch.equal_range(cb); // Range of nodes that have this missing childless block
      // eg. range.first = {hash1, nodeA}, range.second = {hash1, nodeB} etc.

      for (auto j = range.first; j != range.second; ++j){ // For ALL of these nodes, request for the entire chain.
        // j = {hash1, nodeA}, etc.

        serie_branch_b_h = j->first; 
        string serie((char*)&branch_req, sizeof(group_branch_hdr_t));
        serie += serie_branch_b_h; // Serialise the childless hash

        debug_suffix.str("");
        debug_suffix << " SendBranchRequest : Asking for this block chain: " << serie_branch_b_h  << " to node: " << j->second << endl;
        debug(debug_suffix.str());

        ApplicationPacket pkt(ApplicationPacket::CHANGE_TOPO, serie);
        SendPacket(pkt, GetIpAddressFromId(j->second), false);

        request_count++;
        
        // Limit the number of requests that can be sent.
        if (GetB4MeshOracle(node->GetId())->IsLeader() == true){
          if (request_count == ceil(double(peers.size()))){  // number of branch request to different nodes. originally /double(4)
            goto cntt;
          }
        }
        else {
          if (request_count == ceil(double(peers.size()))){  // number of branch request to different nodes. originally /double(8)
            goto cntt;
          }
        }
      }
      cntt:;
    } else {
      // Error no entries for this childless block
    }
  } 
}

/**
 * Takes a list of node hashes and returns those that are not in the blockgraph.
 */
vector<string> B4Mesh::FilterNodesNotInBG(vector<string> nodes){

  vector<string> ret = vector<string>();
  map<string, Block> bg = blockgraph.GetBlocks();

  for (auto &p : nodes){
    if(bg.count(p) > 0){
      debug_suffix.str("");
      debug_suffix << "FilterNodesNotInBG: Parent: " << p << " already in BG " << endl;
      debug(debug_suffix.str());
    }
    else {
      debug_suffix.str("");
      debug_suffix << "FilterNodesNotInBG Parent: " << p << " is NOT in Blockgraph " << endl;
      debug(debug_suffix.str());
      
      ret.push_back(p);
    }
  }
  return ret;
}

/**
 * May 6: Made a change to this.
 * Originally, this was GetParentsNotInWl.
 * But I think logically, it should filter out those in missing list instead, not waiting list.
 * 
 * Takes a list of node hashes and return those that are not in the missing list.
 */
vector<string> B4Mesh::FilterNodesNotInML(vector<string> nodes){

  // vector<string> ret = vector<string>();

  // for (auto &p : parents){
  //   if(block_waiting_list.count(p) > 0){
  //     debug_suffix.str("");
  //     debug_suffix << "FilterNodesNotInML: Parent : " << p << " is already in block_waiting_list " << endl;
  //     debug(debug_suffix.str());
  //   }
  //   else {
  //     debug_suffix.str("");
  //     debug_suffix << "FilterNodesNotInML: Parent : " << p << " is not in Waiting List " << endl;
  //     debug(debug_suffix.str());  
  //     ret.push_back(p);
  //   }
  // }
  // return ret;

  vector<string> ret = vector<string>();

  for (auto &p : nodes){
    if(missing_block_list.count(p) > 0){
      debug_suffix.str("");
      debug_suffix << "FilterNodesNotInML: Parent : " << p << " is already in block_waiting_list " << endl;
      debug(debug_suffix.str());
    }
    else {
      debug_suffix.str("");
      debug_suffix << "FilterNodesNotInML: Parent : " << p << " is not in Waiting List " << endl;
      debug(debug_suffix.str());  

      ret.push_back(p); // Only include nodes that are NOT in missing list
    }
  }
  return ret;
}

/**
 * Returns true if block associated with this hash is in waiting list.
 */
bool B4Mesh::IsBlockInWaitingList(string b_hash){     
  if (block_waiting_list.count(b_hash) > 0){
    return true;
  } else {
    return false;
  }
}

/**
 * Returns true if block associated with this hash is in missing list.
 */
bool B4Mesh::IsBlockInMissingList(string b_hash){
  if (missing_block_list.count(b_hash) > 0){
    return true;
  }
  else {
    return false;
  }
}

/**
 * Removes the block associated with this hash from the missing list.
 * Called by BlockTreatment() after receiving a block that is in missing list.
 */
void B4Mesh::EraseMissingBlock(string b_hash){
  if (missing_block_list.count(b_hash) > 0 ){ // The hash of the missing block exists
    debug("Before erasing: \n");

    for (auto itr = missing_block_list.crbegin(); itr != missing_block_list.crend(); ++itr) {
        cout << itr->first.data()
             << ' ' << itr->second << '\n';
    }

    // Trace purpose
    total_missingBlockTime += Simulator::Now().GetSeconds() - missing_list_time[b_hash];
    count_missingBlock++;
    missing_list_time.erase(b_hash);
    // TRACE << "MISSING_LIST" << " " << "DELETE" << " " << pair_m_b->first.data() << endl;
    debug_suffix.str("");
    debug_suffix << "EraseMissingBlock: Erasing reference block " << b_hash.data() << " from missing_block_list" << endl;
    debug(debug_suffix.str());
      
    missing_block_list.erase(b_hash);    
  } else {
    debug("EraseMissingBlock: Not a missing block");
  }
}

/**
 * Takes in a list of hashes to be put as missing blocks, and ip of sender of these hashes.
 * Updates missing_block_list. Note that missing_block_list is a multimap, so can have the
 * same missing block hash mapped to different senders.
 * 
 * Called by BlockTreatment() to add a received Block's unknown parents to the missing_block_list.
 * Remember that missing_block_list includes childless hashes as well as unknown parent hashes. 
 * The ip that is passed here from BlockTreatment() is actually the ip of that Block's leader. (WHY?)
 * TODO: Continue from here.
 */
void B4Mesh::UpdateMissingList(vector<string> unknown_p, Ipv4Address ip){
  bool flag = false;

  for (auto &missing_p : unknown_p ){
    flag = false;
    if (missing_block_list.count(missing_p) >  0){
      // References for this block already exist -> Check that IP addresses are different
      auto range = missing_block_list.equal_range(missing_p);

      for (auto i = range.first; i != range.second; ++i){
        if (i->second == ip){
          flag = true;
        }
      }

      if (flag){
        // The block reference and the ip address already exists 
        debug_suffix.str("");
        debug_suffix << "updateMissingList: Parent " << missing_p.data() << " already in list" << endl;
        debug(debug_suffix.str());

        continue;
      } else {
        // The ip address does not exits -> Addding it
        debug_suffix.str("");
        debug_suffix << "updateMissingList: Adding new parent reference " << missing_p.data() << " to the list of missing blocks with IP "
                     << ip << endl;
        debug(debug_suffix.str());

        missing_block_list.insert({missing_p, ip});
      }
    } else {
      // No entry for this block reference -> Adding it
      debug_suffix.str("");
      debug_suffix << "updateMissingList: Adding Parent " << missing_p.data() << " to the list of missing blocks with IP"
                   << ip << endl;
      debug(debug_suffix.str());

      missing_block_list.insert({missing_p, ip});
      
      //Trace purpose
      missing_list_time[missing_p] = Simulator::Now().GetSeconds();
      // TRACE << "MISSING_LIST" << " " << "INSERT" << " " << missing_p.data() << endl;
    }
  }
}

/**
 * Called by BlockTreatment() when a block is added to blockgraph, in case this block is a parent
 * that was previously missing, and now allows a new block to be added in subsequently.
 * For each of the blocks in the waiting list, check if its parents are all in blockgraph.
 * If so, add it to the Blockgraph as well.
 */
void B4Mesh::UpdateWaitingList(){
  bool addblock = true;
  while (addblock) {
    addblock = false;
    for (auto it = block_waiting_list.cbegin(); it != block_waiting_list.cend();){ // For each block in waiting list
      debug_suffix.str("");
      debug_suffix << "Update waiting list: starting with block: "<< it->first.data() << endl;
      debug(debug_suffix.str());

      int parentCount = 0; // How many parents are in Blockgraph
      for(auto &hash : it->second.GetParents() ){
        if (blockgraph.HasBlock(hash)){
          parentCount++;
        }
      }
      if (it->second.GetParents().size() == parentCount){ // If all parents are in blockgraph
        // trace purpose
        total_waitingBlockTime += Simulator::Now().GetSeconds() - waiting_list_time[it->first];
        count_waitingBlock++;
        waiting_list_time.erase(it->first);
        // TRACE << "WAITING_LIST" << " " << "DELETE" << " " << it->first.data() << endl;
        debug("Update waiting list: All parents are present -> Adding block.");

        AddBlockToBlockgraph(it->second);

        debug_suffix.str("");
        debug_suffix << "Update waiting list: Block: "<< it->first.data() << " erased from WL" << endl;
        debug(debug_suffix.str());

        it = block_waiting_list.erase(it);
        addblock = true;
        break;
      }
      else {
       debug("Not all parents from this block are present. keeping the block in the list");
       ++it;
      }
    }
  }
}

/**
 * Add block to local blockgraph.
 * If block was originally one of the midding_childless blocks, remove it from missing_childless_hashes.
 * Update recover_branch if applicable as well.
 * Remove transactions in block from the mempool.
 * Called by BlockTreatment(). BlockTreatment() handles updating of waiting list and missing list.
 */
void B4Mesh::AddBlockToBlockgraph(Block b){
  vector<string>::iterator it;
  bool isMissing = false;

  it = find(missing_childless_hashes.begin(), missing_childless_hashes.end(), b.GetHash());

  if (it != missing_childless_hashes.end()){
      debug_suffix.str("");
      debug_suffix << "AddBlockToBlockgraph: Erasing chidless block: "<< b.GetHash() << " from missing_childless_hashes" << endl;
      debug(debug_suffix.str());

      missing_childless_hashes.erase(it);
      isMissing = true;
  }

  // trace purpose 
  if (missing_childless_hashes.size() == 0 && isMissing == true){
    endmergetime = Simulator::Now().GetSeconds();
    createBlock = true;
  }

  //Update recover_branch structure
  if (recover_branch.count(b.GetHash()) > 0 ){
    cout << "Erasing from recover_branch" << endl;
    recover_branch.erase(b.GetHash());
  }
  
  UpdatingMempool(b.GetTransactions(), b.GetHash());

  debug_suffix.str("");
  debug_suffix << " AddBlockToBlockgraph: Adding the block "<< b.GetHash().data() << " to the blockgraph" << endl;
  debug(debug_suffix.str());

  blockgraph.AddBlock(b);
  // TRACE << "ADD_BLOCK" << " " << b.GetHash().data() << endl;
  // For traces purposes
  
  CreateGraph(b);
}

/**
 * Called by AddBlockToBlockgraph() to remove the transactions that are in a block added to Blockgraph from mempool.
 */
void B4Mesh::UpdatingMempool (vector<Transaction> transactions, string b_hash){
  for (auto &t : transactions){
    if (txn_mempool.count(t.GetHash()) > 0){
      // debug_suffix.str("");
      // debug_suffix << " UpdatingMempool: Transaction " << t.GetHash().data() << " found" << endl;
      // debug(debug_suffix.str());

      // Traces purpose 
      TraceTxLatency(t, b_hash);

      // erasing transaction from mempool
      txn_mempool.erase(t.GetHash());

      // traces purpose
      total_pendingTxTime += Simulator::Now().GetSeconds() - pending_transactions_time[t.GetHash()];
      count_pendingTx++;
      pending_transactions_time.erase(t.GetHash());
    }
    else {
      // debug_suffix.str("");
      // debug_suffix << " UpdatingMempool:  I don't know this transaction " << t.GetHash().data() << endl;
      // debug(debug_suffix.str());

      TraceTxLatency(t, b_hash);
    }
  }
}

// ********* REQUEST_BLOCK METHODS **************

/**
 * Scheduled at regular intervals by RecurrentTasks(), to ask other nodes for missing blocks.
 * Missing list maps hash of missing block to the node that is supposed to send it. TODO: clarify this.
 * So for all these missing blocks, send a REQUEST_BLOCK Application Packet to the sender.
 * Also send to the lastLeader, which is the leader of the most recent block added to local Blockgraph.
 */
void B4Mesh::Ask4MissingBlocks(){
  if (running == false){
    return;
  }

  if (missing_block_list.size() > 0 ){
    debug(" Ask4MissingBlocks: List of missing parents");
    for (auto &missing_b : missing_block_list){
      if (!IsBlockInWaitingList(missing_b.first)){ // maybe this check is not necessary
        debug_suffix.str("");
        debug_suffix << "Ask4MissingBlocks: asking for block: " << missing_b.first << " to node: " << GetIdFromIp(missing_b.second) << endl;
        debug(debug_suffix.str());

        string serie = missing_b.first;
        ApplicationPacket packet(ApplicationPacket::REQUEST_BLOCK, serie);

        SendPacket(packet, missing_b.second);

        if (GetIdFromIp(missing_b.second) != lastLeader){
          debug("Sending to leader also ");

          SendPacket(packet, GetIpAddressFromId(lastLeader));
        }
      }
    }
  }
  else {
    debug(" Ask4MissingBlocks: No blocks in waiting list");
  }

}

/**
 * Called by sending node in response to a REQUEST_BLOCK packet, from a node asking 4 missing blocks.
 */
void B4Mesh::SendBlockTo(string hash_p, Ipv4Address destAddr){
  if (blockgraph.HasBlock(hash_p)){
    debug_suffix.str("");
    debug_suffix << " SendBlockTo: Block " << hash_p << " found!" << endl;
    debug(debug_suffix.str());

    Block block = blockgraph.GetBlock(hash_p);

    debug_suffix.str("");
    debug_suffix << " SendBlockTo: Sending block to node: " << destAddr << endl;
    debug(debug_suffix.str());

    ApplicationPacket packet(ApplicationPacket::BLOCK, block.Serialize());
    SendPacket(packet, destAddr);

    // For traces purposes
    TraceBlockSend(GetIdFromIp(destAddr), Simulator::Now().GetSeconds(), stoi(block.GetHash()));
  }
  else {
    debug_suffix.str("");
    debug_suffix << " SendBlockTo: Block " << hash_p << " not found!" << endl;
    debug(debug_suffix.str());

    return;
  }
}

// ************** CALCUL AND GROUP DETECTION METHODS ************************

/**
 * Function called by Group Discovery Module, ie. b4mesh-mobility, to inform the app of the new group.
 * natChange = MERGE/SPLIT..
 * Schedules a call to UpdateTopologyInfo() after 5 seconds.
 */
void B4Mesh::ReceiveNewTopologyInfo(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natChange){
  debug(" ReceiveNewTopology: New topology information ");

  change++;

  //register last changement
  lastChange = natChange;

  Simulator::Schedule(Seconds(5),
      &B4Mesh::UpdateTopologyInfo, this, change, new_group);
}

/**
 * Function called by ReceiveNewTopologyInfo() after group change.
 * natChange = MERGE/SPLIT..
 * Schedules a call to UpdateTopologyInfo() after 5 seconds.
 * If it really is a topology change, update groupId, update groupId_register (which is a list of all past groupIds)
 * update previous and current groups.
 * If change is a MERGE, CLEAR THE RECOVER_BRANCH and set startMerge=true.
 * TODO: Is the above problematic? Clearing of recover_branch before the previous missing branches have been pulled??
 */
void B4Mesh::UpdateTopologyInfo(uint32_t tmp_change, pair<string, vector<pair<int, Ipv4Address>>> new_group){
  debug(" UpdateTopologyInfo");

  if (change == tmp_change){
    debug(" UpdateTopologyInfo: Checking if group ids are different! ");
    
    if (new_group.first != groupId){
      debug(" UpdateTopologyInfo: Confirming topology change! ");

      vector<pair<int, Ipv4Address>> n_group = new_group.second;
      groupId = new_group.first;
      if (find(groupId_register.begin(), groupId_register.end(), groupId) == groupId_register.end()){
        groupId_register.push_back(groupId);
      }
      //Updating Previous group
      previous_group = group;
      //Updating current group
      group = n_group;

      if (lastChange == MERGE){
        debug(" UpdateTopologyInfo: This is a merge change! ");

        recover_branch.clear(); // Is this a problematic line? Why is this necessary?
        startmergetime = Simulator::Now().GetSeconds();

        startMerge = true; // For StartMerge() to proceed.
      } else if (lastChange == SPLIT){
        debug(" UpdateTopologyInfo: This is a split change! ");
      }
    } else {
      debug(" UpdateTopologyInfo: Topology is the same! No changes applied! ");
    }
  } else {
    debug(" UpdateTopologyInfo: Topology not stable yet... waiting for last change... ! ");
  }
}

/**
 * Return nodes of current group that were NOT in the previous group, ie. just joined partition.
 * Called by Ask4ChildlessHashes(). TODO: Here
 */
vector<pair<int, Ipv4Address>> B4Mesh::GetNewNodes(){
  vector<pair<int, Ipv4Address>> res = vector<pair<int, Ipv4Address>> ();
  for(auto n : group){
    if (n.first != node->GetId()){
      if(find(previous_group.begin(), previous_group.end(), n) == previous_group.end()){
        // If a node is not in the old group; then is a new node
        res.push_back(n);
      }
    }
  }
  return res;
}

/**
 * Return nodes of current group that WERE in the previous group.
 * Called by GenerateMergeBlock(). TODO: Here
 */
vector<pair<int, Ipv4Address>> B4Mesh::GetNodesFromOldGroup(){
  vector<pair<int, Ipv4Address>> res = vector<pair<int, Ipv4Address>>  ();
  for (auto node : group){
    if(find(previous_group.begin(), previous_group.end(), node) != previous_group.end()){
      // If nodes from current group are also in previous_group
      res.push_back(node);
    }
  }
  return res;
}

/**
 * Returns nature of last change, ie. MERGE or SPLIT
 */
int B4Mesh::GetLastChange() {
  return lastChange;
}

// ********* CHANGE_TOPO METHODS **************

/**
 * Called by HaveNewLeader(), which is... TODO: here
 * Called by leader to begin merge process. If startMerge, topology stable, so can start a merge.
 * startMerge = true is set by UpdateTopologyInfo.
 * Sets createBlock to false to stop creation of any new blocks,
 * Then calls Ask4ChildlessHashes(). Once this is done, set startMerge to false again.
 * TODO: Clarify what exactly does startMerge entail.
 */
void B4Mesh::StartMerge(){
  if (running == false){
    return;
  }

  if (GetB4MeshOracle(node->GetId())->IsLeader() == false)
    return;

  if (startMerge == true){
    debug(" Starting Merge at Node: ");
    debug_suffix.str("");
    debug_suffix << " StartMerge: Leader node: " << node->GetId()
                << " starting the leader synchronization process..." << endl;
    debug(debug_suffix.str());

    createBlock = false;
    Ask4ChildlessHashes();
    startMerge = false;

  } else {
    debug("Can't start a merge now, topology not stable yet ");

    Simulator::Schedule(Seconds(1),
            &B4Mesh::StartMerge, this);
  }
}

/**
 * Called by leader in StartMerge(). 
 * Asks nodes that just joined this new partition for the hashes of their childless blocks,
 * by broadcasting CHANGE_TOPO Application Packet of type CHILDLESSBLOCK_REQ to them.
 * Then starts a timer to call timer_childless_fct().
 */
void B4Mesh::Ask4ChildlessHashes(){
  debug(" Ask4ChildlessHashes: In leader synch process ");

  childlessblock_req_hdr_t ch_req;
  ch_req.msg_type = CHILDLESSBLOCK_REQ;
  ApplicationPacket packet(ApplicationPacket::CHANGE_TOPO,
      string((char*)&ch_req, sizeof(ch_req)));

  vector<Ipv4Address> groupDestination = vector<Ipv4Address> ();

  debug("B4MESH: Ask4ChildlessHashes: Asking childless blocks to nodes:");

  for (auto &dest : GetNewNodes()){
    debug_suffix.str("");
    debug_suffix << " B4MESH: Ask4ChildlessHashes: Node: " << dest.first << endl;
    debug(debug_suffix.str());

    groupDestination.push_back(dest.second);
  }

  BroadcastPacket(packet, groupDestination);

  Simulator::Schedule(Seconds(6), 
            &B4Mesh::timer_childless_fct, this);
}

/**
 * Scheduled call after Ask4ChildlessHashes() by leader.
 * Assumes that after the delay, the nodes would have responded with their childless hashes to the leader,
 * and leader done processing them(? not sure about this).
 * Which means can set mergeBlock and createBlock=true, and call GenerateBlocks() to create the merge block.
 */
void B4Mesh::timer_childless_fct(){
  if (!createBlock){
    debug("Timer_childless has expired. Creating a merge block");
    mergeBlock = true;
    createBlock = true;
    GenerateBlocks();
  }
}

/**
 * Non-leader nodes call this in response to leader asking for their childless hashes.
 * Sends them as a CHANGE_TOPO Application Packet with type CHILDLESSBLOCK_REP
 */
void B4Mesh::SendChildlessHashes(Ipv4Address destAddr){
  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  string serie_hashes = "";

  for (auto &cb : myChildless){
    string tmp_str = cb;

    debug_suffix.str("");
    debug_suffix << " SendChildlessHashes: Childless block found : " << tmp_str;
    debug(debug_suffix.str());

    serie_hashes += tmp_str;
  }

  if (serie_hashes.size() > 0 ){
    childlessblock_rep_hdr_t ch_rep;
    ch_rep.msg_type = CHILDLESSBLOCK_REP;
    string serie((char*)&ch_rep, sizeof(childlessblock_rep_hdr_t));
    serie += serie_hashes;
    ApplicationPacket pkt(ApplicationPacket::CHANGE_TOPO, serie);
    SendPacket(pkt, destAddr, false);
  } else {
    debug(" SendChildlessHashes: No childless block found!");
  }
  //testing this for edge case:
  //createBlock=false; 
}

/**
 * Called by leader to respond to non-leader's childless hashes sent.
 * For each of the childless hashes, call ChildlessHashTreatment() on it.
 * After all processed, CheckMergeBlockCreation(), which checks if all childless hashes received,
 * and lets leader proceed with merge block creation before timer_childless_fct expires.
 */
void B4Mesh::ProcessChildlessResponse(const string& msg_payload, Ipv4Address senderAddr){
  string deserie = msg_payload;
  vector<string> sender_childless = vector<string> ();
  string tmp_hash = "";

  deserie = deserie.substr(sizeof(childlessblock_rep_hdr_t), deserie.size());

  //deserialisation
  while (deserie.size() > 0){
    tmp_hash = deserie.substr(0, 32);
    sender_childless.push_back(tmp_hash);
    deserie = deserie.substr(32, deserie.size());
  }

  for (auto &cb : sender_childless){
    ChildlessHashTreatment(cb, senderAddr);
  }
  
  // List of responses gathered by the leader node so far.
  debug("Current leader node list of responses: ");
  for (auto &node : recover_branch){
    debug_suffix.str("");
    debug_suffix << " ProcessChildlessResponse : Node: "  << node.second << " has childless block:" << node.first << endl;
    debug(debug_suffix.str());
  }
  CheckMergeBlockCreation();
}

/**
 * Called by a leader node to process a childless hash. ALSO called by non-leader nodes to process the childless hashes received
 * from a merge block.
 * RegisterNode(): Update local recover_branch, noting that the sender node has the childless block.
 * If leader's blockgraph and waiting list don't have the childless block, need to update missing list and missing_childless_hashes.
 * RegisterNode() too, to update recover_branch.
 */
void B4Mesh::ChildlessHashTreatment(string childless_hash, Ipv4Address senderAddr){
  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  vector<string>::iterator it;

  debug("Childless block treatment! ");

  if (find(myChildless.begin(), myChildless.end(), childless_hash) != myChildless.end()){
    debug_suffix.str("");
    debug_suffix << " ChildlessHashTreatment : This Childless block: "  << childless_hash << " is also a childless block of mine" << endl;
    debug(debug_suffix.str());

    RegisterNode(childless_hash, senderAddr);  
    return;
  } else { // If childless block is not a childless block of mine
    debug_suffix.str("");
    debug_suffix << " ChildlessHashTreatment : This Childless block: "  << childless_hash << " is not a childless block of mine" << endl;
    debug(debug_suffix.str());

    if (blockgraph.HasBlock(childless_hash)){ // Ignore childless hash since the blockgraph already has this block
      debug(" ChildlessHashTreatment: Childless block is present in local blockgraph...Ignoring childless block");

      return;
    } else {
      debug(" ChildlessHashTreatment: Childless block is not present in blockgraph...Checking if CB is present in the waiting list");

      if (IsBlockInWaitingList(childless_hash)){ // Ignore childless hash since node it is already in waiting list, just awaiting ancestors
        debug(" ChildlessHashTreatment: Childless block is already present in the block_waiting_list...Ignoring childless block");

        return;
      } else {
        debug(" ChildlessHashTreatment: Childless block is not present in waiting list...Checking if CB is present in missing_block_list");

        if (IsBlockInMissingList(childless_hash)){ // Childless hash already in missing_block list. Just note that this sender node also has it.
          debug(" ChildlessHashTreatment: Childless block already in missing_block_list. Ignoring childless block");

          RegisterNode(childless_hash, senderAddr); 
          return;
        } else { // Need to update missing_block_list and missing_childless_hashes with this.
          debug(" ChildlessHashTreatment: Block not in missing_block_list. Adding block to missing_block_list ");

          // Adding childless block hash in the list of missing block since we now know of it existence
          missing_block_list.insert({childless_hash, senderAddr}); 

          // Trace purpose
          missing_list_time[childless_hash] = Simulator::Now().GetSeconds();
          // TRACE << "MISSING_LIST" << " " << "INSERT" << " " << childless_hash.data() << endl;
          it = find(missing_childless_hashes.begin(), missing_childless_hashes.end(), childless_hash);

          if (it == missing_childless_hashes.end()){
            //If this childless block is not registered adding missing childless block to the list of childless block to recover
            debug(" ChildlessHashTreatment: Adding Childless block to vector missing_chidless");

            missing_childless_hashes.push_back(childless_hash);
          }
          RegisterNode(childless_hash, senderAddr);
        }
      }
    }
  }
}

/**
 * Called by ChildlessHashTreatment() to update recover_branch.
 * Recover_branch is a multimap of {childless_hash -> id of a node that can send this block}.
 * Each childless hash can be mapped to multiple nodes.
 */
void B4Mesh::RegisterNode(string childless_hash, Ipv4Address senderAddr){
  debug(" Register node answer");

  int idSender = GetIdFromIp(senderAddr);
  bool findnode = false;
  
  if (recover_branch.count(childless_hash) > 0 ){ // If the childless block has already been registered 
    findnode = false;
    auto range = recover_branch.equal_range(childless_hash);

    for (auto i = range.first; i != range.second; ++i){
      if (i->second == idSender){
        findnode = true;
      }
    }
    if (!findnode){
      // Registering a new node with an already existing childless block
      debug_suffix.str("");
      debug_suffix << " RegisterNode : Adding new childless block : "  << childless_hash << " for node: " << idSender << endl;
      debug(debug_suffix.str());

      recover_branch.insert(pair<string, int> (childless_hash, idSender));
    } else {
      debug_suffix.str("");
      debug_suffix << " RegisterNode : Node: "  << idSender << " already registered with same childless block: " << childless_hash << endl;

      debug(debug_suffix.str());
    }
  } else { // The chidless block has not been registered yet
    debug_suffix.str("");
    debug_suffix << " RegisterNode : Adding new childless block : "  << childless_hash << " for node: " << idSender << endl;
    debug(debug_suffix.str());

    recover_branch.insert(pair<string, int> (childless_hash, idSender));
  }
}

/**
 * Called by leader node during ProcessChildlessResponse(), to see if can start creating a merge block
 * early before time ends. Condition is that all the unique nodes in the recover_branch have one response.
 */
void B4Mesh::CheckMergeBlockCreation(){
  int ans = 0;
  bool findnode = false;

  for (auto &n_nodes : GetNewNodes()){
    findnode = false;
    for (auto &e : recover_branch){
      if (e.second == n_nodes.first){
        findnode = true;
      }
    }
    if (!findnode){
      debug_suffix.str("");
      debug_suffix << " CheckMergeBlockCreation : Answer of node: "  << n_nodes.first << " not received yet" << endl;
      debug(debug_suffix.str());

      ans++;
    }
  }

  if (ans == 0 && !createBlock){
    // Create Merge BLOCK
    mergeBlock = true;

    debug("A merge block can be created now.");

    createBlock = true;
    GenerateBlocks();
  }
}

/**
 * Called by a node in response to a SendBranchRequest() by another node, asking for the entire branch.
 * TODO: Clarify. Shouldn't the payload just contain a single hash? Why the function seems to account for multiple.
 */
void B4Mesh::SendBranch4Sync(const string& msg_payload, Ipv4Address destAddr){
  string deserie = msg_payload;
  string block_hash = "";
  string gp_id = "";
  vector<string> block_hash_v = vector<string> ();
  vector<string> gp_id_v = vector<string> ();

  deserie = deserie.substr(sizeof(group_branch_hdr_t), deserie.size());

  // Extracting the block hashes in payload and putting them in vector block_hash_v
  while (deserie.size() > 0){
    block_hash = deserie.substr(0, 32);
    if(find(block_hash_v.begin(), block_hash_v.end(), block_hash) != block_hash_v.end()){
    } else {
      block_hash_v.push_back(block_hash);
    }
    deserie = deserie.substr(32, deserie.size());
  }

  /** for every hash in block_hash_v
   *  get the groupId of each block and filling the vector gp_id_v
   *  This vector has the GroupId of the blocks in the request.
   */
  for (auto &b_h : block_hash_v){
    gp_id = blockgraph.GetGroupId(b_h);
    if (find(gp_id_v.begin(), gp_id_v.end(), gp_id) != gp_id_v.end()){ //GroupId already existent "
    } else {
      gp_id_v.push_back(gp_id);
    }
  }

  // Sending blocks with the same groupId to node. Because these should correspond to the same branch.
  // Added small delay
  float delayStep = 50; // milliseconds
  float currentDelay = 0.0;

  for (auto g_id : gp_id_v){
    vector<Block> blocks_group = blockgraph.GetBlocksFromGroup(g_id);
    for (auto b_grp : blocks_group){
      debug_suffix.str("");
      debug_suffix << " SendBranch4Sync: Sending block with hash " << b_grp.GetHash() << " with groupId: " << g_id;
      debug_suffix << " to node " << destAddr << endl;
      debug(debug_suffix.str());

      ApplicationPacket packet(ApplicationPacket::BLOCK, b_grp.Serialize());

      Simulator::Schedule(MilliSeconds(currentDelay),
        &B4Mesh::SendPacket, this, packet, destAddr, false);
      currentDelay = currentDelay + delayStep;

      // For trace purposes
      TraceBlockSend(GetIdFromIp(destAddr), Simulator::Now().GetSeconds(), stoi(b_grp.GetHash()));
    }
  }
}

// ************** GENERATION DES BLOCKS *****************************

/**
 * Called by leader node to check if can create a block.
 */
bool B4Mesh::TestPendingTxs(){
  if ((int)Simulator::Now().GetSeconds() - lastBlock_creation_time > TIME_BTW_BLOCK){
    if ( SizeMempoolBytes()/1000 > MIN_SIZE_BLOCK){
      debug(" TestPendingTxs: Enough txs to create a block.");
      return true;
    } else {
      debug(" Not enough transactions to form a block");
      return false;
    }
  } else {
    debug(" TestPendingTxs: Not allowed to create a block so fast");
    return false;
  }
}

/**
 * Returns the transactions from mempool with smallest timestamp. 
 * For block creation by leader.
 */
vector<Transaction> B4Mesh::SelectTransactions(){
  vector<Transaction> transactions;

  while((transactions.size()*TX_MEAN_SIZE)/1000 < MAX_SIZE_BLOCK){
    pair<string, double> min_ts = make_pair("0", 9999999);
    for(auto &t : txn_mempool){
      if(find(transactions.begin(), transactions.end(), t.second) != transactions.end()){
        continue;
      } else {
        if (t.second.GetTimestamp() < min_ts.second){
          min_ts = make_pair(t.first, t.second.GetTimestamp());
        } else {
          continue;
        }
      }
    }
    if (min_ts.first != "0"){
      // debug_suffix.str("");
      // debug_suffix << "SelectTransactions : Getting tx: " << min_ts.first << " with time stamp: " << min_ts.second << endl;
      // debug(debug_suffix.str());

      transactions.push_back(txn_mempool[min_ts.first]);
    } else {
      break;
    }
  }
  return transactions;
}

/**
 * Returns true if there are missing_childless_hashes.
 * Called by GenerateBlocks(): don't start a merge while one is already in progress.
 * Called by GenerateRegularBlock(): to ensure that parents are chosen correctly. Don't include parents that are currently involved
 * in branch recovery.
 */
bool B4Mesh::IsMergeInProcess(){
  debug(" IsMergeInProcess: List of missing childless: ");

  for (auto &mc : missing_childless_hashes){
    debug_suffix.str("");
    debug_suffix << " Childless: " << mc << endl;
    debug(debug_suffix.str());
  }

  if (missing_childless_hashes.size() > 0){ // There is a merge synchronization in process
    return true;
  } else {
    return false;
  }
}

/**
 * Generates a blank slate block, then turns it into either a regular or merge block by passing to respective functions.
 * Note that a merge block can also be created to join 2 childess blocks even without an actual merge(topology change) taking place.
 */
void B4Mesh::GenerateBlocks(){
  debug(" GenerateBlock: Creating a Block ");

  Block block;

  block.SetLeader(node->GetId());
  block.SetGroupId(groupId);

  if (mergeBlock){ // This block is supposed to be a merge block.
      debug(" GenerateBlock: Generating a Merge block due to merge");

      block = GenerateMergeBlock(block);
  } else {
    if (blockgraph.GetChildlessBlockList().size() > 1){
      // If block has more than one parent and mergeBlock flag is false
      if (IsMergeInProcess()){
        debug(" GenerateBlock: Generating a Regular block because a merge is already in process");
        block = GenerateRegularBlock(block);
      } else {
        debug(" GenerateBlock: Generating a Merge block not because of a merge process");
        block = GenerateMergeBlock(block);
      }
    } else {
      debug(" GenerateBlock: Generating a Regular block");
      block = GenerateRegularBlock(block);
    }
  }

  int index = -1;
  for (auto &p : block.GetParents()){
    index = max(index, blockgraph.GetBlock(p).GetIndex());
  }

  block.SetIndex(index+1);
  block.SetTimestamp(Simulator::Now().GetSeconds());

  lastBlock_creation_time = (int)Simulator::Now().GetSeconds();

   // FOR TRACES purposes
  BlockCreationRate(block.GetHash(), block.GetGroupId(), block.GetTxsCount(), block.GetTimestamp());

  float process_time = GetExecTime(block.GetTxsCount());
  p_b_c_t += process_time;

  Simulator::Schedule(MilliSeconds(process_time),
        &B4Mesh::SendBlockToConsensus, this, block);
  
}

/**
 * Takes a block and turns it into a merge block.
 * Transactions are TODO: Here
 * Parents are: all the childless blocks of the blockgraph, and missing_childless_hashes?
 */
Block B4Mesh::GenerateMergeBlock(Block &block){
  debug(" GenerateMergeBlock: Creating a Merge Block ");

  vector<Transaction> transactions = vector<Transaction> ();
  vector<string> p_block = vector <string> ();

  // Settle transactions
  for (auto hash : blockgraph.GetChildlessBlockList()){
    for (auto node : GetNodesFromOldGroup()){
      Transaction t;
      string payload = to_string(node.first) + " " + hash;

      debug_suffix.str("");
      debug_suffix << " GenerateMergeBlock: TX OLD: " << payload << endl;
      debug(debug_suffix.str());

      t.SetPayload(payload);
      t.SetTimestamp(Simulator::Now().GetSeconds());
      transactions.push_back(t);
    }
  }

  for (auto &node : recover_branch) {
    Transaction t;
    string payload = to_string(node.second) + " " + node.first;

    debug_suffix.str("");
    debug_suffix << " GenerateMergeBlock: TX RECOVER: " << payload << endl;
    debug(debug_suffix.str());

    t.SetPayload(payload);
    t.SetTimestamp(Simulator::Now().GetSeconds());
    transactions.push_back(t);
  }

  block.SetTransactions(transactions);

  // Settle parents
  for (auto &parent : blockgraph.GetChildlessBlockList()){
    p_block.push_back(parent);
  }
  
  if (missing_childless_hashes.size() > 0){
    for (auto &pb : missing_childless_hashes){
      if(find(p_block.begin(), p_block.end(), pb) == p_block.end()){
        p_block.push_back(pb);
      }
    }
  }

  block.SetParents(p_block);

  block.SetBlockType(1);  // new -??

  mergeBlock = false;
  return block;
}

/**
 * Takes a block and turns it into a regular block.
 */
Block B4Mesh::GenerateRegularBlock(Block &block){
  debug(" GenerateRegularBlock: Creating a Regular Block ");

  vector<string> p_block = vector <string> ();
  vector<Transaction> transactions = SelectTransactions();

  if (blockgraph.GetChildlessBlockList().size() > 1 ){
    if (IsMergeInProcess()){
      string tmp_block_groupId;
      for (auto &p : blockgraph.GetChildlessBlockList()){
        tmp_block_groupId = blockgraph.GetBlock(p).GetGroupId();
        if (find(groupId_register.begin(), groupId_register.end(), tmp_block_groupId) != groupId_register.end()){
          p_block.push_back(p);
        } else {
          debug_suffix.str("");
          debug_suffix << " GenerateRegularBlock : The childless block: " << p << " is a block that comes from another branch and its branch is in recover process";
          debug_suffix << " Not adding this childless block as parent... " << endl;
          debug(debug_suffix.str());
        }
      }
      block.SetParents(p_block);
    }
  } else {
    block.SetParents(blockgraph.GetChildlessBlockList());
  }

  block.SetTransactions(transactions);
  block.SetBlockType(0);  // new

  for (auto &t : transactions){
    txn_mempool.erase(t.GetHash());
  }
  return block;
}

/**
 * Sends block to consensus module for dissemination to other nodes in group.
 * Called by leader node, at the end of GenerateBlocks(). 
 */
void B4Mesh::SendBlockToConsensus(Block b){
  debug_suffix.str("");
  debug_suffix << " SendBlockToConsensus:   " << b.GetHash() << " to C4M protocol" << endl;
  debug(debug_suffix.str());
  
  GetB4MeshOracle(node->GetId())->InsertEntry(b);
  // Trace for block propagation delai. Not exact because need to measure from consenus
  
  for (auto &node : group){
    TraceBlockSend(node.first, Simulator::Now().GetSeconds(), stoi(b.GetHash()));
  }
}


//???
void B4Mesh::CourseChange(string context, Ptr<const MobilityModel> mobility){
  Vector pos = mobility->GetPosition();
  Vector vel = mobility->GetVelocity();
  // cout << Simulator::Now().GetSeconds() << " Node: " << node->GetId() <<", model =" << mobility->GetTypeId() << ", POS: x =" << pos.x << ", y =" << pos.y
  //      << ", z =" << pos.z << "; VEL: " << vel.x << ", y =" << vel.y << ", z =" << vel.z << endl;
}

// **************** CONSENSUS INTERFACES METHODS *****************

/**
 * Invoked by consensus module after it has received and committed a block.
 * Then calls GetBlock() to treat the block and so on.
 */
void B4Mesh::RecvBlock(void * b4, Block b) {
  // debug_suffix.str("");
  // debug_suffix << "Received a block from consensus module and RecvBlock with hash" << b.GetHash().data() <<endl;
  // debug(debug_suffix.str());
  // cout << " Node: " << node->GetId() << "Received a block from consensus module and RecvBlock with hash" << b.GetHash().data() <<endl;

  Ptr<B4Mesh> ptr_b = (B4Mesh *) b4;
  ptr_b->GetBlock(b);
}

/**
 * Schedule BlockTreatment() on this block.
 */
void B4Mesh::GetBlock(Block b) {
    debug_suffix.str("");
    debug_suffix << "Received a new block : " << b << " from "  << " with hash " << b.GetHash().data() << endl;
    debug(debug_suffix.str());

    // For traces purposes
    TraceBlockRcv(Simulator::Now().GetSeconds(), stoi(b.GetHash()));

    // Processing delay of the block. It is calculated base on the packet size.
    float process_time = GetExecTimeBKtreatment(blockgraph.GetBlocksCount());
    p_b_t_t += process_time;

    Simulator::Schedule(MilliSeconds(process_time),
          &B4Mesh::BlockTreatment, this, b);
}

void B4Mesh::HaveNewLeader(void * b4) {
  Ptr<B4Mesh> ptr_b = (B4Mesh *) b4;
  ptr_b->debug("B4MESH: HaveNewLeader!");
  if (ptr_b->GetLastChange() == MERGE && ptr_b->GetB4MeshOracle(ptr_b->node->GetId())->IsLeader() ){
    ptr_b->debug_suffix.str("");
    ptr_b->debug_suffix << "HaveNewLeader: leader node: " <<  ptr_b->node->GetId() << " is going to start a merge" << endl;
    ptr_b->debug(ptr_b->debug_suffix.str());
    ptr_b->StartMerge();
  }
}

// *************** TRACES AND PERFORMANCES ***********************************

//{time, number ot txns, size, usage%}
void B4Mesh::MempoolSampling(){

  double time = Simulator::Now().GetSeconds();
  auto mempool = pair<pair<double, int>, pair<int, float>> ();
  mempool = make_pair(make_pair(time, (int)txn_mempool.size()),
                make_pair(SizeMempoolBytes(), (float)((SizeMempoolBytes()/1000)*100)/(float)(SIZE_MEMPOOL)));
  mempool_info.push_back(mempool); 

/*
  pair<int, int> txs_num_bytes = pair<int, int> ();
  txs_num_bytes = make_pair( (int)pending_transactions.size(), SizeMempoolBytes());
  mempool_info[(double)Simulator::Now().GetSeconds()] = txs_num_bytes;
*/

 /* 
  TRACE << "MEMPOOL_INFO" << " " << "NUMTXS" << " " << txs_num_bytes.first
        << " " << "SIZEMEM" << " " << txs_num_bytes.second << endl;
*/
}


// {time, transaction per second, transactions in blockgraph, number of transactions generated by ths node}
void B4Mesh::TxsPerformances (){

  double time = Simulator::Now().GetSeconds();
  auto txs_info = pair<pair<double, float>, pair<int, int>> ();
  txs_info = make_pair(make_pair(time, (float)(blockgraph.GetTxsCount()/time)), 
                  make_pair(blockgraph.GetTxsCount(), numTxsG));
  txs_perf.push_back(txs_info);

/*
  double time = Simulator::Now().GetSeconds();
  pair<float, int> tps_txsinBG = pair<float, int> ();
  tps_txsinBG = make_pair( (float)(blockgraph.GetTxsCount()/time), blockgraph.GetTxsCount());
  txs_perf[time] = tps_txsinBG;
  */

}


void B4Mesh::BlockCreationRate(string hashBlock, string b_groupId, int txCount, double blockTime){
  auto block_info = pair<pair<int, int>, pair<int, double>> ();
  block_info = make_pair(make_pair(stoi(hashBlock), stoi(b_groupId.data())), make_pair(txCount, blockTime));
  traces->ReceivedBlockInfo(block_info);
  /*
  TRACE << "NEW_BLOCK" << " " << "BLOCK_HASH" << " " << block_info.first.first
        << " " << "BLOCK_GROUPID" << " " << block_info.first.first << " " << "BLOCK_TXS" << " " << block_info.second.first
        << " " << "BLOCK_CREATION_TIME" << " " << block_info.second.second << endl;
*/
}

void B4Mesh::TraceBlockSend(int nodeId, double timestamp, int hashBlock){
  map<int, pair<double, double>>::iterator it = GetB4MeshOf(nodeId)->time_SendRcv_block.find(hashBlock); 

  if (it != GetB4MeshOf(nodeId)->time_SendRcv_block.end() ){
    it->second.first = timestamp;
  } else {
    GetB4MeshOf(nodeId)->time_SendRcv_block[hashBlock] = make_pair(timestamp,-1);
  }

 // TRACE << "SEND_BLOCK" << " " << hashBlock << " " << "TO" << " " << nodeId << endl;
}

void B4Mesh::TraceBlockRcv(double timestamp, int hashBlock){
  map<int, pair<double, double>>::iterator it = time_SendRcv_block.find(hashBlock);

  if (it != time_SendRcv_block.end()){
    it->second.second = timestamp;
  }

//  TRACE << "RECV_BLOCK" << " " << hashBlock << endl;
}

void B4Mesh::TraceTxsSend(int nodeId, int hashTx, double timestamp){
  map<int, pair<double, double>>::iterator it = GetB4MeshOf(nodeId)->time_SendRcv_txs.find(hashTx); 

  if (it != GetB4MeshOf(nodeId)->time_SendRcv_txs.end() ){
    it->second.first = timestamp;
  } else {
    GetB4MeshOf(nodeId)->time_SendRcv_txs[hashTx] = make_pair(timestamp,-1);
  }
}

void B4Mesh::TraceTxsRcv(int hashTx, double timestamp){
   map<int, pair<double, double>>::iterator it = time_SendRcv_txs.find(hashTx);

  if (it != time_SendRcv_txs.end()){
    it->second.second = timestamp;
  }
}

void B4Mesh::TraceTxLatency(Transaction t, string b_hash){
  TxsLatency.insert(pair<string, pair<string, double>> (b_hash.data(), make_pair(t.GetHash().data(), Simulator::Now().GetSeconds() - t.GetTimestamp())));
}

int B4Mesh::SizeMempoolBytes (){
  int ret = 0;
  for (auto &t : txn_mempool){
    ret += t.second.GetSize();
  }
  return ret;
}

int B4Mesh::GetNumTxsGen(){
  return numTxsG;
}

int B4Mesh::GetNumReTxs(){
  return numRTxsG;
}

void B4Mesh::CreateGraph(Block &b){
  vector<string> parents_of_block = b.GetParents();
  pair<int,pair<int,int>> create_blockgraph = pair<int,pair<int,int>> ();

  for (auto &p : parents_of_block){
    if (p == "01111111111111111111111111111111"){
      create_blockgraph = make_pair(0,
                                  make_pair(0,stoi(b.GetHash())));
      blockgraph_file.push_back(create_blockgraph);
    } else {
      create_blockgraph = make_pair(stoi(b.GetGroupId()),
                                  make_pair(stoi(p),stoi(b.GetHash())));
      blockgraph_file.push_back(create_blockgraph);
    }
  }
}

float B4Mesh::GetExecTime(int size) {       // size in bytes

  //  The execution time is modeled by a Lognormal random variable
  //  Lognormal -- (mean, sd)
  //  The mean and sd is related to the size of the block

  //  To calculate the mean and sd according to the size, we use a linear regression
  //  Considering the values from a number of tests ran for different block sizes
  //  y = ax + b     --- Linear regression

  float mean_a =  0.018136;
  float mean_b = -1.919424;

  float sd_a = -0.00052039;
  float sd_b =  0.18708;

  float mu = mean_a * size + mean_b;
  float sigma = sd_a * size + sd_b;

  //int bound = MIN_BLOCK_GEN_DELAY;

  Ptr<LogNormalRandomVariable> time = CreateObject<LogNormalRandomVariable> ();
  time->SetAttribute ("Mu", DoubleValue (mu));
  time->SetAttribute ("Sigma", DoubleValue (sigma));
  return time->GetValue()/1000.0;        // In milliseconds
}

//************** Added ******************************
float B4Mesh::GetExecTimeTXtreatment (int Nblock) {       // size in bytes

  //  The execution time is modeled by a Lognormal random variable
  //  Lognormal -- (mean, sd)
  //  The mean and sd is related to the size of the block

  //  To calculate the mean and sd according to the size, we use a linear regression
  //  Considering the values from a number of tests ran for different block sizes
  //  y = ax + b     --- Linear regression

  float mean_a =  0.01626;
  float mean_b = 3.181;

  float sd_a = -0.0008167;
  float sd_b =  0.2121;

  float mu = mean_a * Nblock + mean_b;
  float sigma = sd_a * Nblock + sd_b;

  Ptr<LogNormalRandomVariable> time = CreateObject<LogNormalRandomVariable> ();
  time->SetAttribute ("Mu", DoubleValue (mu));
  time->SetAttribute ("Sigma", DoubleValue (sigma));
  return time->GetValue()/1000.0;        // In milliseconds
}

float B4Mesh::GetExecTimeBKtreatment (int Nblock) {       // size in bytes

  //  The execution time is modeled by a Lognormal random variable
  //  Lognormal -- (mean, sd)
  //  The mean and sd is related to the size of the block

  //  To calculate the mean and sd according to the size, we use a linear regression
  //  Considering the values from a number of tests ran for different block sizes
  //  y = ax + b     --- Linear regression

  float mean_a =  0.01128;
  float mean_b = 3.831;

  float sd_a = -0.0006637;
  float sd_b =  0.1952;

  float mu = mean_a * Nblock + mean_b;
  float sigma = sd_a * Nblock + sd_b;

  Ptr<LogNormalRandomVariable> time = CreateObject<LogNormalRandomVariable> ();
  time->SetAttribute ("Mu", DoubleValue (mu));
  time->SetAttribute ("Sigma", DoubleValue (sigma));
  return time->GetValue()/1000.0;        // In milliseconds
}


void B4Mesh::StopApplication(){
  running = false;
  //if (recv_sock ==0){
  if (!recv_sock){
    recv_sock->Close();
    recv_sock->SetRecvCallback (MakeNullCallback<void, Ptr<Socket> > ());
    recv_sock = 0;
  }

  GenerateResults();
  debug("GENERATING RESULTS");
// /*
  if (node->GetId() == 0){
    PerformancesB4Mesh();
  }
  // */
}

void B4Mesh::GenerateResults(){

  // Délais Propagation Block
  ofstream output_file1;
  char filename1[80];
  sprintf(filename1, "Traces/Delai_Prop_Block-%d.txt", node->GetId());
  std::cout << "Saving to: " << filename1 << std::endl;
  output_file1.open(filename1, ios::out);
  output_file1 << "#BlockHash" << " " << "TimeSend" << " " << "TimeReceve" << endl;
  for (auto it : time_SendRcv_block){
    if (it.second.second != -1 ){
      output_file1 << it.first << " " << it.second.first << " " << it.second.second << endl;
    }
  }
  output_file1.close();

  // Délais Propagation Txs
  ofstream output_file2;
  char filename2[60];
  sprintf(filename2, "Traces/Delai_Prop_Txs-%d.txt", node->GetId());
  output_file2.open(filename2, ios::out);
  output_file2 << "#TxHash" << " " << "TimeSend" << " " << "TimeReceve" << endl;
  for (auto it : time_SendRcv_txs){
    if (it.second.second != -1 ){
      output_file2 << it.first << " " << it.second.first << " " << it.second.second << endl;
    }
  }
  output_file2.close();

  // Throughput b4mesh protocol
  ofstream output_file3;
  char filename3[50];
  sprintf(filename3, "Traces/b4mesh_throughput-%d.txt", node->GetId());
  output_file3.open(filename3, ios::out);
  output_file3 << "#time" << " " << "bytes-send" << endl;
  for (auto it : b4mesh_throughput){
    output_file3 << it.first << " " << it.second << endl;
  }
  output_file3.close();

  //  Transaction throughput performances
  // #it.first = Time ; it.second.first = tps ; it.second.second = txs in blockgraph
  ofstream output_file4;
  char filename4[60];
  sprintf(filename4, "scratch/b4mesh/Traces/live_txs_perf-%d.txt", node->GetId());
  output_file4.open(filename4, ios::out);
  output_file4 << "Time" << " " << "tps" << " " << "txs_in_bg" << " " << "txs_Gen" << endl;
  for (auto it : txs_perf){
    output_file4 << it.first.first << " " << it.first.second << " " << it.second.first << " " << it.second.second << endl;
  }
  output_file4.close();
  
  // Transaction Latency
  // #it.first = TxBlock ; it.second.first = TXHash ; it.second.second = tx latency 
  ofstream output_file5;
  char filename5[60];
  sprintf(filename5, "Traces/TxsLatency-%d.txt", node->GetId());
  output_file5.open(filename5, ios::out);
  for (auto it : TxsLatency){
    output_file5 << it.first << " " << it.second.first << " " << it.second.second << endl;
  }
  output_file5.close();

  // ---The blockgraph of each node. ----
  ofstream output_file6;
  char filename6[50];
  sprintf(filename6, "Traces/blockgraph-%d.txt", node->GetId());
  output_file6.open(filename6, ios::out);
  output_file6 << "#BlockGroup" << " " << "ParentBlock" << " " << "BlockHash" << endl;
  for (auto &it : blockgraph_file){
    output_file6 << it.first << " " << it.second.first << " " << it.second.second << endl;
  }

  // New: list blocks in waiting list

  // vector<string> parents_of_block = b.GetParents();
  // pair<int,pair<int,int>> create_blockgraph = pair<int,pair<int,int>> ();

  // for (auto &p : parents_of_block){
  //   if (p == "01111111111111111111111111111111"){
  //     create_blockgraph = make_pair(0,
  //                                 make_pair(0,stoi(b.GetHash())));
  //     blockgraph_file.push_back(create_blockgraph);
  //   } else {
  //     create_blockgraph = make_pair(stoi(b.GetGroupId()),
  //                                 make_pair(stoi(p),stoi(b.GetHash())));
  //     blockgraph_file.push_back(create_blockgraph);
  //   }
  // }

  output_file6.close();

  ofstream output_file7;
  char filename7[50];
  sprintf(filename7, "Traces/waiting_list-%d.txt", node->GetId());
  output_file7.open(filename7, ios::out);
  output_file7 << "Waitinglist" << endl;
  output_file7 << "Parent Hash" << " BlockHash" << endl;
  for (auto &pair: block_waiting_list) { //{block hash, block}
    vector<string> parents_of_block = pair.second.GetParents();
    for (auto &p : parents_of_block) {
      output_file7 << p << pair.first << endl;
    } 
  }
  output_file7.close();


  // Mempool information
  ofstream output_file9;
  char filename9[60];
  sprintf(filename9, "Traces/live_mempool-%d.txt", node->GetId());
  output_file9.open(filename9, ios::out);
  output_file9 << "TimeSimulation" << " " << "NumTxs" << " " << "SizeMempool(Bytes)" << " " << " MempoolUsage" << endl;
  for (auto it : mempool_info){
    output_file9 << it.first.first << " " << it.first.second << " " << it.second.first << " " << it.second.second << endl;
  }
  output_file9.close();
  

  // Print performances
  ofstream output_file10;
  char filename10[50];
  sprintf(filename10, "Traces/Performances-%d.txt", node->GetId());
  output_file10.open(filename10, ios::out);
  output_file10 << std::fixed << std::setprecision(2);

  output_file10 << "********* PACKET RELATED PERFORMANCES *****************" << endl;
  output_file10 << "B4Mesh: Size of all packets sent: " << sentPacketSizeTotal << endl;
  output_file10 << "B4MeshOracle: Size of packets sent for consensus: " << GetB4MeshOracle(node->GetId())->sentPacketSizeTotalConsensus << endl;
  output_file10 << "B4Mesh: Size of transaction packets sent: " << sentTxnPacketSize << endl;
  

  output_file10 << "********* BLOCKGRAPH RELATED PERFORMANCES *****************" << endl;
  output_file10 << "B4Mesh: Time of simulation: " << Simulator::Now().GetSeconds() << "s" << endl;
  output_file10 << "B4Mesh: Size of the blockgraph (bytes) : " << blockgraph.GetByteSize()  << endl;
  output_file10 << "B4Mesh: Packet lost due to messaging problems: " << lostPacket << endl;
  output_file10 << "B4Mesh: Time to merge a branch: " << endmergetime - startmergetime << "s" << endl;
  output_file10 << "B4Mesh: p_t_t_t : " << p_t_t_t/1000 << "s " << endl;
  output_file10 << "B4Mesh: p_t_c_t : " << p_t_c_t/1000 << "s " << endl;
  output_file10 << "B4Mesh: p_b_t_t : " << p_b_t_t/1000 << "s " << endl;
  output_file10 << "B4Mesh: p_b_c_t : " << p_b_c_t/1000 << "s " << endl;
  output_file10 << "********* BLOCK RELATED PERFORMANCES *****************" << endl;
  output_file10 << "B4mesh: Total number of blocks in blockgraph: " << blockgraph.GetBlocksCount() << endl;
  output_file10 << "B4Mesh: Mean size of a block in the blockgraph (bytes): " << blockgraph.GetByteSize()/blockgraph.GetBlocksCount() << endl;
  output_file10 << "B4Mesh: Number of dumped blocks (already in block_waiting_list or in blockgraph): " << numDumpingBlock << endl;
  output_file10 << "B4Mesh: Number of remaining blocks' references in the missing_block_list: " << missing_block_list.size() << endl;
  for (auto &b : missing_block_list){
    output_file10 << "B4Mesh: - Block #: " << b.first.data() << " is missing " << endl;
  }
  output_file10 << "B4Mesh: Mean time that a blocks' reference spends in the missing_block_list: " << total_missingBlockTime / count_missingBlock << "s" << endl;
  output_file10 << "B4Mesh: Number of remaining blocks in the block_waiting_list: " << block_waiting_list.size() << endl;
  for (auto &b : block_waiting_list){
      output_file10 << "B4Mesh: - Block #: " << b.first << " is in block_waiting_list " << endl;
  }
  output_file10 << "B4Mesh: Mean time that a block spends in the block_waiting_list: " << total_waitingBlockTime / count_waitingBlock << "s" << endl;
  output_file10 << "********* TRANSACTIONS RELATED PERFORMANCES *****************" << endl;
  output_file10 << "B4mesh: Total number of transactions in the blockgraph: " << blockgraph.GetTxsCount() << endl;
  output_file10 << "B4Mesh: Number of transactions generated by this node: " << GetNumTxsGen() << endl;
  output_file10 << "B4mesh: Number of committed transactions per second: " << (float)(blockgraph.GetTxsCount())/Simulator::Now().GetSeconds() << endl;
  output_file10 << "B4Mesh: Size of all transactions in the blockgraph (bytes): " << blockgraph.GetTxsByteSize() << endl;
  output_file10 << "B4Mesh: Mean number of transactions per block: " << blockgraph.MeanTxPerBlock() << endl;
  output_file10 << "B4Mesh: Mean time that a transaction spends in the mempool: " << total_pendingTxTime / count_pendingTx << "s" << endl;
  output_file10 << "B4Mesh: Number of remaining transactions in the mempool: " << txn_mempool.size() << endl;   
  output_file10 << "B4Mesh: Number of re transmissions : " << GetNumReTxs() << endl;
  output_file10 << "B4Mesh: Transactions lost due to mempool space: " << lostTrans << endl;
  output_file10 << "B4Mesh: Number of dumped transactions (already in blockgraph or in mempool): " << numDumpingTxs << endl;
  output_file10 << "B4Mesh: Transactions with multiple occurance in BG : "  << endl;
  int TxRep = blockgraph.ComputeTransactionRepetition();
  output_file10 << TxRep << std::endl;
  output_file10.close();

}

void B4Mesh::PerformancesB4Mesh(){
  int n = peers.size();
  int totalTxs = GetNumTxsGen();
  float meanTxPerB = blockgraph.MeanTxPerBlock();
  int meanBkSize = blockgraph.GetByteSize()/blockgraph.GetBlocksCount();
  int txCount = blockgraph.GetTxsCount();
  int bgSize = blockgraph.GetByteSize();
  int txSize = blockgraph.GetTxsByteSize();

  for (int i=1; i<n; i++){
    meanTxPerB += GetB4MeshOf(i)->blockgraph.MeanTxPerBlock();
    meanBkSize += GetB4MeshOf(i)->blockgraph.GetByteSize()/blockgraph.GetBlocksCount();
    bgSize += GetB4MeshOf(i)->blockgraph.GetByteSize();
    txCount += GetB4MeshOf(i)->blockgraph.GetTxsCount();
    txSize += GetB4MeshOf(i)->blockgraph.GetTxsByteSize();
    totalTxs += GetB4MeshOf(i)->GetNumTxsGen();
  }

  meanTxPerB = meanTxPerB / n;
  meanBkSize = meanBkSize / n;
  bgSize = bgSize / n;
  txCount = txCount / n;
}


void B4Mesh::debug(string suffix){
  std::cout << Simulator::Now().GetSeconds() << "s: B4Mesh : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
 
}
