
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <iomanip>
using namespace std;

namespace DisasterCore {

namespace Alerts {

class EarlyWarningSiren {
    static int alertLevel;

public:
    static void setAlertLevel(int level) {
        alertLevel = level;
    }

    static int getAlertLevel() {
        return alertLevel;
    }

    void trigger(int region) const {
        cout << "SIREN ALERT: Station " << region
             << " | Level " << alertLevel << endl;
    }
};

int EarlyWarningSiren::alertLevel = 0;

struct Incident {
    int stationId;
    double hazard;
    int alertLevel;
};

class IncidentLogger {
    fstream file;

public:
    IncidentLogger() {
        file.open("incidents.dat", ios::in | ios::out | ios::binary);

        if (!file) {
            file.clear();
            ofstream create("incidents.dat", ios::binary);
            create.close();

            file.open("incidents.dat", ios::in | ios::out | ios::binary);
        }

        if (!file)
            throw runtime_error("Unable to open incident file");
    }

    void logIncident(const Incident& incident) {
        file.clear();
        file.seekp(0, ios::end);

        streampos position = file.tellp();

        file.write(reinterpret_cast<const char*>(&incident),
                   sizeof(incident));

        file.flush();

        if (!file) {
            file.clear();
            file.seekp(position);

            Incident empty = {};
            file.write(reinterpret_cast<const char*>(&empty),
                       sizeof(empty));

            file.flush();
            throw runtime_error("Incident logging failed");
        }

        file.seekg(position);

        Incident check;
        file.read(reinterpret_cast<char*>(&check), sizeof(check));

        if (!file || check.stationId != incident.stationId ||
            check.hazard != incident.hazard ||
            check.alertLevel != incident.alertLevel) {

            file.clear();
            file.seekp(position);

            Incident empty = {};
            file.write(reinterpret_cast<const char*>(&empty),
                       sizeof(empty));

            file.flush();
            throw runtime_error("Incident verification failed");
        }
    }

    ~IncidentLogger() {
        file.close();
    }
};

ostream& tableFormat(ostream& out) {
    out << left << setw(15);
    return out;
}

}

namespace Sensors {

class SensorFailureException : public exception {
public:
    const char* what() const noexcept override {
        return "Sensor failure detected";
    }
};

class TelemetryChecksumMismatch : public exception {
public:
    const char* what() const noexcept override {
        return "Telemetry checksum mismatch";
    }
};

class ThresholdOverflowException : public exception {
public:
    const char* what() const noexcept override {
        return "Hazard threshold exceeded";
    }
};

struct RainSlope {
    double rainfall;
    double slope;
};

struct SoilVibration {
    double saturation;
    double vibration;
};

inline double computeHazardIndex(const RainSlope& data) {
    return data.rainfall * 0.6 + data.slope * 0.4;
}

inline double computeHazardIndex(const SoilVibration& data) {
    return data.saturation * 0.7 + data.vibration * 0.3;
}

struct Coordinates {
    double latitude;
    double longitude;
};

Coordinates& transformCoordinates(Coordinates& c, double offset) {
    c.latitude += offset;
    c.longitude += offset;
    return c;
}

double& accumulateTelemetry(double& total, double value) {
    total += value;
    return total;
}

class TelemetryAuditor;

class PowerSubsystem {
    double battery;

public:
    PowerSubsystem(double b = 100) : battery(b) {}

    bool hasPower() const {
        return battery > 0;
    }

    void consume(double amount) {
        battery -= amount;

        if (battery < 0)
            battery = 0;
    }
};

class DisasterTelemetryDevice {
protected:
    int deviceId;

public:
    DisasterTelemetryDevice(int id = 0) : deviceId(id) {}

    virtual ~DisasterTelemetryDevice() {}

    int getDeviceId() const {
        return deviceId;
    }
};

class SolarPoweredNode : virtual public DisasterTelemetryDevice {
protected:
    double solarCapacity;

public:
    SolarPoweredNode(double capacity = 100)
        : solarCapacity(capacity) {}
};

class WirelessLoRaTransceiver : virtual public DisasterTelemetryDevice {
protected:
    int signalStrength;

public:
    WirelessLoRaTransceiver(int signal = 100)
        : signalStrength(signal) {}
};

class AutonomousHybridProbe :
    public SolarPoweredNode,
    public WirelessLoRaTransceiver {

public:
    AutonomousHybridProbe(int id = 0)
        : DisasterTelemetryDevice(id),
          SolarPoweredNode(100),
          WirelessLoRaTransceiver(100) {}
};

class BaseSensorNode : public AutonomousHybridProbe {
protected:
    PowerSubsystem power;

public:
    BaseSensorNode(int id)
        : DisasterTelemetryDevice(id),
          AutonomousHybridProbe(id),
          power(100) {}

    virtual void readTelemetry() = 0;

    virtual double evaluateThreshold() const = 0;

    virtual ~BaseSensorNode() {}
};

class SensorStation : public BaseSensorNode {
    double rainfall;
    double slope;
    double saturation;
    double vibration;
    double hazard;

    static int activeStations;

    friend class TelemetryAuditor;

public:
    SensorStation(int id, double rain, double sl,
                  double soil, double vib)
        : DisasterTelemetryDevice(id),
          BaseSensorNode(id),
          rainfall(rain),
          slope(sl),
          saturation(soil),
          vibration(vib),
          hazard(0) {
        activeStations++;
    }

    SensorStation(const SensorStation& s)
        : DisasterTelemetryDevice(s.deviceId),
          BaseSensorNode(s.deviceId),
          rainfall(s.rainfall),
          slope(s.slope),
          saturation(s.saturation),
          vibration(s.vibration),
          hazard(s.hazard) {
        activeStations++;
    }

    ~SensorStation() {
        activeStations--;
    }

    void writeData(ostream& out) const {
        out << deviceId << " "
            << rainfall << " "
            << slope << " "
            << saturation << " "
            << vibration << endl;
    }

    void readTelemetry() override {
        if (!power.hasPower())
            throw SensorFailureException();

        RainSlope rainData = {rainfall, slope};
        SoilVibration soilData = {saturation, vibration};

        double rainHazard = computeHazardIndex(rainData);
        double soilHazard = computeHazardIndex(soilData);

        hazard = max(rainHazard, soilHazard);

        power.consume(1);

        cout << "Station " << deviceId
             << " telemetry collected. Hazard: "
             << hazard << endl;
    }

    double evaluateThreshold() const override {
        if (hazard > 100)
            throw ThresholdOverflowException();

        return hazard;
    }


    void updateReadings(double rain, double sl, double soil, double vib) {
        rainfall = rain;
        slope = sl;
        saturation = soil;
        vibration = vib;
        hazard = 0;
    }

    bool operator>(const SensorStation& s) const {
        return hazard > s.hazard;
    }

    static int getActiveStations() {
        return activeStations;
    }

    friend ostream& operator<<(ostream& out,
                               const SensorStation& s) {
        out << "Station ID: " << s.deviceId
            << " | Rainfall: " << s.rainfall
            << " | Slope: " << s.slope
            << " | Saturation: " << s.saturation
            << " | Vibration: " << s.vibration
            << " | Hazard: " << s.hazard;

        return out;
    }

    friend istream& operator>>(istream& in, SensorStation& s) {
        in >> s.rainfall >> s.slope
           >> s.saturation >> s.vibration;

        return in;
    }
};

int SensorStation::activeStations = 0;

class TelemetryAuditor {
public:
    void inspect(const SensorStation& s) const {
        cout << "\n===== STATION AUDIT =====\n";
        cout << "Station ID: " << s.deviceId << endl;
        cout << "Rainfall: " << s.rainfall << endl;
        cout << "Slope: " << s.slope << endl;
        cout << "Soil Saturation: " << s.saturation << endl;
        cout << "Vibration: " << s.vibration << endl;
        cout << "Hazard Index: " << s.hazard << endl;
        cout << "=========================\n";
    }
};

class TelemetryBuffer {
    double* data;
    int size;

public:
    TelemetryBuffer(int n) : size(n) {
        data = new double[size]{};
    }

    TelemetryBuffer(int n, double value) : size(n) {
        data = new double[size];

        for (int i = 0; i < size; i++)
            data[i] = value;
    }

    TelemetryBuffer(const TelemetryBuffer& b) : size(b.size) {
        data = new double[size];

        for (int i = 0; i < size; i++)
            data[i] = b.data[i];
    }

    TelemetryBuffer& operator=(const TelemetryBuffer& b) {
        if (this != &b) {
            double* temp = new double[b.size];

            for (int i = 0; i < b.size; i++)
                temp[i] = b.data[i];

            delete[] data;
            data = temp;
            size = b.size;
        }

        return *this;
    }

    double& operator[](int index) {
        return data[index];
    }

    const double& operator[](int index) const {
        return data[index];
    }

    int getSize() const {
        return size;
    }

    ~TelemetryBuffer() {
        delete[] data;
    }
};

class RegionalZone {
public:
    vector<int> stations;

    RegionalZone operator+(const RegionalZone& r) const {
        RegionalZone result;

        result.stations = stations;

        result.stations.insert(
            result.stations.end(),
            r.stations.begin(),
            r.stations.end()
        );

        return result;
    }
};

class EvacuationPriority {
public:
    bool operator()(const pair<int, double>& a,
                    const pair<int, double>& b) const {
        return a.second > b.second;
    }
};

}

}

using namespace DisasterCore;
using namespace DisasterCore::Sensors;
using namespace DisasterCore::Alerts;


int main() {
    ifstream file("stations.txt");

    if (!file) {
        cout << "Station data file not found.\n";
        return 1;
    }

    int n;

    if (!(file >> n) || n <= 0) {
        cout << "Invalid station data file.\n";
        return 1;
    }

    vector<SensorStation> stations;

    for (int i = 0; i < n; i++) {
        int id;
        double rain, slope, soil, vibration;

        if (!(file >> id >> rain >> slope >> soil >> vibration)) {
            cout << "Invalid station record.\n";
            return 1;
        }

        for (const auto& s : stations) {
            if (s.getDeviceId() == id) {
                cout << "Duplicate station ID.\n";
                return 1;
            }
        }

        stations.emplace_back(id, rain, slope, soil, vibration);
    }

    file.close();

    TelemetryAuditor auditor;
    IncidentLogger logger;

    auto saveData = [&]() {
        ofstream out("stations.txt");

        if (!out) {
            cout << "Unable to save station data.\n";
            return;
        }

        out << stations.size() << endl;

        for (const auto& s : stations) s.writeData(out);
        out.close();
        cout << "Station data saved successfully.\n";
    };

    int choice;

    do {
        cout << "\n========== DISASTER MONITORING SYSTEM ==========\n";
        cout << "1. Display All Stations\n";
        cout << "2. Search Station by ID\n";
        cout << "3. Audit Station by ID\n";
        cout << "4. Add New Station\n";
        cout << "5. Update Station Telemetry\n";
        cout << "6. Run Sensor Sweep\n";
        cout << "7. Display Active Stations\n";
        cout << "8. Save Station Data\n";
        cout << "9. Exit\n";
        cout << "Enter choice: ";

        if (!(cin >> choice)) {
            cout << "Invalid input.\n";
            break;
        }

        switch (choice) {

        case 1:
            if (stations.empty()) {
                cout << "No stations registered.\n";
                break;
            }

            cout << "\n========== STATION DETAILS ==========\n";

            for (const auto& s : stations)
                cout << s << endl;

            break;

        case 2: {
            int id;
            bool found = false;

            cout << "Enter Station ID: ";
            cin >> id;

            for (const auto& s : stations) {
                if (s.getDeviceId() == id) {
                    cout << s << endl;
                    found = true;
                    break;
                }
            }

            if (!found)
                cout << "Station not found.\n";

            break;
        }

        case 3: {
            int id;
            bool found = false;

            cout << "Enter Station ID: ";
            cin >> id;

            for (const auto& s : stations) {
                if (s.getDeviceId() == id) {
                    auditor.inspect(s);
                    found = true;
                    break;
                }
            }

            if (!found)
                cout << "Station not found.\n";

            break;
        }

        case 4: {
            int id;
            double rain, slope, soil, vibration;

            cout << "Enter New Station ID: ";
            cin >> id;

            bool exists = false;

            for (const auto& s : stations) {
                if (s.getDeviceId() == id) {
                    exists = true;
                    break;
                }
            }

            if (exists) {
                cout << "Station ID already exists.\n";
                break;
            }

            cout << "Enter Rainfall Rate: ";
            cin >> rain;

            cout << "Enter Slope Angle: ";
            cin >> slope;

            cout << "Enter Soil Saturation: ";
            cin >> soil;

            cout << "Enter Vibration Frequency: ";
            cin >> vibration;

            if (rain < 0 || slope < 0 || soil < 0 ||
                vibration < 0 || rain > 100 || slope > 100 ||
                soil > 100 || vibration > 100) {
                cout << "Invalid sensor readings.\n";
                break;
            }

            stations.emplace_back(id, rain, slope, soil, vibration);

            cout << "Station added successfully.\n";
            saveData();

            break;
        }

        case 5: {
            int id;
            bool found = false;

            cout << "Enter Station ID: ";
            cin >> id;

            for (auto& s : stations) {
                if (s.getDeviceId() == id) {
                    double rain, slope, soil, vibration;

                    cout << "Enter New Rainfall Rate: ";
                    cin >> rain;

                    cout << "Enter New Slope Angle: ";
                    cin >> slope;

                    cout << "Enter New Soil Saturation: ";
                    cin >> soil;

                    cout << "Enter New Vibration Frequency: ";
                    cin >> vibration;

                    if (rain < 0 || slope < 0 || soil < 0 ||
                        vibration < 0 || rain > 100 || slope > 100 ||
                        soil > 100 || vibration > 100) {
                        cout << "Invalid sensor readings.\n";
                        found = true;
                        break;
                    }

                    s.updateReadings(rain, slope, soil, vibration);

                    cout << "Telemetry updated successfully.\n";
                    saveData();

                    found = true;
                    break;
                }
            }

            if (!found)
                cout << "Station not found.\n";

            break;
        }

        case 6: {
            vector<BaseSensorNode*> sensors;
            vector<pair<int, double>> priorities;
            double totalHazard = 0;

            for (auto& s : stations)
                sensors.push_back(&s);

            if (sensors.empty()) {
                cout << "No stations available.\n";
                break;
            }

            cout << "\n========== SENSOR SWEEP ==========\n";

            for_each(sensors.begin(), sensors.end(),
                [&](BaseSensorNode* sensor) {
                    try {
                        sensor->readTelemetry();

                        double hazard = sensor->evaluateThreshold();

                        priorities.push_back({
                            sensor->getDeviceId(), hazard
                        });

                        accumulateTelemetry(totalHazard, hazard);

                        if (hazard > 50) {
                            EarlyWarningSiren::setAlertLevel(2);

                            EarlyWarningSiren siren;
                            siren.trigger(sensor->getDeviceId());

                            logger.logIncident({
                                sensor->getDeviceId(),
                                hazard,
                                EarlyWarningSiren::getAlertLevel()
                            });
                        }
                        else {
                            cout << "Station "
                                 << sensor->getDeviceId()
                                 << ": Normal condition.\n";
                        }
                    }
                    catch (const exception& e) {
                        cout << "Station "
                             << sensor->getDeviceId()
                             << ": " << e.what() << endl;
                    }
                });

            sort(priorities.begin(), priorities.end(),
                 EvacuationPriority());

            cout << "\n========== EVACUATION PRIORITY ==========\n";

            for (const auto& p : priorities)
                cout << "Station " << p.first
                     << " | Hazard: " << p.second << endl;

            cout << "Total Hazard: " << totalHazard << endl;

            break;
        }

        case 7:
            cout << "Active Stations: "
                 << SensorStation::getActiveStations() << endl;
            break;

        case 8:
            saveData();
            break;

        case 9:
            saveData();
            cout << "Exiting Disaster Monitoring System.\n";
            break;

        default:
            cout << "Invalid choice.\n";
        }

    } while (choice != 9);

    return 0;
}
