#ifndef B4MESH_RAFT_HELPER_H
#define B4MESH_RAFT_HELPER_H

#include "ns3/node.h"
#include "ns3/object-factory.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"

#include "b4m_traces.h"
#include "b4mesh-raft.h"
#include <vector>

using namespace std;
using namespace ns3;

class B4MeshRaftHelper{
  public:
    B4MeshRaftHelper();
    B4MeshRaftHelper(vector<B4MTraces> *t);
    ~B4MeshRaftHelper();

    ApplicationContainer Install(NodeContainer c, int hbeat, int eTimeout); 

  private:
    ObjectFactory factory;
    vector<B4MTraces>* traces;
};

#endif
