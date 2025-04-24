#include "b4mesh-mobility.h"
#define EVENODDG 0
#define NUM_INTERVAL 6
#define TOLERANCE_TIME 2

NS_LOG_COMPONENT_DEFINE("B4MeshMobility");
NS_OBJECT_ENSURE_REGISTERED(B4MeshMobility);


TypeId B4MeshMobility::GetTypeId(void){
  static TypeId tid = TypeId("B4MeshMobility")
    .SetParent<Application>()
    .SetGroupName("Application")
    .AddConstructor<B4MeshMobility>()
    ;
  return tid;

}

B4MeshMobility::B4MeshMobility(){

  running = false;
  changeTopo = false;
  switchNode = false;
  moveInterval = 1;
  speed = 3;
  bounds = vector<int>({-200,-200, 200, 200,});
  groupHash = string(32,0);
  group = vector<pair<int, Ipv4Address>> ();
  peers = vector<Ipv4Address>();

  Vector translation;
  translation.x = uniform_rand(-100,100);
  translation.y = uniform_rand(-100,100);
  translation.z = 0;

  Vector zero;
  double norm = CalculateDistance(zero, translation);
  translation.x = (translation.x / norm) * speed;
  translation.y = (translation.y / norm) * speed;
  direction = translation;

}

B4MeshMobility::~B4MeshMobility(){
}

void B4MeshMobility::SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int nScen, int sTime){
  this->peers = peers;
  this->node = node;
  this->scenario = nScen;
  this->numNodes = peers.size();
  this->leaderIdMod = peers.size()/2;
  this->duration = sTime;
  this->intervalChange = duration/NUM_INTERVAL;


  Ptr<ns3::olsr::RoutingProtocol> olsrOb = node->GetObject<ns3::olsr::RoutingProtocol>();
  olsrOb->TraceConnectWithoutContext ("RoutingTableChanged",
                                      MakeCallback (&B4MeshMobility::TableChange, this));

}

void B4MeshMobility::StartApplication(){
  cout << "Start mobility on node " << node->GetId() << endl;
  running = true;
  UpdatePos();
}

void B4MeshMobility::StopApplication(){
  running = false;
}

Ipv4Address B4MeshMobility::GetIpAddress(){
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

void B4MeshMobility::TableChange(uint32_t size){
  if (running == false){
    return;
  }

  bool flag = false;
  string candidatehash = "";
  vector<ns3::olsr::RoutingTableEntry> entries = vector<ns3::olsr::RoutingTableEntry> ();
  Ptr<ns3::olsr::RoutingProtocol> olsrOb = node->GetObject<ns3::olsr::RoutingProtocol>();
  entries = olsrOb->GetRoutingTableEntries();

  // Creat the candidate group
  vector<pair<int, Ipv4Address>> groupCandidate;
  groupCandidate.clear();
  // Adding ourself to the candidate group
  groupCandidate.push_back(make_pair(node->GetId(), GetIpAddress()));

  // Populating Group table
  for (auto& t : entries){
    if (t.destAddr != GetIpAddress()) {
      for (unsigned int i = 0; i < peers.size(); ++i){
        if(t.destAddr == peers[i]){
          groupCandidate.push_back(make_pair(i, t.destAddr));
        }
      }
    }
  }

  sort(groupCandidate.begin(), groupCandidate.end());

  if (groupCandidate == group){
    return;
  }

    /* toleranceTime is an arbitarry number that represent the seconds passed since the last
   * change in the network topology
   */
  if(Simulator::Now().GetSeconds() - time_change < TOLERANCE_TIME ){
    // Check if the change is considerable before notifying
    flag = true;
  }

  //
  if (flag == true){
    debug(" The network topopolgy has changed again. Checking if it is a considerable change...");
    bool do_change = CalculDiffBtwGroups(groupCandidate);
    if( do_change == false){
      // Not enough difference btw groups to say that is a true change
      debug(" The difference between groupes was less than one element. Ignoring this change. ");
      return;
    } else {
      debug(" The topology change was indeed a considerable change. Prociding with the change. ");
    }
  }
  // Ici vrai changement de topology
  debug(" TableChange: Change in topology detected ");

  debug_suffix << "Old topology : ( ";
  for (auto n : group)
    debug_suffix << n.first << ", ";
  debug_suffix << ")";
  debug(debug_suffix.str());

  debug_suffix << "New topology : ( ";
  for (auto n : groupCandidate)
    debug_suffix << n.first << ", ";
  debug_suffix << ")";
  debug(debug_suffix.str());


  // Calculating groupId from candidate table
  candidatehash = CalculeGroupHash(groupCandidate);

  // Updating groupId
  groupHash = candidatehash;

  // Updating group
  group.clear();
  group = groupCandidate;

  // Notify consensus of the new groupId and new group
  GetB4MeshRaft(node->GetId())->ChangeGroup(make_pair(groupHash, group));

  time_change = Simulator::Now().GetSeconds();
}

bool B4MeshMobility::CalculDiffBtwGroups(vector<pair<int, Ipv4Address>> grp_cand){
  debug(" CalculDiffBtwGroups function");
  unsigned int i = 0;
  unsigned int lim = 2;

  /*
   * It is needed a difference of 2 nodes btw group and candidategroup
   * in order to consider a change of topology.
   */
  int diff = group.size() - grp_cand.size();
  if (abs(diff) < 2) {
    for (auto &n : grp_cand){
      if(find(group.begin(), group.end(), n) == group.end()){
        i++;
      }
    }
    if (grp_cand.size() < group.size())
      lim = 1;
    if (i < lim) {
      // not enough difference. Not concidering this change of ReceiveNewTopology
      debug(" CalculDiffBtwGroups: Not enough distincts elements ");
      return false;
    }
  }
  debug(" CalculDiffBtwGroups: Enough distincts elements to call it a change. ");
  return true;

}

string B4MeshMobility::CalculeGroupHash (vector<pair<int,Ipv4Address>> grp){

  string ret = "";
  int hash = 0;
  string iptostring = string(4,0);
  string newHash = "";
  vector<pair<int, Ipv4Address>> order = grp;

  sort(order.begin(), order.end());

  for (auto g : order){
    g.second.Serialize((uint8_t *)iptostring.data());
    ret = ret + to_string(g.first) + iptostring;
  }

  for (unsigned int k = 0; k < ret.length(); k++){
    hash = hash + int(ret[k]);
  }

  newHash = to_string(hash % numeric_limits<int>::max());
  newHash = newHash + string(32 - newHash.size(), 0);
  return newHash;
}

void B4MeshMobility::UpdateFollowersConsecutive(Vector leader_pos){
  if (node->GetId() == 0){
    for (int i=1; i<(numNodes/2); ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      //cout << "Getting follower: " <<  i << ". This is leader node: " << node->GetId() << endl;
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      current_pos.x += 10;
      Vector new_position;
      new_position.z = current_pos.z;
      float tmp_i = i;
      float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
      new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
        sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
      new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
        cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
      mob->SetPosition(new_position);
    }
  }
  unsigned int half = numNodes/2;
  if (node->GetId() == (half)){
    for (int i=(numNodes/2)+1; i<numNodes; ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      //cout << "Getting follower: " <<  i << ". This is leader node: " << node->GetId() << endl;
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      current_pos.x += 10;
      Vector new_position;
      new_position.z = current_pos.z;
      float tmp_i = i;
      float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
      new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
        sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
      new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
        cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
      mob->SetPosition(new_position);
    }
  }
}

void B4MeshMobility::UpdateFollowersEvenOdd(Vector leader_pos){
  // update mobility followers position creating groups according to NodeId parity
  // Even nodes are gather together and Odd nodes are gathered together.

  for (unsigned int i=0; i< (unsigned int)numNodes; i++){
    if (node->GetId() % 2 == 0){
      cout << "leader EVEN: " << endl;
      if (i % 2 == 0){
        if (i != node->GetId()){
          Ptr<Node> follower = ns3::NodeList::GetNode(i);
          cout << "Getting node: " << i << " as a follower "<< endl;
          Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
          Vector current_pos = leader_pos;
          current_pos.x += 10;
          Vector new_position;
          new_position.z = current_pos.z;
          float tmp_i = i;
          float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
          new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
            sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
          new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
            cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
          mob->SetPosition(new_position);
        }
      }
    } else if (node->GetId() % 2 == 1){
      cout << "leader ODD: " << endl;
      if (i % 2 == 1){
        if (i != node->GetId()){
          Ptr<Node> follower = ns3::NodeList::GetNode(i);
          cout << "Getting node: " << i << " as a follower "<< endl;
          Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
          Vector current_pos = leader_pos;
          current_pos.x += 10;
          Vector new_position;
          new_position.z = current_pos.z;
          float tmp_i = i;
          float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
          new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
            sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
          new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
            cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
          mob->SetPosition(new_position);
        }
      }
    }
  }
}

void B4MeshMobility::UpdateFollowersScen3(Vector leader_pos){
  // make it general to all scenarios.
  unsigned int half = numNodes/2;
  if (node->GetId() == 0){
    if(switchNode){
      for (int i=1; i< (numNodes/2); ++i){
        // Excluding node 1
        if(i != 1){
          Ptr<Node> follower = ns3::NodeList::GetNode(i);
          Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
          Vector current_pos = leader_pos;
          current_pos.x += 10;
          Vector new_position;
          new_position.z = current_pos.z;
          float tmp_i = i;
          float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
          new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
            sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
          new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
            cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
          mob->SetPosition(new_position);
        }
      }
        // Add node from other group
        Ptr<Node> follower = ns3::NodeList::GetNode((numNodes/2)+1);
        Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
        Vector current_pos = leader_pos;
        current_pos.x += 10;
        Vector new_position;
        new_position.z = current_pos.z;
        float angle = (2*M_PI) * (6/(leaderIdMod-1));
        new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
          sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
        new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
          cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
        mob->SetPosition(new_position);

    } else {
      UpdateFollowersConsecutive(leader_pos);

    }

  } else if (node->GetId() == half){
      if (switchNode){
        for (int i=(numNodes/2)+1; i < numNodes; ++i){
          if(i != (numNodes/2)+1){
            Ptr<Node> follower = ns3::NodeList::GetNode(i);
            Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
            Vector current_pos = leader_pos;
            current_pos.x += 10;
            Vector new_position;
            new_position.z = current_pos.z;
            float tmp_i = i;
            float angle = (2*M_PI) * (tmp_i/(leaderIdMod-1));
            new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
              sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
            new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
              cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
            mob->SetPosition(new_position);
          }
        }
        // Add node 1
        Ptr<Node> follower = ns3::NodeList::GetNode(1);
        Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
        Vector current_pos = leader_pos;
        current_pos.x += 10;
        Vector new_position;
        new_position.z = current_pos.z;
        float angle = (2*M_PI) * (1/(leaderIdMod-1));
        new_position.x = cos(angle) * (current_pos.x - leader_pos.x) +
          sin(angle) * (current_pos.y - leader_pos.y) + leader_pos.x;
        new_position.y = sin(angle) * (current_pos.x - leader_pos.x) -
          cos(angle) * (current_pos.y - leader_pos.y) + leader_pos.y;
        mob->SetPosition(new_position);

      } else {
        UpdateFollowersConsecutive(leader_pos);
      }
  }
}

void B4MeshMobility::UpdatePos(){
  if (!running) return;
  // Update mobility leaders position
  Vector leader_pos;

  if (node->GetId() % leaderIdMod == 0){
      leader_pos = UpdateLeaderPos();
      UpdateDirection();

     if (scenario == 1 || scenario == 2){
        // Scenario Split/Merge
#ifdef EVENODDG
          if (EVENODDG == 1){
            UpdateFollowersEvenOdd(leader_pos);

          } else {
            UpdateFollowersConsecutive(leader_pos);
          }
#else
            UpdateFollowersConsecutive(leader_pos);
#endif

} else if (scenario == 3){
          UpdateFollowersScen3(leader_pos);
      }
  }
  Simulator::Schedule(Seconds(moveInterval), &B4MeshMobility::UpdatePos, this);
}

void B4MeshMobility::UpdateDirection(){
    Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();

    Vector current_pos = mob->GetPosition();
    bool pos_changed = false;
    if (current_pos.x < bounds[0] || current_pos.x > bounds[2]){
      cout << Simulator::Now().GetSeconds() << "s: B4MeshMobility : Node "
        << node->GetId() << " reached the x limit of the area" << endl;
      direction.x = - direction.x;

      if (current_pos.x < bounds[0]) current_pos.x = bounds[0];
      else current_pos.x = bounds[2];
      pos_changed = true;
    }

    if (current_pos.y < bounds[1] || current_pos.y > bounds[3]){
      cout << Simulator::Now().GetSeconds() << "s: B4MeshMobility : Node "
        << node->GetId() << " reached the y limit of the area" << endl;
      direction.y = - direction.y;

      if (current_pos.y < bounds[1]) current_pos.y = bounds[1];
      else current_pos.y = bounds[3];
      pos_changed = true;
    }
    if (pos_changed)
      mob->SetPosition(current_pos);
}

Vector B4MeshMobility::UpdateLeaderPos(){

  Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
  Vector current_pos = mob->GetPosition();
  unsigned int half = numNodes/2;
  if (scenario == 1){
    if (Simulator::Now().GetSeconds() < duration ){
      current_pos.x = 10*(node->GetId() / leaderIdMod) ;
      current_pos.y = 0;
    }
  } else if (scenario == 2){
    if((int)Simulator::Now().GetSeconds() % (int)intervalChange == 0 ){
      if(changeTopo == true){
      //  cout << " This is a merge at: " << Simulator::Now().GetSeconds() << " by node: " <<  node->GetId() << endl;
        current_pos.x = 10*(node->GetId() / leaderIdMod) ;
        current_pos.y = 0;
        changeTopo = false;
      } else {
        if (node->GetId() == 0 ){
      //    cout << " This is a split at: " << Simulator::Now().GetSeconds() << " by node: " <<  node->GetId() << endl;
          current_pos.x = -150;
          current_pos.y = 100;
        }
        if(node->GetId() == half ){
          cout << " This is a split at: " << Simulator::Now().GetSeconds() << " by node: " <<  node->GetId() << endl;
          current_pos.x = 150;
          current_pos.y = -100;
        }
        changeTopo = true;
      }
    }
  } else if (scenario == 3){
    if (Simulator::Now().GetSeconds() < 10){
      current_pos.x = 10*(node->GetId() / leaderIdMod) ;
      current_pos.y = 0;
    } else if (Simulator::Now().GetSeconds() > 10 && Simulator::Now().GetSeconds() < duration){
      if(node->GetId() == 0){
        current_pos.x = -150;
        current_pos.y = 100;
        if((int)Simulator::Now().GetSeconds() % (int)intervalChange == 0){
          if (switchNode){
            switchNode = false;
          } else {
            switchNode = true;
          }
        }
      }
      if (node->GetId() == half){
        current_pos.x = 150;
        current_pos.y = -100;
        if((int)Simulator::Now().GetSeconds() % (int)intervalChange == 0){
          if(switchNode){
            switchNode = false;
          } else {
            switchNode = true;
          }
        }
      }
    }
  } else {
    if (Simulator::Now().GetSeconds() < duration ){
      current_pos.x = 10*(node->GetId() / leaderIdMod) ;
      current_pos.y = 0;
    }
  }
mob->SetPosition(current_pos);
return current_pos;
}

Ptr<B4MeshRaft> B4MeshMobility::GetB4MeshRaft(int nodeId){
  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(0);
  Ptr<B4MeshRaft> consensus = app->GetObject<B4MeshRaft>();
  return consensus;
}

void B4MeshMobility::debug(string suffix){
/*  cout << Simulator::Now().GetSeconds() << "s: B4MeshMobility : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
  */
}
