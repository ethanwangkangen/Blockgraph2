#ifndef B4M_TRACES_H
#define B4M_TRACES_H

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>

using namespace std;
class B4MTraces{
  public:
    B4MTraces();
  public:

    // Register election start times and delay
    void StartElection(float timestamp, uint32_t leader);
    void EndElection(float timestamp, uint32_t leader);
    void ResetStartElection();

    void StartConfigChange(float timestamp, uint32_t leader);
    void EndConfigChange(float timestamp, uint32_t leader);
    void ResetStartConfigChange();



    // Register sent and received bytes/messages
    void ReceivedBytes(pair<float, int> new_value);
    void SentBytes(pair<float, int> new_value);
    void ReceivedMessages(pair<float, int> new_value);
    void SentMessages(pair<float, int> new_value);
    void DroppedMessages(pair<float, int> new_value);

  public:

    /*
     * Print a summary of all traces registered.
     */
    string PrintSummary();

    /*
     * Print a summary of raft specific traces.
     */
    string PrintRaftSummary();


  public:

    /*
     * The keys of maps are the timestamps and the values are data traced at
     * that timestamp.
     */
    // Network traces
    map<float, int> received_bytes;
    map<float, int> sent_bytes;
    map<float, int> received_messages;
    map<float, int> sent_messages;
    map<float, int> dropped_messages;

    // Raft specific traces
    map<float, float> election_delay;
    float start_election; // initialize to -1
    map<float, float> config_change_delay;
    float start_config_change;


};
#endif
