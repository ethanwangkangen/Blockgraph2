#include "b4mesh.h"

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

//Constructor - Global variables initialization
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
  missing_childless = vector<string> ();
  block_waiting_list = map<string, Block>();
  pending_transactions = map<string, Transaction>();
  recover_branch = multimap<string, int> ();
  mergeBlock = false;
  createBlock = true;
  startMerge = false;
  lastBlock_creation_time = 0;
  change = 0;   
  lastLeader = -1;

  /* Blockgraph parameters */
  interArrival = -1;

  /* Variables for the blockchain performances */
  numTxsG = 0; 
  numRTxsG = 0;
  lostTrans = 0;
  lostPacket = 0;
  numDumpingBlock = 0;
  numDumpingTxs = 0;
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

void B4Mesh::SetUp(Ptr<Node> node, vector<Ipv4Address> peers, float txMean){

  this->peers = peers;
  this->node = node;
  this->interArrival = txMean;

  //if (recv_sock == 0){
  if (!recv_sock){
    // Open the receiving socket
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    recv_sock = Socket::CreateSocket(node, tid);
  }

  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(),
        80);
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

void B4Mesh::RecurrentSampling(){

  if (running == false){
    return;
  }

  MempoolSampling();
  TxsPerformances();

  Simulator::Schedule(Seconds(SEC_5_TIMER), 
        &B4Mesh::RecurrentSampling, this);
}

void B4Mesh::TestBlockCreation(){
  // If leader have enough transactions in mempool. Then create block.
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

Ipv4Address B4Mesh::GetIpAddress(){
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

int B4Mesh::GetIdFromIp(Ipv4Address ip){
  for(unsigned int i=0; i<=peers.size(); i++){
    if(peers[i] ==  ip){
      return i;
    }
  }
  return -1;
}

Ipv4Address B4Mesh::GetIpAddressFromId(int id){
  Ipv4Address ipv4 = peers[id];
  return ipv4;
}

//  ************* PACKET RELATED METHODS ********************************
int B4Mesh::ExtractMessageType(const string& msg_payload){
  int ret = *((int*)msg_payload.data());
  return ret;
}

void B4Mesh::ReceivePacket(Ptr<Socket> socket){
  Ptr<Packet> packet;
  Address from;
  while ((packet = socket->RecvFrom(from))){
    if (InetSocketAddress::IsMatchingType(from)){
      InetSocketAddress iaddr = InetSocketAddress::ConvertFrom(from);
      Ipv4Address ip = iaddr.GetIpv4();

     // debug_suffix.str("");
      //debug_suffix << " Received Packet : New packet of " << packet->GetSize() << "B from Node " << ip;
      //debug(debug_suffix.str());

      string parsedPacket;
      char* packetInfo = new char[packet->GetSize()];
      packet->CopyData(reinterpret_cast<uint8_t*>(packetInfo),
          packet->GetSize());
      string parsed_packet = string(packetInfo, packet->GetSize());
      delete[] packetInfo;

      try{
        ApplicationPacket p(parsed_packet);
        // For traces propuses
        b4mesh_throughput[Simulator::Now().GetSeconds()] = p.CalculateSize();

        /* ------------ TRANSACTION TREATMENT ------------------ */
        if (p.GetService() == ApplicationPacket::TRANSACTION){
          /*
          debug_suffix.str("");
          debug_suffix << "Received Packet : TRANSACTION from: " <<  GetIdFromIp(ip) << endl;
          debug(debug_suffix.str());
          */
          Transaction t(p.GetPayload());
          // For traces propuses
          TraceTxsRcv(stoi(t.GetHash()),Simulator::Now().GetSeconds());
          // Transaction Processing Delay
          float process_time =GetExecTimeTXtreatment(blockgraph.GetBlocksCount());
          // For traces propuses
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
          // For traces propuses
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
          string req_block = p.GetPayload();
          SendBlockto(req_block, ip);
        }
        /* ------------ CHANGE_TOPO TREATMENT ------------------ */
        else if (p.GetService() == ApplicationPacket::CHANGE_TOPO){
          int message_type = ExtractMessageType(p.GetPayload());
          /* ----------- CHILDLESSBLOCK_REQ TREATMENT ------------*/
          if ( message_type == CHILDLESSBLOCK_REQ){
            // Childless blocks are sent to the leader
            debug_suffix.str("");
            debug_suffix << "Received Packet : CHILDLESSBLOCK_REQ from: " <<  GetIdFromIp(ip) << endl;
            debug(debug_suffix.str());
            SendChildlessBlocks(ip);
          }
          /* ----------- CHILDLESSBLOCK_REP TREATMENT ------------*/
          else if (message_type == CHILDLESSBLOCK_REP){
            // Function executed by the leader.
            debug_suffix.str("");
            debug_suffix << "Received Packet : CHILDLESSBLOCK_REP from: " <<  GetIdFromIp(ip) << endl;
            debug(debug_suffix.str());
            ProcessChildlessResponse(p.GetPayload(), ip);
          }
          /* ----------- GROUPBRANCH_REQ TREATMENT ------------*/
          else if (message_type == GROUPBRANCH_REQ){
          // Sends a branch of the blockgraph to a node
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

void B4Mesh::SendPacket(ApplicationPacket& packet, Ipv4Address ip, bool
    scheduled){

  if (running == false){
    return;
  }

  // if (!scheduled){
  //   float desync = (rand() % 100) / 1000.0;
  //   Simulator::Schedule(Seconds(desync),
  //       &B4Mesh::SendPacket, this, packet, ip, true);
  //   return;
  // }
    // Create the packet to send
  Ptr<Packet> pkt = Create<Packet>((const uint8_t*)(packet.Serialize().data()), packet.GetSize());

  // Open the sending socket
  TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
  Ptr<Socket> source = Socket::CreateSocket (node, tid);
  InetSocketAddress remote = InetSocketAddress (ip, 80);
  source->Connect(remote);

  int res = source->Send(pkt);
  source->Close();

  if (res > 0){
    /*
    debug_suffix.str("");
    debug_suffix << GetIpAddress() << " sends a packet of size " << pkt->GetSize() << " to " << ip << endl;
    debug(debug_suffix.str());
    */
  }
  else{
    
    debug_suffix.str("");
    debug_suffix << GetIpAddress() << " failed to send a packet of size " << pkt->GetSize() << " to " << ip << endl;
    debug(debug_suffix.str());
    
  }
}

void B4Mesh::BroadcastPacket(ApplicationPacket& packet, vector<Ipv4Address> v){
  for (auto& ip : v){
    if(GetIpAddress() != ip)
    SendPacket(packet, ip, false);
  }
}

//  ******* TRANSACTION RELATED METHODS **************** 
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

  double mean = interArrival;
  double bound = 0;
  Ptr<ExponentialRandomVariable> x = CreateObject<ExponentialRandomVariable> ();
  x->SetAttribute("Mean", DoubleValue(mean));
  x->SetAttribute("Bound", DoubleValue(bound));
  double interval = x->GetValue();
  
  Simulator::Schedule(Seconds(interval),
        &B4Mesh::GenerateTransactions, this);

}

void B4Mesh::CourseChange(string context, Ptr<const MobilityModel> mobility){
  Vector pos = mobility->GetPosition();
  Vector vel = mobility->GetVelocity();
  // cout << Simulator::Now().GetSeconds() << " Node: " << node->GetId() <<", model =" << mobility->GetTypeId() << ", POS: x =" << pos.x << ", y =" << pos.y
  //      << ", z =" << pos.z << "; VEL: " << vel.x << ", y =" << vel.y << ", z =" << vel.z << endl;
}

void B4Mesh::RegisterTransaction(string payload){
  Transaction t;
  t.SetPayload(payload);
  t.SetTimestamp(Simulator::Now().GetSeconds());
/*
  debug_suffix.str("");
  debug_suffix << "Creating Transaction : " << t.GetHash().data() << endl;
  debug(debug_suffix.str());
*/
  // Adding to mempool
  TransactionsTreatment(t);

  // Transaction Processing delay
  float process_time = (t.GetSize() / pow(10,3)); // 1 kbytes/s
  p_t_c_t += process_time;

  Simulator::Schedule(MilliSeconds(process_time),
        &B4Mesh::SendTransaction, this, t);
}

void B4Mesh::SendTransaction(Transaction t){
/*  
  debug_suffix.str("");
  debug_suffix << "Sending Transaction : " << t.GetHash().data() << endl;
  debug(debug_suffix.str());
*/  
  string serie = t.Serialize();
  ApplicationPacket packet(ApplicationPacket::TRANSACTION, serie);
  // Populate groupDestination vector
  vector<Ipv4Address> groupDestination = vector<Ipv4Address> ();
  bool trace = false;
  if (stoi(t.GetHash().data()) % 5 == 0 ){
    trace = true;
  }
  for (auto& dest : group){
    groupDestination.push_back(dest.second);
    // For traces propuses
    if (trace){
      TraceTxsSend(dest.first, stoi(t.GetHash()), Simulator::Now().GetSeconds());
    }
  }
  BroadcastPacket(packet, groupDestination);
}

void B4Mesh::TransactionsTreatment(Transaction t){

  if (running == false){
    return;
  }

  if (!IsTxInMempool(t)){
//    debug("Transaction not in mempool. ");
    if (!blockgraph.IsTxInBG(t)){
//      debug("Transaction not in Blockgraph. ");
      if (IsSpaceInMempool()){
//        debug("Adding transaction in mempool... ");
        pending_transactions[t.GetHash()] = t;
        // traces purpose
        pending_transactions_time[t.GetHash()] = Simulator::Now().GetSeconds();
      } else { // No space in mempool.
//        debug("Transaction's Mempool is full\n Dumping transaction...");
        // For traces propuses
        lostTrans++;
    //    TRACE << "MEMPOOL_FULL" << " " << "TXS_LOST" << " " << lostTrans << endl;
      }
    } else { // Transaction already in blockgraph
//        debug("Transaction already present in Blockgraph\n Dumping transaction ...  ");
        numDumpingTxs++;
      //  TRACE << "DUMP_TX" << " " << "TX_IN_BG" << " " << numDumpingTxs << endl;
    }
  } else { // Transaction already in mempool
//      debug("Transaction already present in Memepool\n Dumping transaction ... ");
      numDumpingTxs++;
   //   TRACE << "DUMP_TX" << " " << "TX_IN_MEMPOOL" << " " << numDumpingTxs << endl;
  }

}

void B4Mesh::RetransmitTransactions(){
  // Retransmission of transactions to be sure that old transactinos are register 
  int i = 0;
  if (pending_transactions.size() > 0){
    for (auto mem_i : pending_transactions){
      if (mem_i.second.GetTimestamp() + RETRANSMISSION_TIMER < Simulator::Now().GetSeconds() && i <= 10 ){

        debug_suffix.str("");
        debug_suffix << "Retransmission : ----> Transaction: " << mem_i.second.GetHash().data() << " with Timestamp of: " <<  mem_i.second.GetTimestamp() << endl;
        debug(debug_suffix.str());

        numRTxsG ++;
        SendTransaction(mem_i.second);
      }
      else {
        break;
      }
    }
  }
}

bool B4Mesh::IsTxInMempool (Transaction t){

  if(pending_transactions.count(t.GetHash()) > 0){
    return true;
  }
  else {
    return false;
  }
}

bool B4Mesh::IsSpaceInMempool (){

  if ( SizeMempoolBytes()/1000 < SIZE_MEMPOOL){
    return true;
  }
  else {
    return false;
  }
}

// ****** BLOCK RELATED METHODS *********************
void B4Mesh::BlockTreatment(Block b){
  // Block Treatment
  if (running == false){
    return;
  }

  debug_suffix.str("");
  debug_suffix << " BlockTreatment: Block made by leader: " << b.GetLeader() << " with hash " << b.GetHash().data() << endl;
  debug(debug_suffix.str());
  // Updating the leader node value 
  lastLeader = b.GetLeader();
  // Checking if the block is already in BLOCKGRAPH
  if (!blockgraph.HasBlock(b.GetHash())){
    debug_suffix.str("");
    debug_suffix << " BlockTreatment: This block "<< b.GetHash().data() << " is not in the blockgraph " << endl;
    debug(debug_suffix.str());
    // Check if the block is a missing block
    if (IsBlockInMissingList(b.GetHash())){
      // If block is in missing list we erase it hash from the list
      debug(" BlockTreatment: This block is a missing block");
      EraseMissingBlock(b.GetHash());
    }
    // Check if the block is already in the waiting list
    if (!IsBlockInWaitingList(b.GetHash())){
      // Check if the block is a merge block.
      if (b.IsMergeBlock()){
        debug_suffix.str("");
        debug_suffix << " BlockTreatment: Block "<< b.GetHash().data() << " is a merge block" << endl;
        debug(debug_suffix.str());
       // TRACE << "MERGE_BLOCK" << " " << b.GetHash().data() << endl;
        StartSyncProcedure(b.GetTransactions());
        //SyncNode(b.GetTransactions());
        AddBlockToBlockgraph(b);
      } else {
        // The block is not a merge block
        // Checking that the parents of this block are in the BG:
        Ipv4Address ip = GetIpAddressFromId(b.GetLeader());
        vector<string> unknown_parents = GetParentsNotInBG(b.GetParents());
        if (unknown_parents.size() > 0){
          // One or more parents of this block are not in the BG
          debug(" BlockTreatment: Some of the parents of this block are not in the local BG");
          // Checking that the parents of this block are in the Waiting List:
          unknown_parents = GetParentsNotInWL(unknown_parents);
          if (unknown_parents.size() > 0){
            debug(" BlockTreatment: Some of the parents of this block are not in the Waiting List");
            // adding unknown parents to the list of missing blocks
            UpdateMissingList(unknown_parents, ip);
          }
          // Adding the block to the waiting list since parents aren't in BG yet
          debug_suffix.str("");
          debug_suffix << " BlockTreatment: Adding new block: " << b.GetHash().data() << " to block_waiting_list: " << endl;
          debug(debug_suffix.str());
          //Adding block to the block_waiting_list
          block_waiting_list[b.GetHash()] = b;
          //Trace purpose
          waiting_list_time[b.GetHash()] = Simulator::Now().GetSeconds();
          // TRACE << "WAITING_LIST" << " " << "INSERT" << " " << b.GetHash().data() << endl;
        } else {
          // All ancestors are known by the local node
          // Adding block to blockgraph
          AddBlockToBlockgraph(b);
          // Update the waiting list.
          if (block_waiting_list.size() > 0){
            UpdateWaitingList();
          }
        }
      }
    }
    else {
      // The block is already present in waiting list
      debug_suffix.str("");
      debug_suffix << " BlockTreatment: Block " << b.GetHash() << " already present in waiting list\n  Dumping block..." << endl;
      debug(debug_suffix.str());
      numDumpingBlock++;
     // TRACE << "DUMP_BLOCK" << " " << "BLOCK_IN_WAITING_LIST" << " " << numDumpingBlock << endl;
    }
  } else {
    // The block is already present in the local BLOCKGRAPH
    debug_suffix.str("");
    debug_suffix << " BlockTreatment: Block " << b.GetHash().data() << " already present in blockgraph\n  Dumping block..." << endl;
    debug(debug_suffix.str());
    numDumpingBlock++;
    // TRACE << "DUMP_BLOCK" << " " << "BLOCK_IN_BG" << " " << numDumpingBlock << endl;
  }
}

void B4Mesh::StartSyncProcedure(vector<Transaction> transactions){

  string::size_type n;
  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  vector<string>::iterator it;

  debug("Starting synchronization process...");
  for (auto &tx : transactions){
    string tmp = tx.GetPayload();
    n = tmp.find(" ");
    int idnode = stoi(tmp.substr(0, n));
    string childless_hash = tmp.substr(n+1);
    cout << "toto1" << endl;
    ChildlessBlockTreatment(childless_hash, GetIpAddressFromId(idnode));
    cout << "toto2" << endl;
  }

  if (GetB4MeshOracle(node->GetId())->IsLeader() == true){
    Simulator::ScheduleNow(&B4Mesh::SendBranchRequest, this);
  } else {
    Ptr<UniformRandomVariable> randNum = CreateObject<UniformRandomVariable> ();
    randNum->SetAttribute("Min", DoubleValue(0));
    randNum->SetAttribute("Max", DoubleValue(1));
    Simulator::Schedule(Seconds(randNum->GetValue()), &B4Mesh::SendBranchRequest, this);
  }
}

void B4Mesh::SendBranchRequest(){

  group_branch_hdr_t branch_req;
  branch_req.msg_type = GROUPBRANCH_REQ;
  string serie_branch_b_h = "";

  int i = 0;
  for(auto &cb : missing_childless){
    if (recover_branch.count(cb) > 0){
      auto range = recover_branch.equal_range(cb);
      for (auto j = range.first; j != range.second; ++j){
        serie_branch_b_h = j->first; 
        string serie((char*)&branch_req, sizeof(group_branch_hdr_t));
        serie += serie_branch_b_h;
        debug_suffix.str("");
        debug_suffix << " SendBranchRequest : Asking for this block chain: " << serie_branch_b_h  << " to node: " << j->second << endl;
        debug(debug_suffix.str());
        ApplicationPacket pkt(ApplicationPacket::CHANGE_TOPO, serie);
        SendPacket(pkt, GetIpAddressFromId(j->second), false);
        i++;
        if (GetB4MeshOracle(node->GetId())->IsLeader() == true){
          if (i == ceil(double(peers.size())/(4))){  // number of branch request to different nodes
            goto cntt;
          }
        }
        else {
          if (i == ceil(double(peers.size())/double(8))){  // number of branch request to different nodes
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

vector<string> B4Mesh::GetParentsNotInWL(vector<string> parents){

  vector<string> ret = vector<string>();

  for (auto &p : parents){
    if(block_waiting_list.count(p) > 0){
      debug_suffix.str("");
      debug_suffix << "GetParentsNotInWL: Parent : " << p << " is already in block_waiting_list " << endl;
      debug(debug_suffix.str());
    }
    else {
      debug_suffix.str("");
      debug_suffix << "GetParentsNotInWL: Parent : " << p << " is not in Waiting List " << endl;
      debug(debug_suffix.str());  
      ret.push_back(p);
    }
  }
  return ret;
}

vector<string> B4Mesh::GetParentsNotInBG(vector<string> parents){

  vector<string> ret = vector<string>();
  map<string, Block> bg = blockgraph.GetBlocks();

  for (auto &p : parents){
    if(bg.count(p) > 0){
      debug_suffix.str("");
      debug_suffix << "GetParentsNotInBG: Parent: " << p << " already in BG " << endl;
      debug(debug_suffix.str());
    }
    else {
      debug_suffix.str("");
      debug_suffix << "GetParentsNotInBG Parent: " << p << " is NOT in Blockgraph " << endl;
      debug(debug_suffix.str());
      ret.push_back(p);
    }
  }
  return ret;
}

bool B4Mesh::IsBlockInWaitingList(string b_hash){     

  if (block_waiting_list.count(b_hash) > 0){
    return true;
  } else {
    return false;
  }

}

bool B4Mesh::IsBlockInMissingList(string b_hash){

  if (missing_block_list.count(b_hash) > 0){
    return true;
  }
  else {
    return false;
  }
}

void B4Mesh::EraseMissingBlock(string b_hash){

  if (missing_block_list.count(b_hash) > 0 ){
    // The hash of the missing block exists

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
  }
  else {
    debug("EraseMissingBlock: Not a missing block");
  }
}

void B4Mesh::UpdateMissingList(vector<string> unknown_p, Ipv4Address ip){
  // Updating missing_block_list
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
    }
    else {
      // No entry for this block reference  -> Adding it
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

void B4Mesh::UpdateWaitingList(){

  bool addblock = true;
  while (addblock) {
    addblock = false;
    for (auto it = block_waiting_list.cbegin(); it != block_waiting_list.cend();){
      debug_suffix.str("");
      debug_suffix << "Update waiting list: starting with block: "<< it->first.data() << endl;
      debug(debug_suffix.str());
      int i = 0;
      for(auto &hash : it->second.GetParents() ){
        if (blockgraph.HasBlock(hash)){
          i++;
        }
      }
      if (it->second.GetParents().size() == i){ // if all parents are in blockgraph
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

void B4Mesh::AddBlockToBlockgraph(Block b){

  vector<string>::iterator it;
  bool isMissing = false;

  it = find(missing_childless.begin(), missing_childless.end(), b.GetHash());
  if (it != missing_childless.end()){
      debug_suffix.str("");
      debug_suffix << "AddBlockToBlockgraph: Erasing chidless block: "<< b.GetHash() << " from missing_childless" << endl;
      debug(debug_suffix.str());
      missing_childless.erase(it);
      isMissing = true;
  }

  // trace purpose 
  if (missing_childless.size() == 0 && isMissing == true){
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

void B4Mesh::UpdatingMempool (vector<Transaction> transactions, string b_hash){

  for (auto &t : transactions){
    if (pending_transactions.count(t.GetHash()) > 0){
      debug_suffix.str("");
      debug_suffix << " UpdatingMempool: Transaction " << t.GetHash().data() << " founded" << endl;
      debug(debug_suffix.str());
      // Traces purpose 
      TraceTxLatency(t, b_hash);
      // erasing transaction from mempool
      pending_transactions.erase(t.GetHash());
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
void B4Mesh::Ask4MissingBlocks(){

  if (running == false){
    return;
  }

  // Ask also the leader for missing blocks

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
          cout << "Sending to leader also " << endl;
          SendPacket(packet, GetIpAddressFromId(lastLeader));
        }
      }
    }
  }
  else {
    debug(" Ask4MissingBlocks: No blocks in waiting list");
  }

}

// function executed in response of a REQUEST_BLOCK (Ask4MissingBlocks)
void B4Mesh::SendBlockto(string hash_p, Ipv4Address destAddr){

  if (blockgraph.HasBlock(hash_p)){
    debug_suffix.str("");
    debug_suffix << " SendBlockto: Block " << hash_p << " founded!" << endl;
    debug(debug_suffix.str());

    Block block = blockgraph.GetBlock(hash_p);

    debug_suffix.str("");
    debug_suffix << " SendBlockto: Sending block to node: " << destAddr << endl;
    debug(debug_suffix.str());
    ApplicationPacket packet(ApplicationPacket::BLOCK, block.Serialize());
    SendPacket(packet, destAddr);
    // For traces propuses
    TraceBlockSend(GetIdFromIp(destAddr), Simulator::Now().GetSeconds(), stoi(block.GetHash()));
  }
  else {
    debug_suffix.str("");
    debug_suffix << " SendBlockto: Block " << hash_p << " not founded!" << endl;
    debug(debug_suffix.str());
    return;
  }
}

// ************** CALCUL AND GROUP DETECTION METHODS ************************
void B4Mesh::ReceiveNewTopologyInfo(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natChange){

  debug(" ReceiveNewTopology: New topology information ");

  change++;

  //register last changement
  lastChange = natChange;

  Simulator::Schedule(Seconds(5),
      &B4Mesh::UpdateTopologyInfo, this, change, new_group);
}

void B4Mesh::UpdateTopologyInfo(uint32_t tmp_change, pair<string, vector<pair<int, Ipv4Address>>> new_group){
  
  debug(" UpdateTopologyInfo");

  if (change == tmp_change){
    debug(" UpdateTopologyInfo: Checking if groups id are differents! ");
    if (new_group.first != groupId){
      debug(" UpdateTopologyInfo: Confirming topology change! ");
      vector<pair<int, Ipv4Address>> n_group = new_group.second;
      //Updating groupId
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
      recover_branch.clear();
      startmergetime = Simulator::Now().GetSeconds();    
      startMerge = true;
      }
      else if (lastChange == SPLIT){
        debug(" UpdateTopologyInfo: This is a split change! ");
      }
    } else {
      debug(" UpdateTopologyInfo: Topology is the same! No changes applied! ");
    }
  } else {
    debug(" UpdateTopologyInfo: Topology not stable yet... waiting for last change... ! ");
  }
}

vector <pair<int, Ipv4Address>> B4Mesh::GetNewNodes(){

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

int B4Mesh::GetLastChange() {
  return lastChange;
}

// ********* CHANGE_TOPO METHODS **************
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
    Ask4ChildlessBlocks();
    startMerge = false;

  } else {
    debug("Can't start a merge now, topology not stable yet ");
    Simulator::Schedule(Seconds(1),
            &B4Mesh::StartMerge, this);
  }
  
}

void B4Mesh::Ask4ChildlessBlocks(){

  debug(" Ask4ChildlessBlocks: In leader synch process ");

  childlessblock_req_hdr_t ch_req;
  ch_req.msg_type = CHILDLESSBLOCK_REQ;

  ApplicationPacket packet(ApplicationPacket::CHANGE_TOPO,
      string((char*)&ch_req, sizeof(ch_req)));

  vector<Ipv4Address> groupDestination = vector<Ipv4Address> ();
  // Asking to new nodes for childless blocks
  debug("B4MESH: Ask4ChildlessBlocks: Asking childless blocks to nodes:");
  for (auto &dest : GetNewNodes()){
    debug_suffix.str("");
    debug_suffix << " B4MESH: Ask4ChildlessBlocks: Node: " << dest.first << endl;
    debug(debug_suffix.str());
    groupDestination.push_back(dest.second);
  }
  BroadcastPacket(packet, groupDestination);

  Simulator::Schedule(Seconds(6), 
            &B4Mesh::timer_childless_fct, this);
}

void B4Mesh::timer_childless_fct(){

  if (!createBlock){
    debug("Timer_childless has expired. Creating a merge block");
    mergeBlock = true;
    createBlock = true;
    GenerateBlocks();
  }
}

void B4Mesh::SendChildlessBlocks(Ipv4Address destAddr){
  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  string serie_hashes = "";

  for (auto &cb : myChildless){
    string tmp_str = cb;
    debug_suffix.str("");
    debug_suffix << " SendChildlessBlocks: Childless block founded : " << tmp_str;
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
    debug(" SendChildlessBlocks: No childless block founded!");
  }
}

void B4Mesh::ProcessChildlessResponse(const string& msg_payload, Ipv4Address senderAddr){
  // Only executed by the leader
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
    ChildlessBlockTreatment(cb, senderAddr);
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

void B4Mesh::ChildlessBlockTreatment(string childless_hash, Ipv4Address senderAddr){

  vector<string> myChildless = blockgraph.GetChildlessBlockList();
  vector<string>::iterator it;

  debug("Childless block treatment! ");

  if (find(myChildless.begin(), myChildless.end(), childless_hash) != myChildless.end()){
    debug_suffix.str("");
    debug_suffix << " ChildlessblockTreatment : This Childless block: "  << childless_hash << " is also a childless block of mine" << endl;
    debug(debug_suffix.str());
    RegisterNode(childless_hash, senderAddr);  
    return;
  } else {
    // If childless block is not a childless block of mine
    debug_suffix.str("");
    debug_suffix << " ChildlessblockTreatment : This Childless block: "  << childless_hash << " is not a childless block of mine" << endl;
    debug(debug_suffix.str());
    if (blockgraph.HasBlock(childless_hash)){
      debug(" ChildlessBlockTreatment: Childless block is present in local blockgraph...Ignoring childless block");
      return;
    } else {
      debug(" ChildlessBlockTreatment: Childless block is not present in blockgraph...Checking if CB is present in the waiting list");
      if (IsBlockInWaitingList(childless_hash)){
        debug(" ChildlessBlockTreatment: Childless block is already present in the block_waiting_list...Ignoring childless block");
        return;
      } else {
        debug(" ChildlessBlockTreatment: Childless block is not present in waiting list...Checking if CB is present in missing_block_list");
        if (IsBlockInMissingList(childless_hash)){
          debug(" ChildlessBlockTreatment: Childless block already in missing_block_list. Ignoring childless block");
          RegisterNode(childless_hash, senderAddr); 
          return;
        } else {
          debug(" ChildlessBlockTreatment: Block not in missing_block_list. Adding block to missing_block_list ");
          // Adding childless block hash in the list of missing block since we now know of it existance
          missing_block_list.insert({childless_hash, senderAddr}); 
          // Trace purpose
          missing_list_time[childless_hash] = Simulator::Now().GetSeconds();
          // TRACE << "MISSING_LIST" << " " << "INSERT" << " " << childless_hash.data() << endl;
          it = find(missing_childless.begin(), missing_childless.end(), childless_hash);
          if (it == missing_childless.end()){
            //If this childless block is not registered adding missing childless block to the list of childless block to recover
            debug(" ChildlessBlockTreatment: Adding Childless block to vector missing_chidless");
            missing_childless.push_back(childless_hash);
          }
          RegisterNode(childless_hash, senderAddr);
        }
      }
    }
  }

}

void B4Mesh::RegisterNode(string childless_hash, Ipv4Address senderAddr){

  // Register node answer
  debug(" Register node answer");

  int idSender = GetIdFromIp(senderAddr);
  bool findnode = false;
  
  if (recover_branch.count(childless_hash) > 0 ){
    // If the childless block has already been registered 
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
      debug_suffix << " RegisterNode : Node: "  << idSender << " already register with same childless block: " << childless_hash << endl;
      debug(debug_suffix.str());
    }
  }
  else {
    // The chidless block have not been registered yet
    debug_suffix.str("");
    debug_suffix << " RegisterNode : Adding new childless block : "  << childless_hash << " for node: " << idSender << endl;
    debug(debug_suffix.str());
    recover_branch.insert(pair<string, int> (childless_hash, idSender));
  }
}

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

void B4Mesh::SendBranch4Sync(const string& msg_payload, Ipv4Address destAddr){
  string deserie = msg_payload;
  string block_hash = "";
  string gp_id = "";
  vector<string> block_hash_v = vector<string> ();
  vector<string> gp_id_v = vector<string> ();

  deserie = deserie.substr(sizeof(group_branch_hdr_t), deserie.size());

  // Extracting the block hashes in payload and putting them in vector block_hash_v
  while ( deserie.size() > 0){
    block_hash = deserie.substr(0, 32);
    if(find(block_hash_v.begin(), block_hash_v.end(), block_hash) != block_hash_v.end()){
    } else {
      block_hash_v.push_back(block_hash);
    }
    deserie = deserie.substr(32, deserie.size());
  }

  /** for every block in block_hash_v
   *  getting the groupId of each block and filling the vector gp_id_v
   *  This vector has the GroupId of the blocks in the request.
   */
  for (auto &b_h : block_hash_v){
    gp_id = blockgraph.GetGroupId(b_h);
    if (find(gp_id_v.begin(), gp_id_v.end(), gp_id) != gp_id_v.end()){
      //GroupId already existent "
    } else {
      gp_id_v.push_back(gp_id);
    }
  }

  // Sending blocks with the same groupId to node
  for (auto g_id : gp_id_v){
    vector<Block> blocks_group = blockgraph.GetBlocksFromGroup(g_id);
    for (auto b_grp : blocks_group){
      debug_suffix.str("");
      debug_suffix << " SendBranch4Sync: Sending block hash " << b_grp.GetHash() << " with groupId: " << g_id;
      debug_suffix << " to node " << destAddr << endl;
      debug(debug_suffix.str());
      ApplicationPacket packet(ApplicationPacket::BLOCK, b_grp.Serialize());
      SendPacket(packet, destAddr);
      // For trace propuses
      TraceBlockSend(GetIdFromIp(destAddr), Simulator::Now().GetSeconds(), stoi(b_grp.GetHash()));
    }
  }
}

// ************** GENERATION DES BLOCKS *****************************
bool B4Mesh::TestPendingTxs(){
// Only leader execute this function

  if ((int)Simulator::Now().GetSeconds() - lastBlock_creation_time > TIME_BTW_BLOCK){
    if ( SizeMempoolBytes()/1000 > MIN_SIZE_BLOCK){
      debug(" TestPendingTxs: Enough txs to create a block.");
      return true;
    }
    else {
      debug(" Not enough transactions to form a block");
      return false;
    }
  }
  else {
    debug(" TestPendingTxs: Not allow to create a block so fast");
    return false;
  }
}

//Select the transactions with a small timestamp.
vector<Transaction> B4Mesh::SelectTransactions(){

  vector<Transaction> transactions;
  

  while((transactions.size()*TX_MEAN_SIZE)/1000 < MAX_SIZE_BLOCK){
    pair<string, double> min_ts = make_pair("0", 9999999);
    for(auto &t : pending_transactions){
      if(find(transactions.begin(), transactions.end(), t.second) != transactions.end()){
        continue;
      }
      else {
        if (t.second.GetTimestamp() < min_ts.second){
          min_ts = make_pair(t.first, t.second.GetTimestamp());
        } else {
          continue;
        }
      }
    }
    if (min_ts.first != "0"){
      debug_suffix.str("");
      debug_suffix << "SelectTransactions : Getting tx: " << min_ts.first << " with time stamp: " << min_ts.second << endl;
      debug(debug_suffix.str());
      transactions.push_back(pending_transactions[min_ts.first]);
    } else {
      break;
    }
  }
  return transactions;
}

bool B4Mesh::IsMergeInProcess(){

  debug(" IsMergeInProcess: List of missing childless: ");
  for (auto &mc : missing_childless){
    debug_suffix.str("");
    debug_suffix << " Childless: " << mc << endl;
    debug(debug_suffix.str());
  }

  if (missing_childless.size() > 0){
    // There is a merge synchronization in process
    return true;
  } else {
    return false;
  }
}

void B4Mesh::GenerateBlocks(){

  debug(" GenerateBlock: Creating a Block ");

  Block block;

  block.SetLeader(node->GetId());
  block.SetGroupId(groupId);

  if (mergeBlock){
      // If block is a merge block
      debug(" GenerateBlock: Generating a Merge block due to merge");
      block = GenerateMergeBlock(block);
  } else {
    if (blockgraph.GetChildlessBlockList().size() > 1){
      // If block has more than one parent and mergeBlock flag is false
      if (IsMergeInProcess()){
        debug(" GenerateBlock: Generating a Regular block because a merge is already in process");
        block = GenerateRegularBlock(block);
      } else {
        debug(" GenerateBlock: Generating a Merge block not beacause of a merge process");
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

   // FOR TRACES PROPUSES
  BlockCreationRate(block.GetHash(), block.GetGroupId(), block.GetTxsCount(), block.GetTimestamp());

  float process_time = GetExecTime(block.GetTxsCount());
  p_b_c_t += process_time;

  Simulator::Schedule(MilliSeconds(process_time),
        &B4Mesh::SendBlockToConsensus, this, block);
  
}

Block B4Mesh::GenerateMergeBlock(Block &block){

  debug(" GenerateMergeBlock: Creating a Merge Block ");

  vector<Transaction> transactions = vector<Transaction> ();
  vector<string> p_block = vector <string> ();

  for (auto &parent : blockgraph.GetChildlessBlockList() ){
    p_block.push_back(parent);
  }

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

  // Adding the new parents
  if(missing_childless.size() > 0){
    for (auto &pb : missing_childless){
      if(find(p_block.begin(), p_block.end(), pb) == p_block.end()){
        p_block.push_back(pb);
      }
    }
  }

  block.SetParents(p_block);
  block.SetBlockType(1);  // new

  mergeBlock = false;

  return block;

}

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
          debug_suffix << " GenerateRegularBlock : The childless block: " << p << " is a block that comes from another branch and its branch is in revcover process";
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
       pending_transactions.erase(t.GetHash());
  }
  
  return block;

}

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

// **************** CONSENSUS INTERFACES METHODS *****************
void B4Mesh::RecvBlock(void * b4, Block b) {

  // debug_suffix.str("");
  // debug_suffix << "Received a block from consensus module and RecvBlock with hash" << b.GetHash().data() <<endl;
  // debug(debug_suffix.str());

    // cout << " Node: " << node->GetId() << "Received a block from consensus module and RecvBlock with hash" << b.GetHash().data() <<endl;

  Ptr<B4Mesh> ptr_b = (B4Mesh *) b4;
  ptr_b->GetBlock(b);

}

void B4Mesh::GetBlock(Block b) {

    debug_suffix.str("");
    debug_suffix << "Received a new block : " << b << " from "  << " with hash " << b.GetHash().data() << endl;
    debug(debug_suffix.str());
    // For traces propuses
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

void B4Mesh::MempoolSampling(){

  double time = Simulator::Now().GetSeconds();
  auto mempool = pair<pair<double, int>, pair<int, float>> ();
  mempool = make_pair(make_pair(time, (int)pending_transactions.size()),
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
  for (auto &t : pending_transactions){
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
  output_file6.close();


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
  output_file10 << "B4Mesh: Number of remaining transactions in the mempool: " << pending_transactions.size() << endl;   
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
 