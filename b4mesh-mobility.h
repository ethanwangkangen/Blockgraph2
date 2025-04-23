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
#include "b4mesh-oracle.h"
#include "b4mesh.h"

using namespace ns3;
using namespace std;

class B4MeshMobility : public Application{

  public:
    static TypeId GetTypeId(void);
    enum{NONE, SPLIT, MERGE, ARBITRARY};
    enum{cPosition=1, rWalk2, cVelosity};

  public:
    /**
     * Setup the application
     */
    //void SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int sTime, int nScen);
    void SetUp(Ptr<Node> node, vector<Ipv4Address> peers, int sTime, int nScen, int mMob, double speed);

    /**
     * Method called at time specified by Start.
     */
    virtual void StartApplication();

    /**
     * Method called at time specified by Stop.
     */
    virtual void StopApplication();

    /**
     * This is a pointer to the Oracle Consensus protocol of node i
     */
    Ptr<B4MeshOracle> GetB4MeshOracle(int nodeId);

    /*
     * This is a pointer to the blockgraph protocol of node i
     */
    Ptr<B4Mesh> GetB4MeshOf(int nodeId);

    /*
     * Get the local Ip adresse
     */
    Ipv4Address GetIpAddress();

    /*
     * It is the recurrent function that updates the possition of a node.
     */
    void UpdatePos();

    /*
     * Update the position of the leader according to the mobility scenario.
     */
    Vector UpdateLeaderPos();

    /*
     * Implement rebound behavior of leader when they reach the limits of the
     * area;
     */
    Vector UpdateDirection();

    /*
     *  Update the followers position with respect of the leader possition for scenario 1
     */
    void UpdateFollowersScn1(Vector leader_pos);

    /*
     *  Update the followers position with respect of the leader possition for scenario 2 
     */
    void UpdateFollowersScn2(Vector leader_pos);

    /*
     *  Update the followers position with respect of the leader possition for scenario 3 and 4
     */
    void UpdateFollowersScn3(Vector leader_pos);

  public:
    // Functions related to the Group Management System
    /*
     * This function is call by NS3 trace system everytime a change in the OLSR routing table is dectected
     * It checks if the new candidate groupe is different from the current group
     */
    void TableChange(uint32_t size);

    /**
     * This fucntion decides whether to apply a change inmediatly or to wait for a more stable topology
     */ 
    void CheckGroupChangement(vector<pair<int, Ipv4Address>> groupCandidate);

    /**
     * Apply a change in the network topology
     */
    void ChangeGroup(vector<pair<int, Ipv4Address>> groupCandidate);

    /*
     * Calculates the new group id of the new group
     */
    string CalculeGroupId(vector<pair<int,Ipv4Address>> grp);

    /**
     * Return the type of group change related to the current configuration
     */
    int DetecteNatureChange(vector<pair<int, Ipv4Address>> oldgroup, vector<pair<int, Ipv4Address>> newgroup );

    /**
     * Returns false if the difference between nodes in group
     * and groupCandidate is 2 or more. retuens true if less or equal than 1
     */
    bool CalculDiffBtwGroups(vector<pair<int, Ipv4Address>> candidategroup);

    /* Comment this function to unable COUT DEBUG prints */
    void debug(string suffix);

    void ConstantPositionModel();

    void ConstantVelocityModel(); 

    void RandomWalk2Model();

    void CourseChange(string context, Ptr<const MobilityModel> mobility);


  public:
    /*
     * Constructors and Destructors
     */
    B4MeshMobility();
    ~B4MeshMobility();

  private:
    bool running;
    Ptr<Node> node;
    int numNodes;       // Number of nodes in the simulation
    int duration;       // Time of simulation
    int scenario;       // Scenario being simulated
    int mMob;
    double speed;

    int leaderIdMod;    // Identifier to determine the mobility leaders (x%numNodes == 0)
    int onethird;       // Identifier of the mobility leader when using scenarion 3 or 4
    int twothird;       // Identifier of the mobility leader when using scenarion 3 or 4
                      
    double time_change; // last time that a change in the network topology has happened
    string groupId;     // the GroupId
    int moveInterval;   // Periodicity at which nodes' position is updated.
    //int speed;          // Speed at which nodes move.
    vector<int> bounds; // Bounds of the space in which nodes moves (inf_x,
                        // inf_y, sup_x, sup_y)
    int direct;         // Direction of the node. If direct > 0 -> direction goes in positive X's && If Direct < 0 -> direction goes towards negative X's 
    vector<pair<int, Ipv4Address>> group;   // Nodes belonging to the same group
    vector<Ipv4Address> peers;              // Ip address of all nodes of the system
    stringstream debug_suffix;
};
#endif
