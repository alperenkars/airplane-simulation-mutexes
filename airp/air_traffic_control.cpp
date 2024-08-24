#include <iostream>
#include <queue>
#include <pthread.h>
#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <cstddef>
//#include "pthread_sleep.c"
// function declaration for pthread_sleep
extern "C" {
    // int pthread_sleep(int seconds);
    #include "pthread_sleep.c"
}
using namespace std;

#define LANDING_TIME 2 // 2t, as requested
#define TAKEOFF_TIME 2 // 2t
#define MAX_WAIT_TIME 10 // maximum wait time for departing planes in t for part 2

struct Plane {
    int id;
    time_t requestTime;
    char status; // 'L' for landing, 'D' for departing, 'E' for emergency
};

queue<Plane> landingQueue;
queue<Plane> departingQueue;

pthread_mutex_t queueMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t runwayCond = PTHREAD_COND_INITIALIZER;

ofstream logFile; // logfile for part3
bool lastWasLanding = false; // track the type of last serviced plane for starvation
time_t startTime; // start time of the simulation
bool stopSimulation = false; // flag to stop the simulation

void logPlane(const Plane& plane, time_t runwayTime) { //writing to a log file
    time_t turnaroundTime = runwayTime - plane.requestTime;
    logFile << plane.id << " " << plane.status << " " 
            << plane.requestTime - startTime << " " 
            << runwayTime - startTime << " " 
            << turnaroundTime << endl;
}

void* planeThread(void* arg) {
    Plane plane = *(Plane*)arg;
    pthread_mutex_lock(&queueMutex);

    // add the plane to the queue depending on whether it is landing or departing
    if (plane.status == 'L') {
        landingQueue.push(plane);
    } else {
        departingQueue.push(plane);
    }

    // send a signal to tower that a plane has arrived
    pthread_cond_signal(&runwayCond);
    pthread_mutex_unlock(&queueMutex);
    pthread_exit(NULL);
}

void* towerThread(void* arg) {
    while (true) {
        pthread_mutex_lock(&queueMutex);


        if (stopSimulation && landingQueue.empty() && departingQueue.empty()) {
            pthread_mutex_unlock(&queueMutex);
            break;
        }

        // if all queues are empty, wait until new plane comes
        while (!stopSimulation && landingQueue.empty() && departingQueue.empty()) {
            pthread_cond_wait(&runwayCond, &queueMutex);
        }

        // added this twice to be able to quit the loop at any time
        if (stopSimulation && landingQueue.empty() && departingQueue.empty()) {
            pthread_mutex_unlock(&queueMutex);
            break;
        }

        Plane plane;
        bool landingPriority = false;

        // check necessary conditions to pick the plane to process
        if (!landingQueue.empty() && !departingQueue.empty()) {
            time_t currentTime = time(NULL);
            Plane nextDeparting = departingQueue.front();

            // if a departing plane has waited enough, service it first
            if ((currentTime - nextDeparting.requestTime) >= MAX_WAIT_TIME) {
                plane = nextDeparting;
                departingQueue.pop();

                // change between landing and departing queues to ensure fair service for part 2
            } else {
                landingPriority = !lastWasLanding;
                if (landingPriority) {
                    plane = landingQueue.front();
                    landingQueue.pop();
                } else {
                    plane = departingQueue.front();
                    departingQueue.pop();
                }
            }
        } else if (!landingQueue.empty()) {
            plane = landingQueue.front();
            landingQueue.pop();
            landingPriority = true;
        } else {
            plane = departingQueue.front();
            departingQueue.pop();
        }

        lastWasLanding = landingPriority;
        pthread_mutex_unlock(&queueMutex);

        // sleep for landing or takeoff by 2t
        if (plane.status == 'L') {
            pthread_sleep(LANDING_TIME);
        } else {
            pthread_sleep(TAKEOFF_TIME);
        }

        time_t currentTime = time(NULL);
        logPlane(plane, currentTime);
    }

    pthread_exit(NULL);
}

void* snapshotThread(void* arg) {
    int startSnapshot = *(int*)arg;
    while (true) {
        sleep(1); // sleep for 1 second
        time_t currentTime = time(NULL) - startTime; // calculate relative time converted from unix timestamp

        if (currentTime >= startSnapshot) {
            pthread_mutex_lock(&queueMutex);  // to output to the terminal at every seconds after given n
            cout << "At " << currentTime << " sec ground: ";

            queue<Plane> tempQueue = landingQueue;
            while (!tempQueue.empty()) {
                Plane plane = tempQueue.front();
                tempQueue.pop();
                cout << plane.id; 
                if (!tempQueue.empty()) {
                    cout << ", ";
                }
            }
            cout << endl;

            cout << "At " << currentTime << " sec air: ";
            tempQueue = departingQueue;
            while (!tempQueue.empty()) {
                Plane plane = tempQueue.front();
                tempQueue.pop();
                cout << plane.id;
                if (!tempQueue.empty()) {
                    cout << ", ";
                }
            }
            cout << endl;

            pthread_mutex_unlock(&queueMutex);
        }

        if (stopSimulation && landingQueue.empty() && departingQueue.empty()) {
            break;
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]) {
    int simTime = 60;
    float p = 0.5;
    int seed = time(NULL);
    int startSnapshot = 5; // default starting second for snapshot, if n is not given

    // parsing command arguments
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (string(argv[i]) == "-s" && i + 1 < argc) {
                simTime = atoi(argv[++i]);
            } else if (string(argv[i]) == "-p" && i + 1 < argc) {
                p = atof(argv[++i]);
            } else if (string(argv[i]) == "--seed" && i + 1 < argc) {
                seed = atoi(argv[++i]);
            } else if (string(argv[i]) == "-n" && i + 1 < argc) {
                startSnapshot = atoi(argv[++i]);
            }
        }
    }

    srand(seed);
    logFile.open("planes.log");

    // write log file header for better representation
    logFile << "PlaneID Status RequestTime RunwayTime TurnaroundTime" << endl;

    startTime = time(NULL); // set the start time of the simulation to now
    time_t endTime = startTime + simTime; // calculate the end time of the simulation

    pthread_t tower;
    pthread_create(&tower, NULL, towerThread, NULL);

    pthread_t snapshot;
    pthread_create(&snapshot, NULL, snapshotThread, (void*)&startSnapshot);

    int planeId = 1;

    // create initial planes to ensure one landing and one departing at s=1, as requested
    for (int i = 0; i < 2; ++i) {
        Plane plane;
        plane.id = planeId++;
        plane.requestTime = startTime; // request time is start time

        // use even IDs for landing and odd IDs for departing, as specified
        if (plane.id % 2 == 0) {
            plane.status = 'L';
        } else {
            plane.status = 'D';
        }

        pthread_t planeThreadID;
        pthread_create(&planeThreadID, NULL, planeThread, (void*)&plane);
        pthread_detach(planeThreadID);

        pthread_sleep(1); // sleep for 1 second
    }

    // main simulation loop
    while (time(NULL) < endTime) {
        Plane plane;
        plane.id = planeId++;
        plane.requestTime = time(NULL);

        // use even IDs for landing and odd IDs for departing again
        if (plane.id % 2 == 0) {
            plane.status = 'L';
        } else {
            plane.status = 'D';
        }

        // plane thread creation every 2 seconds
        pthread_t planeThreadID;
        pthread_create(&planeThreadID, NULL, planeThread, (void*)&plane);
        pthread_detach(planeThreadID);

        pthread_sleep(1); // sleep for 1 second
    }

    // signal to stop the simulation
    pthread_mutex_lock(&queueMutex);
    stopSimulation = true;
    pthread_cond_broadcast(&runwayCond); // signal the tower thread in case it is waiting
    pthread_mutex_unlock(&queueMutex);

    // wait for tower and snapshot threads to finish: this has been picked deliberately to ensure all threads are recorded into logfile
    pthread_join(tower, NULL);
    pthread_join(snapshot, NULL);

    logFile.close();
    return 0;
}

