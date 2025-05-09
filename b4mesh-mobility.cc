#include "b4mesh-mobility.h"

NS_LOG_COMPONENT_DEFINE("B4MeshMobility");
NS_OBJECT_ENSURE_REGISTERED(B4MeshMobility);

TypeId B4MeshMobility::GetTypeId(void){

  static TypeId tid = TypeId("B4MeshMobility")
    .SetParent<Application>()
    .SetGroupName("Application")
    .AddConstructor<B4MeshMobility>();
  return tid;
}

B4MeshMobility::B4MeshMobility(){
  running = false;

  leaderIdMod = -1;
  onethird = -1;
  twothird = -1;
  moveInterval = 5;
  time_change = 0;
  groupId = string(32,0);
  group = vector<pair<int, Ipv4Address>> ();
  peers = vector<Ipv4Address>();
  direct = 1;

}

B4MeshMobility::~B4MeshMobility(){
}

void B4MeshMobility::SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int sTime, int nScen, int mMob, double speed){
  this->peers = peers;
  this->node = node;
  this->duration = sTime;
  this->numNodes = peers.size();
  this->scenario = nScen;
  this->mMob = mMob;
  this->speed = speed;

  debug_suffix.str("");
  debug_suffix << "Mobility Model : " << this->mMob << " Simulation time: " << this->duration << " Node's speed: " << this->speed << endl;
  debug(debug_suffix.str());

  if (this->mMob == cPosition){
    // Set up for Constant Position Mobility Model
    if (scenario == 1){
      this->leaderIdMod = numNodes;
      this->bounds = vector<int>({-200,-200, 200, 200,});
    } else if (scenario == 2){
      this->leaderIdMod = ceil((float)numNodes/(float)2);
      this->bounds = vector<int>({-250,-250, 250, 250,});
    } else if (scenario == 3){
      this->leaderIdMod = ceil((float)numNodes/(float)3);
      this->onethird = ceil((float)numNodes/(float)3);
      this->twothird = onethird*2;
      if (numNodes > 25){
        this->bounds = vector<int>({-350,-350, 350, 350,});
      } else {
        this->bounds = vector<int>({-300,-300, 300, 300,});
      }
    } else if (scenario == 4){
      this->leaderIdMod = ceil((float)numNodes/(float)3);
      this->onethird = ceil((float)numNodes/(float)3);
      this->twothird = onethird*2;
      if (numNodes > 25){
        this->bounds = vector<int>({-350,-350, 350, 350,});
      } else {
        this->bounds = vector<int>({-300,-300, 300, 300,});
      }
    }

    debug_suffix.str("");
    debug_suffix << " Mobility Scenario: " << scenario << " Speed: " << speed << " Update possition interval : " << moveInterval << endl;
    debug(debug_suffix.str());

  } else if (this->mMob == rWalk2){
    // Set up for random walk mobility model
    this->scenario = -1;
  } else if (this->mMob == cVelosity){
    // # No parametters defined for this mobility model YET.
  }

 /*
  * Detects changes in the olsr routing table and executes function TableChange
  */
  Ptr<ns3::olsr::RoutingProtocol> olsrOb = node->GetObject<ns3::olsr::RoutingProtocol>();
  olsrOb->TraceConnectWithoutContext ("RoutingTableChanged",
                                      MakeCallback (&B4MeshMobility::TableChange, this));

  Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange",
                    MakeCallback (&B4MeshMobility::CourseChange, this));
  }


Ptr<B4MeshOracle> B4MeshMobility::GetB4MeshOracle(int nodeId){

  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(0);
  Ptr<B4MeshOracle> oracle = app->GetObject<B4MeshOracle>();
  return oracle;

}

Ptr<B4Mesh> B4MeshMobility::GetB4MeshOf(int nodeId){

  Ptr<Application> app = ns3::NodeList::GetNode(nodeId)->GetApplication(1);
  Ptr<B4Mesh> b4mesh = app->GetObject<B4Mesh>();
  return b4mesh;

}

/**
 * Starts the application.
 * Only constant position mobility model is supported now.
 */
void B4MeshMobility::StartApplication(){

  debug(" Start Mobility Application ");
  running = true;

  if (mMob == cPosition){
    // Execute constant position mobility model
    Simulator::ScheduleNow(&B4MeshMobility::ConstantPositionModel, this);
  } else  if (mMob == cVelosity){
    // Execute constant velocity mobility model
    Simulator::ScheduleNow(&B4MeshMobility::ConstantVelocityModel, this);
  } else if (mMob ==rWalk2){
    // Execute Random Walk2 Mobility model
    Simulator::ScheduleNow(&B4MeshMobility::RandomWalk2Model, this);
  }

}

void B4MeshMobility::StopApplication(){
  debug(" Stoping Mobility Application ");
  running = false;
}

Ipv4Address B4MeshMobility::GetIpAddress(){
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  return ipv4->GetAddress(1, 0).GetLocal();
}

void B4MeshMobility::CourseChange(string context, Ptr<const MobilityModel> mobility){
  Vector pos = mobility->GetPosition();
  Vector vel = mobility->GetVelocity();
  // cout << Simulator::Now().GetSeconds() << " Node: " << node->GetId() <<", model =" << mobility->GetTypeId() << ", POS: x =" << pos.x << ", y =" << pos.y
  //      << ", z =" << pos.z << "; VEL: " << vel.x << ", y =" << vel.y << ", z =" << vel.z << endl;
}
// ************ Constant Position Model ****************

/**
 * Called by setUp(). Calls UpdatePos();
 */
void B4MeshMobility::ConstantPositionModel(){
  cout << " #### Executing Constant Position model ####" << endl;
  UpdatePos();
}


void B4MeshMobility::ConstantVelocityModel(){
  cout << " #### Executing Constant Velocity model ####" << endl;
}


void B4MeshMobility::RandomWalk2Model(){
  cout << " #### Executing Random Walk2 model ####" << endl;
}

// ********** Group Management Functions ***************

/**
 * Calls itself every moveInterval.
 * If node is a MOBILITY leader (not the same as BG/Consensus leader),
 * get leader vector from UpdateLeaderPos().
 * Then passes this vector to UpdateFollowersScn<X>().
 */ 
void B4MeshMobility::UpdatePos(){
  if (!running) return;

  // Update mobility leaders position
  // For scenario 3, leaderIdMod = numNodes/3
  // eg. 15 nodes, leaderIdMod is 5.
  // so leader nodes will be 0, 5, 10.
  if (node->GetId() % leaderIdMod == 0){
      Vector leader_pos;
      leader_pos = UpdateLeaderPos();
      leader_pos = UpdateDirection();

    // Update mobility followers' position
    if (scenario == 1 ){
      UpdateFollowersScn1(leader_pos);
    }
    else if (scenario == 2){
      UpdateFollowersScn2(leader_pos);
    }
    else if (scenario == 3 || scenario == 4){
      UpdateFollowersScn3(leader_pos);
    }
  }
  Simulator::Schedule(Seconds(moveInterval), &B4MeshMobility::UpdatePos, this);
}

/**
 * Called by UpdatePos() to update and retrieve a vector for mobility leader to use.
 * Based on scenario and time, 
 */
Vector B4MeshMobility::UpdateLeaderPos(){
    Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
    Vector current_pos = mob->GetPosition();

    if (scenario == 1 ){
      // MOBILITY SCENARIO 1:  ALL NODES TOGETHER (----------)
      debug("Executing Scenario 1");
      
      if (Simulator::Now().GetSeconds() < duration ){
        current_pos.y = 0;
        if (direct > 0){
          current_pos.x = current_pos.x + speed;
        } else {
          current_pos.x = current_pos.x - speed;
        }
      }
    }
    if (scenario == 2){
      //  MOBILITY MODEL 2 : SPLIT FOLLOWED BY A MERGE (1-2-1)
      debug("Executing Scenario 2");
      
      if (Simulator::Now().GetSeconds() < (duration/3)-((duration/3)/2) ){
        debug("****** 1/3 *******");
        
	      current_pos.y = 0;
        if (direct > 0){
          current_pos.x = current_pos.x + speed;
        } else {
          current_pos.x = current_pos.x - speed;
        }
      } else if (Simulator::Now().GetSeconds() > (duration/3)-((duration/3)/2) 
		      && Simulator::Now().GetSeconds() < (duration/3)*2-((duration/3)/2) ){
        debug("****** 2/3 *******");

	if (node->GetId() == 0 && direct > 0 ){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y < bounds[3]/2){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[3]/2;
          }     
        } else if (node->GetId() == 0 && direct < 0 ){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y < bounds[3]/2){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[3]/2;
          }     
        } else if (node->GetId() == (unsigned int)leaderIdMod && direct > 0){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        } else if (node->GetId() == (unsigned int)leaderIdMod && direct < 0){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y > bounds[1]/2){    
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
      } else if (Simulator::Now().GetSeconds() > (duration/3)*2-((duration/3)/2) ){
       debug("****** 3/3 *******");
       
       if (node->GetId() == 0 && direct > 0){
         current_pos.x = current_pos.x + speed;
         if (current_pos.y > 0){
           current_pos.y = current_pos.y - speed;
         } else {
           current_pos.y = 0;
         }
       } 
       if (node->GetId() == 0 && direct < 0){
         current_pos.x = current_pos.x - speed;
         if (current_pos.y > 0){
           current_pos.y = current_pos.y - speed;
         } else {
           current_pos.y = 0;
         }
       }
        if (node->GetId() == (unsigned int)leaderIdMod && direct > 0){
          current_pos.x =  current_pos.x + speed;
          if (current_pos.y < 0){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = 0;
          }
        }
        if (node->GetId() == (unsigned int)leaderIdMod && direct < 0){
          current_pos.x =  current_pos.x - speed;
          if (current_pos.y < 0){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = 0;
          }
        }
      }
    }
    if (scenario == 3){
      debug("Executing Scenario 3");

      if (Simulator::Now().GetSeconds() < (duration/13) ){
        debug("****** 1/3 *******");
        current_pos.y = 0;
        if (direct > 0){
          current_pos.x += speed;
        } else {
          current_pos.x -= speed;
        }
      }
      else if (Simulator::Now().GetSeconds() > (duration/13) && Simulator::Now().GetSeconds() < (duration/2) ){
        debug("****** 2/3 *******");

        if (node->GetId() == 0 && direct > 0 ){
          current_pos.x += speed;
          current_pos.y = 0;
        }
        else if (node->GetId() == 0 && direct < 0 ){
          current_pos.x -= speed;
          current_pos.y = 0;
        }
        else if (node->GetId() == (unsigned int)onethird && direct > 0){
          current_pos.x += speed;
          if (current_pos.y < bounds[3]/2){
            current_pos.y += speed;
          } else {
            current_pos.y = bounds[3]/2;
          }
        }
        else if (node->GetId() == (unsigned int)onethird && direct < 0){
          current_pos.x -= speed;
          if (current_pos.y < bounds[3]/2){
            current_pos.y += speed;
          } else {
            current_pos.y = bounds[3]/2;
          }
        }  
        else if(node->GetId() == (unsigned int)twothird && direct > 0 ){
          current_pos.x += speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y -= speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
        else if(node->GetId() == (unsigned int)twothird && direct < 0 ){
          current_pos.x -= speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y -= speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
      }
      else if (Simulator::Now().GetSeconds() > (duration/2) ){
        debug("****** 3/3 *******");
        if (node->GetId() == 0 && direct > 0 ){
          current_pos.x += speed;
          current_pos.y = 0;
        }
        else if (node->GetId() == 0 && direct < 0 ){
          current_pos.x -= speed;
          current_pos.y = 0;
        }
        else if (node->GetId() == (unsigned int)onethird && direct > 0){
          current_pos.x += speed;
          if (current_pos.y > 0){
            current_pos.y -= speed;
          } else {
            current_pos.y = 0;
          }
        }
        else if (node->GetId() == (unsigned int)onethird && direct < 0){
          current_pos.x -= speed;
          if (current_pos.y > 0){
            current_pos.y -= speed;
          } else {
            current_pos.y = 0;
          }
        } 
        else if(node->GetId() == (unsigned int)twothird && direct > 0 ){
          current_pos.x += speed;
          if (current_pos.y < 0){
            current_pos.y += speed;
          } else {
            current_pos.y = 0;
          }
        }
        else if(node->GetId() == (unsigned int)twothird && direct < 0 ){
          current_pos.x -= speed;
          if (current_pos.y < 0){
            current_pos.y += speed;
          } else {
            current_pos.y = 0;
          }
        } 
      }
    }
    if (scenario == 4){
      //  MOBILITY MODEL 4 : TWO SPLITS FOLLOWED BY TWO MERGE (1-2-3-2-1)
      debug("Executing Scenario 4");
      if (Simulator::Now().GetSeconds() < (duration/6)-((duration/6)/2)){
        debug("****** 1/5 *******");
        current_pos.y = 0;
        if (direct > 0){
          current_pos.x = current_pos.x + speed;
        } else {
          current_pos.x = current_pos.x - speed;
        }
      }
      else if (Simulator::Now().GetSeconds() > (duration/6)-((duration/6)/2) && Simulator::Now().GetSeconds() < ((duration/6)*2)-((duration/6)/4) ){
        debug("****** 2/5 *******");
        if (node->GetId() == (unsigned int)twothird && direct > 0 ){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y < bounds[3]/2 ){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[3]/2;
          } 
        }
        else if (node->GetId() == (unsigned int)twothird && direct < 0 ){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y < bounds[3]/2 ){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[3]/2;
          }
        }
        else if ( (node->GetId() == 0 || node->GetId() == (unsigned int)onethird) && direct > 0){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
        else if ( (node->GetId() == 0 || node->GetId() == (unsigned int)onethird) && direct < 0 ){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
      }
      else if (Simulator::Now().GetSeconds() > ((duration/6)*2)-((duration/6)/4) && Simulator::Now().GetSeconds() < ((duration/6)*3)-((duration/6)/4)){
        debug("****** 3/5 *******");
        if (node->GetId() == (unsigned int)twothird && direct > 0 ){
          current_pos.x = current_pos.x + speed;
          current_pos.y = bounds[3]/2;
        }
        else if (node->GetId() == (unsigned int)twothird && direct < 0){
          current_pos.x = current_pos.x - speed;
          current_pos.y = bounds[3]/2;
        }
        else if ( node->GetId() == (unsigned int)onethird && direct > 0 ){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y > (bounds[1]/2)+(bounds[1]/4) ){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = (bounds[1]/2)+(bounds[1]/4);
          }
        }
        else if ( node->GetId() == (unsigned int)onethird && direct < 0 ){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y > (bounds[1]/2)+(bounds[1]/4) ){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = (bounds[1]/2)+(bounds[1]/4);
          }
        }
        else if (node->GetId() == 0 && direct > 0){
           current_pos.x = current_pos.x + speed;
           if (current_pos.y < (bounds[1]/2)-(bounds[1]/4)){
             current_pos.y = current_pos.y + speed;
           } else {
             current_pos.y = (bounds[1]/2)-(bounds[1]/4);
           }
        }
        else if (node->GetId() == 0 && direct < 0){
           current_pos.x = current_pos.x - speed;
           if (current_pos.y < (bounds[1]/2)-(bounds[1]/4)){
             current_pos.y = current_pos.y + speed;
           } else {
             current_pos.y = (bounds[1]/2)-(bounds[1]/4);
           }
        }
      }
      else if (Simulator::Now().GetSeconds() > ((duration/6)*3)-((duration/6)/4) && Simulator::Now().GetSeconds() < ((duration/6)*4)-((duration/6)/4) ){
        debug("****** 4/5 *******");
        if (node->GetId() == (unsigned int)twothird && direct > 0 ){
          current_pos.x = current_pos.x + speed;
          current_pos.y = bounds[3]/2;
        }
        else if (node->GetId() == (unsigned int)twothird && direct < 0){
          current_pos.x = current_pos.x - speed;
          current_pos.y = bounds[3]/2;
        }
        else if (node->GetId() == (unsigned int)onethird && direct > 0){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y < bounds[1]/2){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
        else if (node->GetId() == (unsigned int)onethird && direct < 0){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y < bounds[1]/2){
            current_pos.y = current_pos.y + speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
        else if (node->GetId() == 0 && direct > 0){
          current_pos.x = current_pos.x + speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
         else if (node->GetId() == 0 && direct < 0){
          current_pos.x = current_pos.x - speed;
          if (current_pos.y > bounds[1]/2){
            current_pos.y = current_pos.y - speed;
          } else {
            current_pos.y = bounds[1]/2;
          }
        }
      }
      else if ( Simulator::Now().GetSeconds() > ((duration/6)*4)-((duration/6)/4) && Simulator::Now().GetSeconds() < duration){
        debug("****** 5/5 *******");
        if (node->GetId() == (unsigned int)twothird && direct > 0){
          current_pos.x += speed;
          if (current_pos.y > 0){
            current_pos.y -= speed;
          } else {
            current_pos.y = 0;
          }
        }
        else if (node->GetId() == (unsigned int)twothird && direct < 0){
          current_pos.x -= speed;
          if (current_pos.y > 0){
            current_pos.y -= speed;
          } else {
            current_pos.y = 0;
          }
        }
        else if ( (node->GetId() == 0 || node->GetId() == (unsigned int)onethird) && direct > 0 ){
          current_pos.x += speed;
          if (current_pos.y < 0){
            current_pos.y += speed;
          } else {
            current_pos.y = 0;
          }
        }
         else if ( (node->GetId() == 0 || node->GetId() == (unsigned int)onethird) && direct < 0 ){
          current_pos.x -= speed;
          if (current_pos.y < 0){
            current_pos.y += speed;
          } else {
            current_pos.y = 0;
          }
        }
      }
    }
  mob->SetPosition(current_pos);
  return current_pos;
}

Vector B4MeshMobility::UpdateDirection(){
    Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
    Vector current_pos = mob->GetPosition();

    bool pos_changed = false;

    debug_suffix.str("");
    debug_suffix << " Update direction. Updated position in X: " << current_pos.x << 
	    " updated position in Y: " << current_pos.y << endl;
    debug(debug_suffix.str());

    if (current_pos.x < bounds[0]){
      debug(" reaching the lower X limit of the area");
      direct = -direct;
      current_pos.x = bounds[0]+10;
      pos_changed = true;
    }
    else if (current_pos.x > bounds[2]){
      debug(" reaching the highrt X limit of the area");
      direct = -direct;
      current_pos.x = bounds[2]-10;
      pos_changed = true;
    }
    else if (current_pos.y < bounds[1]){
      debug(" reaching the lower Y limit of the area");
      direct = -direct;
      current_pos.y = bounds[1]+10;
      pos_changed = true;
    }
    else if (current_pos.y > bounds[3]){
      debug(" reaching the higher Y limit of the area");
      direct = -direct;
      current_pos.y = bounds[3]-10;
      pos_changed = true;
    }

    if (pos_changed){
      debug(" Changing position of node");
      mob->SetPosition(current_pos);
    }
    return current_pos;
}


void B4MeshMobility::UpdateFollowersScn1(Vector leader_pos){

  for (int i=1; i<numNodes; ++i){
    Ptr<Node> follower = ns3::NodeList::GetNode(i);
    Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
    Vector current_pos = leader_pos;
    current_pos.x;
    Vector new_position;
    new_position.z = current_pos.z;
    new_position.x = current_pos.x;
    new_position.y = current_pos.y;
    debug_suffix.str("");
    debug_suffix << " Updating follower's " << i << " possition. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
    debug(debug_suffix.str());
    mob->SetPosition(new_position);
  }
}

void B4MeshMobility::UpdateFollowersScn2(Vector leader_pos){

  if (node->GetId() == 0){
    for (int i=1; i<(leaderIdMod); ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      //current_pos.x += 2;
      Vector new_position;
      new_position.z = current_pos.z;
      new_position.x = current_pos.x;
      new_position.y = current_pos.y;
      debug_suffix.str("");
      debug_suffix << " Updating follower's " << i << " possition. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
      debug(debug_suffix.str());
      mob->SetPosition(new_position);
    }
  }
  if(node->GetId() == (unsigned int)leaderIdMod){
    for (int i=leaderIdMod+1; i<numNodes; ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      //current_pos.x += 2;
      Vector new_position;
      new_position.z = current_pos.z;
      new_position.x = current_pos.x;
      new_position.y = current_pos.y;
      debug_suffix.str("");
      debug_suffix << " Updating follower's " << i << " possition. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
      debug(debug_suffix.str());
      mob->SetPosition(new_position);
    }
  }
}

void B4MeshMobility::UpdateFollowersScn3(Vector leader_pos){

  if (node->GetId() == 0){ // This is the first leader.
    for (int i=1; i < onethird; ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      //current_pos.x += 2;
      Vector new_position;
      new_position.z = current_pos.z;
      new_position.x = current_pos.x; // Used to add + i 
      new_position.y = current_pos.y; // Used to add + i
      debug_suffix.str("");
      debug_suffix << " Updating follower " << i << "'s position. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
      debug(debug_suffix.str());
      mob->SetPosition(new_position);
    }
  }
  if(node->GetId() == (unsigned int)onethird){ // This is the second leader.
    for (int i=onethird+1; i < twothird; ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      //current_pos.x += 2;
      Vector new_position;
      new_position.z = current_pos.z;
      new_position.x = current_pos.x; // Used to add + i 
      new_position.y = current_pos.y; // Used to add + i 
      debug_suffix.str("");
      debug_suffix << " Updating follower " << i << "'s position. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
      debug(debug_suffix.str());
      mob->SetPosition(new_position);
    }
  }
  if(node->GetId() == (unsigned int)twothird){ // This is the third leader.
    for (int i=twothird+1; i < numNodes; ++i){
      Ptr<Node> follower = ns3::NodeList::GetNode(i);
      Ptr<MobilityModel> mob = follower->GetObject<MobilityModel>();
      Vector current_pos = leader_pos;
      //current_pos.x += 2;
      Vector new_position;
      new_position.z = current_pos.z;
      new_position.x = current_pos.x; // Used to add + i 
      new_position.y = current_pos.y; // Used to add + i 
      debug_suffix.str("");
      debug_suffix << " Updating follower " << i << "'s position. New pos in X is : " << new_position.x << " New pos in Y is :" << new_position.y << endl;
      debug(debug_suffix.str());
      mob->SetPosition(new_position);
    }
  }
}


/**
 *
 */
void B4MeshMobility::TableChange(uint32_t size){
  if (running == false){
    return;
  }

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
  } else {
    CheckGroupChangement(groupCandidate);
  }

}

void B4MeshMobility::CheckGroupChangement(vector<pair<int, Ipv4Address>> groupCandidate){
  /* toleranceTime is a defined number that represent the seconds passed since the last
   * change in the network topology
   */
  if(Simulator::Now().GetSeconds() - time_change < TOPOLOGY_TOLERANCE_TIME ){
    // Check if the change is considerable before notifying
    debug(" CheckGroupChangement: The network topopolgy has changed again.\nChecking if it is a considerable change...");
    bool do_change = CalculDiffBtwGroups(groupCandidate);
    if (do_change){
      ChangeGroup(groupCandidate);
    } else {
      // Not enough difference btw groups to say that is a true change
      debug(" CheckGroupChangement: The difference between groupes was less than one element. Ignoring this change. ");
      return;
    }
  } else {
    ChangeGroup(groupCandidate);
  }
}

void B4MeshMobility::ChangeGroup(vector<pair<int, Ipv4Address>> groupCandidate){

  debug(" ChangeGroup: Change in the network topology detected ");
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

  // Updating groupId
  groupId  = CalculeGroupId(groupCandidate);
  //Detect the nature of the changement in the topology
  int natchange = DetecteNatureChange(group, groupCandidate); 
  // Updating group
  group.clear();
  group = groupCandidate;
  // Notify consensus of the new groupId and new group
  // GetB4MeshOracle(node->GetId())->ChangeGroup(make_pair(groupId, group), natchange);
  GetB4MeshOracle(node->GetId())->ChangeGroup(make_pair(groupId, group)); // Changed because of new ChangeGroup() inside Oracle 
  debug(" Notifying the consensus module");
  // Notify the blockchain protocol of the new groupId, new group and nature of change
  GetB4MeshOf(node->GetId())->ReceiveNewTopologyInfo(make_pair(groupId, group), natchange); 
  debug(" Notifying the blockgraph module");
  time_change = (int)Simulator::Now().GetSeconds();

}

bool B4MeshMobility::CalculDiffBtwGroups(vector<pair<int, Ipv4Address>> grp_cand){

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

string B4MeshMobility::CalculeGroupId (vector<pair<int,Ipv4Address>> grp){

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

  for (uint32_t k = 0; k < ret.length(); k++){
    hash = hash + int(ret[k]);
  }

  newHash = to_string(hash % numeric_limits<int>::max());
  newHash = newHash + string(32 - newHash.size(), 0);
  return newHash;
}

int B4MeshMobility::DetecteNatureChange(vector<pair<int, Ipv4Address>> oldgroup, vector<pair<int, Ipv4Address>> newgroup){

  if (newgroup == oldgroup) return NONE;

  int common_count = 0;
  for (auto n : newgroup){
    if (find(oldgroup.begin(), oldgroup.end(), n) !=
        oldgroup.end()) common_count++;
  }

  if ((uint32_t)common_count == newgroup.size()) return SPLIT;
  if ((uint32_t)common_count == oldgroup.size()) return MERGE;
  else return ARBITRARY;
}

void B4MeshMobility::debug(string suffix){
  
  cout << Simulator::Now().GetSeconds() << "s: B4MeshMobility : Node " << node->GetId() <<
      " : " << suffix << endl;
  debug_suffix.str("");
  
}
