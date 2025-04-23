#include "b4mesh-mobility-helper.h"

B4MeshMobilityHelper::B4MeshMobilityHelper(){
  factory.SetTypeId("B4MeshMobility");
}
B4MeshMobilityHelper::~B4MeshMobilityHelper(){
}

ApplicationContainer B4MeshMobilityHelper::Install(NodeContainer c, int sTime, int nScen, int mMob, double speed){
  ApplicationContainer apps;

  vector<Ipv4Address> peers;
  for (uint32_t i=0; i<c.GetN(); ++i){
    Ptr<Ipv4> ipv4 = c.Get(i)->GetObject<Ipv4>();
    peers.push_back(ipv4->GetAddress(1, 0).GetLocal());
  }

  // Get list of ip addresses
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i){
    cout << "Install B4MeshMobility on node : " << (*i)->GetId() << endl;
    Ptr<B4MeshMobility> app = factory.Create<B4MeshMobility>();
    app->SetUp(*i, peers, sTime, nScen, mMob, speed);
    (*i)->AddApplication(app);
    apps.Add(app);
  }
  return apps;
}
