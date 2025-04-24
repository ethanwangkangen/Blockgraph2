#ifndef B4MESH_MOBILITY_H
#define B4MESH_MOBILITY_H

#include "ns3/application.h"
#include "ns3/core-module.h"
#include "ns3/string.h"
#include "ns3/node-list.h"
#include "ns3/log.h"
#include "ns3/mobility-model.h"
#include "ns3/simulator.h"
#include "ns3/node-container.h"
#include "ns3/olsr-routing-protocol.h"

#include <cmath>
#include <math.h>
#include <vector>

#include "utils.h"
#include "b4m_traces.h"
#include "b4mesh-raft.h"


using namespace ns3;
using namespace std;

class B4MeshMobility : public Application{

  public:
    static TypeId GetTypeId(void);

  public:
    /**
     * Setup the application
     */
  //  void SetUp(Ptr<Node> node);
    void SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int nScen, int sTime);

    /**
     * Method called at time specified by Start.
     */
    virtual void StartApplication();

    /**
     * Method called at time specified by Stop.
     */
    virtual void StopApplication();

    /*
     * Update position of the node if it is a leader and its follower.
     */
    void UpdatePos();

    /*
     * Update the position of the leader.
     */
    Vector UpdateLeaderPos();

    /*
     * Implement rebound behavior of leader when they reach the limits of the
     * area;
     */
    void UpdateDirection();

    /*
     * This function is part of the Group Management system
     * It reacts to topology changes and notify other modules
     * of the changes in the network topology
     */
    void TableChange(uint32_t size);

    /*
     * This function create a hash from a list of nodes
     * given. It is the potential groupId
     */
    string CalculeGroupHash(vector<pair<int,Ipv4Address>> grp);

    /**
     * Returns false if the difference between nodes in group
     * and groupCandidate is 2 or more. retuens true if less or equal than 1
     */
    bool CalculDiffBtwGroups(vector<pair<int, Ipv4Address>> candidategroup);

    void UpdateFollowersEvenOdd(Vector leader_pos);

    void UpdateFollowersConsecutive(Vector leader_pos);

    void UpdateFollowersScen3(Vector leader_pos);

    /*
     * This is a pointer to the Consensus protocol
     */
    Ptr<B4MeshRaft> GetB4MeshRaft(int nodeId);

    /*
     * Get the local Ip adresse
     */
    Ipv4Address GetIpAddress();

    void debug(string suffix);

  public:
    /*
     * Constructors and Destructors
     */
    B4MeshMobility();
    ~B4MeshMobility();

  private:
    bool running;
    Ptr<Node> node;
    int leaderIdMod;  // Identifiant pour determiner les leaders de groupes
                      // mobiles.
    double time_change; //last time that a change in the network topology has happened
    int moveInterval; // Periodicity at which nodes move.
    int speed;        // Speed at which nodes move.
    vector<int> bounds; // Bounds of the space in which nodes moves (inf_x,
                        // inf_y, sup_x, sup_y)
    Vector direction; // Initial direction of the node (random)
    int duration;
    double intervalChange;
    bool changeTopo;
    bool switchNode;
    int scenario;
    int numNodes;

    string groupHash;
    vector<pair<int, Ipv4Address>> group;   // Nodes belonging to the same group
    vector<Ipv4Address> peers;  // Ip address of all nodes of the system
    stringstream debug_suffix;
};
#endif
