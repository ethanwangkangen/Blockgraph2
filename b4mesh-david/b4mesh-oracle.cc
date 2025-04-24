#include "b4mesh-oracle.h"

NS_LOG_COMPONENT_DEFINE("B4MeshOracle");
NS_OBJECT_ENSURE_REGISTERED(B4MeshOracle);

TypeId B4MeshOracle::GetTypeId(){
  static TypeId tid = TypeId("B4MeshOracle")
    .SetParent<Application>()
    .SetGroupName("Application")
    .AddConstructor<B4MeshOracle>()
    ;
  return tid;
}

vector<void *> B4MeshOracle::nodesB4mesh;     // b4mesh references
double B4MeshOracle::sendServiceTime;       // milliseconds
double B4MeshOracle::leaderElectionDelay;   // seconds
double B4MeshOracle::blockCommitDelay;      // milliseconds


B4MeshOracle::B4MeshOracle(){
  current_state = FOLLOWER;
  current_leader = -1;
  running = false;
  term = 0;

  // Service Times
  sendServiceTime = 100;      // milliseconds
  leaderElectionDelay = 2;   // seconds
  blockCommitDelay = 50;      // milliseconds

  // Probabilities
  blockRecvProbability = 1.0;
  blockCommitProbability = 1.0;

  SetCoin();

}

B4MeshOracle::~B4MeshOracle(){
}

void B4MeshOracle::SetUp(Ptr<Node> node, vector<Ipv4Address> peers){
  this->peers = peers;
  this->node = node;

  // Build initial current group
  for (uint32_t i=0; i<ns3::NodeList::GetNNodes(); ++i)
    current_group.push_back(make_pair(i, GetIpAddress(i)));
}

Ipv4Address B4MeshOracle::GetIpAddress(int i){
  Ptr<Ipv4> ipv4 = ns3::NodeList::GetNode(i)->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

void B4MeshOracle::StartApplication(){
  running = true;
}

void B4MeshOracle::StopApplication(){
  running = false;
}


int B4MeshOracle::GetCurrentLeader(){
  return current_leader;
}

int B4MeshOracle::GetCurrentState(){
  return current_state;
}

void B4MeshOracle::SetCurrentState(int st) {
  current_state = st;
}


bool B4MeshOracle::IsLeader(){
  return current_state == LEADER;
}


void B4MeshOracle::ChangeGroup(pair<string, vector<pair<int, Ipv4Address>>> new_group, int natchange){

  // Upon receiving a groupe change:
  // Decide whether to run a leader election

  if (new_group.second.size() == 1) {
    current_group = new_group.second;
    current_leader = node->GetId();
    current_state = LEADER;
    return;
  }

  // If it is a split
  if (natchange == SPLIT) {

    //Set the new group as the current group
    current_group = new_group.second;

    // if the node is the leader, it does trigger a leader election
    if (!IsLeader()) {
      if (GetCurrentLeader() != -1) {  // if there is a leader
        if (!IsInCurrentGroup(GetIpAddress(GetCurrentLeader()))) {
          StartElection();
        }
      } else {
        StartElection();
      }
    }
  } else if (natchange == MERGE || natchange == ARBITRARY) {
    current_group = new_group.second;
    StartElection();
  }

  SetMajority();
  traces->ResetStartConfigChange();
  traces->StartConfigChange(Simulator::Now().GetSeconds());

}

void B4MeshOracle::StartElection() {

  term++;
  current_leader = -1;
  if (current_state == LEADER)
    current_state = FOLLOWER;

    // Calculate election delay:
    //    Sending request for votes delay +
    //    Receiving votes delays
  Simulator::Schedule(Seconds(leaderElectionDelay),
      &B4MeshOracle::EndElection, this, term);
}

void B4MeshOracle::EndElection(uint32_t t_elect){

/* Disable
  double minSize = 0;                   // pick a random leader
  double maxSize = current_group.size();
  Ptr<UniformRandomVariable> randomLeader = CreateObject<UniformRandomVariable> ();
  randomLeader->SetAttribute("Min", DoubleValue(minSize));
  randomLeader->SetAttribute("Max", DoubleValue(maxSize));
  int pos = trunc(randomLeader->GetValue());
*/
  if (term == t_elect) {
    unsigned int winner = current_group.at(0).first;
    current_leader = winner;     // the first node of the group is always the leader
    if (node->GetId() == winner) {
      SetCurrentState(LEADER);
    }

    // Indicate b4mesh graph that we have a leaders
    indicateNewLeader(nodesB4mesh.at(node->GetId()));
  }
}

bool B4MeshOracle::IsInCurrentGroup(Ipv4Address ip){

  for (auto g : current_group)
    if (g.second == ip) return true;
  return false;
}

int B4MeshOracle::GetNodeId(Ipv4Address ip){
  for (uint32_t i=0; i<peers.size(); ++i)
    if (peers[i] == ip) return i;
  return 0;
}


Ptr<B4MeshOracle> B4MeshOracle::GetB4MeshOracleOf(int nodeId){
  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(0);
  Ptr<B4MeshOracle> consensus = app->GetObject<B4MeshOracle>();
  return consensus;
}


void B4MeshOracle::SetCoin() {

  double minSize = 0;
  double maxSize = 1;
  coin = CreateObject<UniformRandomVariable> ();
  coin->SetAttribute("Min", DoubleValue(minSize));
  coin->SetAttribute("Max", DoubleValue(maxSize));

}

void B4MeshOracle::SetMyB4Mesh(void * b4) {

  nodesB4mesh.push_back(b4);

}

void B4MeshOracle:: SetBlockRecvProbability(double b_rcv) {

  blockRecvProbability = b_rcv;

}

void B4MeshOracle::SetblockCommitProbability(double b_com) {

  blockCommitProbability = b_com;

}

void * B4MeshOracle::GetMyB4Mesh(int nid) {

  return nodesB4mesh.at(nid);
}

void B4MeshOracle::InsertEntry(Block& b, int term){

  Simulator::Schedule(MilliSeconds(sendServiceTime),
        &B4MeshOracle::SendBlock, this, b);

}

void B4MeshOracle::SetSendBlock(void func(void *, Block)){

  sendBlock = func;
}

void B4MeshOracle::SendBlock(Block b){

  // With probability p_blockRcv, node i receives the block
  vector<bool> success_recv = vector<bool>(current_group.size(), false);
  int no_recvs = 1;


// Deciding who is going to receive
// Leader always receives and commit the block.
  int leader = b.GetLeader();
  success_recv[leader] = true;

  for (uint32_t i = 0; i < current_group.size(); i++) {
    if (coin->GetValue() <= blockRecvProbability) {
      success_recv[i] = true;
      no_recvs++;
      //cout << " Node: " << i << " success of recv is: " << success_recv[i] << endl;
     }
  }

  if (no_recvs >= GetMajority()) {      // If the majority has received the block
    int count = 0;
    for(auto n : current_group) {
      if (success_recv[count]) {
        GetB4MeshOracleOf(n.first)->RecvBlock(b);
      }
      count++;
    }
  }

}

void B4MeshOracle::RecvBlock(Block b) {

  // Receives a block and schedules its commitment

  Simulator::Schedule(MilliSeconds(blockCommitDelay),
                  &B4MeshOracle::IndicateBlockCommit, this, b);
}

void B4MeshOracle::SetIndicateNewBlock(void func(void *, Block)) {

  indicateNewBlock = func;
}

void B4MeshOracle::SetIndicateNewLeader(void func(void *)) {

  indicateNewLeader = func;
}



void B4MeshOracle::IndicateBlockCommit(Block b){

  sendBlock(nodesB4mesh.at(node->GetId()), b);             // Calling recvBlock in B4Mesh

}


void B4MeshOracle::SetMajority() {

  majority = current_group.size()/2;

}

float B4MeshOracle::GetMajority() {

  return majority;

}
