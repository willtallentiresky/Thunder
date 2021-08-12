#include "Module.h"
#include <core/core.h>

using namespace WPEFramework;
using ClusterList = std::list<uint16_t>;


/**
 * The DeviceObject Class is an oversimplified zigbee ZDO class containing several endpoints.
 * 
 * endpoints: a list of information for each available endpoint. Each Endpoint may contain several 
 * in or out clusters
 *
 * Clusters are a group of commands and attributes that define what a device can do. Think of
 * clusters as a group of actions by function. A device can support multiple clusters to do a
 * whole variety of tasks. The majority of clusters are defined by the ZigBee Alliance and 
 * listed in the ZigBee Cluster Library. There are also profile specific clusters that are
 * defined by their own ZigBee profile like Home Automation or ZigBee Smart Energy, and 
 * Manufacture Specific clusters that are defined by the manufacture of the device. These 
 * are typically used when no existing cluster can be used for a device.
 * Most used clusters are
 *  0x0006 - On/Off (Switch)
 *  0x0008 - Level Control (Dimmer)
 *  0x0201 - Thermostat
 *  0x0202 - Fan Control
 *  0x0402 - Temperature Measurement
 *  0x0406 - Occupancy Sensing 
 *
 *  Client:
 *   A cluster interface which is listed in the output cluster list of the simple descriptor on an endpoint.
 *   Typically this interface sends commands that manipulate the attributes on the corresponding server cluster. 
 *   A client cluster communicates with a corresponding remote server cluster with the same cluster identifier.
 *
 *  Server:
 *   A cluster interface which is listed in the input cluster list of the simple descriptor on an endpoint.
 *  Typically this interface supports all or most of the attributes of the cluster. A server cluster communicates 
 *  with a corresponding remote client cluster with the same cluster identifier.
 *
 *
 *  In this example we focus only on the ZDO to process a Simple_Descriptor_Response sent by 
 *  a newly joined device. When it creates a new endpoint it uses the ProxyType to create a new element with
 *  additional space to save the in/out clusters. A couple of canned Simple Descriptor responses are used here which
 *  will be parsed and the endpoints created. The test will try to retrieve the data stored in the additional space and
 *  print it.
 * 
 */
class  DeviceObject {
    public:
  
        DeviceObject(const DeviceObject&) = delete;
        DeviceObject& operator= (const DeviceObject&) = delete;

        DeviceObject()
				: _endPoints(){
                LoadData();
			}
    
    class Element {
    public:
        class Iterator {
        public:
            Iterator()
                : _parent()
                , _index(0)
                , _length(0)
                , _data(nullptr) {
            }
            Iterator(const Core::ProxyType<const Element>& parent, const uint8_t length, const uint16_t clusters[])
                : _parent(parent)
                , _index(0)
                , _length(length)
                , _data(clusters) {
            }
            Iterator(const Iterator& copy)
                : _parent(copy._parent)
                , _index(copy._index)
                , _length(copy._length)
                , _data(copy._data) {
            }
            ~Iterator() {
            }

            Iterator& operator= (const Iterator& rhs)
            {
                _parent = rhs._parent;
                _index = rhs._index;
                _length = rhs._length;
                _data = rhs._data;

                return (*this);
            }

        public:
            bool IsValid() const {
                return ((_index <= _length) && (_index != 0));
            }
            void Reset() {
                _index = 0;
            }
            bool Next() {

                if (_index <= _length) {

                    _index++;
                }

                return (_index <= _length);
            }
            uint16_t Cluster() const {

                ASSERT(IsValid() == true);

                return (_data[_index - 1]);
            }

        private:
            Core::ProxyType<const Element> _parent;
            uint8_t _index;
            uint8_t _length;
            const uint16_t* _data;
        };

    private:
        friend class Core::ProxyService<Element>;

        static uint16_t* FillClusters(uint16_t* data, const uint8_t source[]) {
            uint8_t count = source[0];
            source++;

            while (count != 0) {
                *data++ = ((source[1] << 8) | (source[0]));
                source++;
                source++;
                count--;
            }

            return (data);
        }
        Element(const uint8_t data[])
            : _endPoint(data[0])
            , _protocol((data[2] << 8) | data[1])
            , _device((data[4] << 8) | data[3])
            , _version(data[5])
            , _inClusters(data[6])
            , _outClusters(data[7 + (_inClusters * sizeof(uint16_t))]) {

            uint16_t* storage = Storage(0);
            storage = FillClusters(storage, &(data[6]));
            FillClusters(storage, &(data[7 + (_inClusters * sizeof(uint16_t))]));
        }

    public:
        Element() = delete;
        Element(const Element&) = delete;
        Element& operator= (const Element&) = delete;

        static Core::ProxyType<Element> Create(const uint8_t data[]) {

            uint32_t clusterSize = static_cast<uint32_t>((data[6] + data[7 + (data[6] * sizeof(uint16_t))]) * sizeof(uint16_t));
            return (Core::ProxyType<Element>::CreateEx(clusterSize, data));
        }
        virtual ~Element() {
        }

    public:
        inline uint16_t Clusters() const {
            return (_inClusters + _outClusters);
        }
        inline uint8_t Id() const {
            return (_endPoint);
        }
        inline uint16_t Profile() const {
            return (_protocol);
        }
        inline uint16_t Device() const {
            return (_device);
        }
        inline uint8_t Version() const {
            return (_version);
        }
        inline Iterator InClusters() const {
            return (Iterator(Core::ProxyType<const Element>(*this), _inClusters, Storage(0)));
        }
        inline Iterator OutClusters() const {
            return (Iterator(Core::ProxyType<const Element>(*this), _outClusters, Storage(_inClusters)));
        }
        
        bool HasInCluster(const uint16_t cluster) const {
                uint8_t count = _inClusters;
                const uint16_t* current = Storage(0);

                while ((count > 0) && (*current != cluster)) { count--; }

                return (count != 0);
            }
        inline bool HasOutCluster(const uint16_t cluster) const {
                uint8_t count = _outClusters;
                const uint16_t* current = Storage(_inClusters);

                while ((count > 0) && (*current != cluster)) { count--; }

                return (count != 0);
            }

        inline void InClusters(ClusterList& list) const {
            const uint16_t* data = Storage(0);
            
            for (uint8_t index = 0; index < _inClusters; index++) {
                list.emplace_back(data[index]);
            }
        }
        inline void OutClusters(ClusterList& list) const {
            const uint16_t* data = Storage(_inClusters);
            for (uint8_t index = 0; index < _outClusters; index++) {
                list.emplace_back(data[index]);
            }
        }

    private:
        inline uint16_t* Storage(const uint8_t index) {
            return (&(static_cast<Core::ProxyObject<Element>&>(*this).Store<uint16_t>()[index]));
        }
        inline const uint16_t* Storage(const uint8_t index) const {
            return (&(static_cast<const Core::ProxyObject<Element>&>(*this).Store<uint16_t>()[index]));
        }
        const uint8_t* ClusterLoad(const uint8_t* data, std::list<uint16_t>& clusters) {
            uint8_t count = data[0];
            data++;
            while (count != 0) {
                count--;
                clusters.push_back((data[1] << 8) | (data[0]));
                data = &(data[2]);
            }
            return (data);
        }

    private:
        uint8_t _endPoint;
        uint16_t _protocol;
        uint16_t _device;
        uint8_t _version;
        uint8_t _inClusters;
        uint8_t _outClusters;
    };

    class Iterator {
        public:
            Iterator()
                : _position(0)
                , _container()
                , _index() {
            }
            Iterator(const std::list<Core::ProxyType<const Element> >& points)
                : _position(0)
                , _container()
                , _index() {
                std::list<Core::ProxyType<const Element> > ::const_iterator index(points.cbegin());

                while (index != points.cend()) {
                    _container.emplace_back(*index);
                    index++;
                }
                _index = _container.begin();
            }
            Iterator(const Iterator& copy)
                : _position(0)
                , _container()
                , _index() {
                std::list<Core::ProxyType<const Element> > ::const_iterator index(copy._container.cbegin());

                while (index != copy._container.cend()) {
                    _container.emplace_back(*index);
                    index++;
                }
                _index = _container.begin();
            }
            ~Iterator() {
            }

            Iterator& operator= (const Iterator& rhs)
            {
                _container.clear();
                std::list<Core::ProxyType<const Element> > ::const_iterator index(rhs._container.cbegin());

                while (index != rhs._container.cend()) {
                    _container.emplace_back(*index);
                    index++;
                }
                _position = rhs._position;
                _index = rhs._index;

                return (*this);
            }

            using Clusters = Element::Iterator;

        public:
            inline uint8_t EndPoint() const {
                return ((*_index)->Id());
            }
            inline uint16_t Profile() const {
                return ((*_index)->Profile());
            }
            inline uint16_t Device() const {
                return ((*_index)->Device());
            }
            inline uint8_t Version() const {
                return ((*_index)->Version());
            }
            inline Clusters InClusters() const {
                return ((*_index)->InClusters());
            }
            inline Clusters OutClusters() const {
                return ((*_index)->OutClusters());
            }
            inline void InClusters(ClusterList& clusters) const {
                (*_index)->InClusters(clusters);
            }
            inline void OutClusters(ClusterList& clusters) const {
                (*_index)->OutClusters(clusters);
            }
            inline bool HasInCluster(const uint16_t cluster) const {
                return ((*_index)->HasInCluster(cluster));
            }
            inline bool HasOutCluster(const uint16_t cluster) const {
                return ((*_index)->HasOutCluster(cluster));
            }
            bool IsValid() const {
                return ((_position != 0) && _index != _container.end());
            }
            void Reset() {
                _position = 0;
                _index = _container.begin();
            }
            bool Next() {

                if (_position == 0) {
                    _position++;
                }
                else if (_index != _container.end()) {
                    _position++;
                    _index++;
                }

                return (IsValid());
            }
            uint8_t Count() const {
                return (static_cast<uint8_t>(_container.size()));
            }

        private:
            uint16_t _position;
            std::list< Core::ProxyType<const Element> > _container;
            std::list< Core::ProxyType<const Element> >::iterator _index;
        };
    void Simple_Desc_rsp(const uint8_t length, const uint8_t stream[]) {
        uint8_t bytes = std::min(stream[2], static_cast<uint8_t>(length - 3));
        printf("Incoming Len %u Desc Size %u\n",length,stream[2]);
        const uint8_t* data = &(stream[3]);

        while (bytes >= 7) {
            printf("Creating an Element\n");
            _endPoints.emplace_back(Element::Create(data));

            uint8_t loaded = (8 + static_cast<uint8_t>(_endPoints.back()->Clusters() * 2));
            bytes = (bytes >= loaded ? (bytes - loaded) : 0);
        }
	} 
    void LoadData() {
        
         /*
            Simple Descriptorssssss
                Endpoint: 1
                Profile: Home Automation (0x0104)
                Application Device: Unknown (0x0050)
                Application Version: 0x0014
                Input Cluster Count: 6
                Input Cluster List
                    Input Cluster: Basic (0x0000)
                    Input Cluster: Identify (0x0003)
                    Input Cluster: Partition (0x0016)
                    Input Cluster: Message (0x0703)
                    Input Cluster: Time (0x000a)
                    Input Cluster: Simple Metering (0x0702)
                Output Cluster Count: 9
                Output Cluster List
                    Output Cluster: Appliance Identification (0x0b00)
                    Output Cluster: Appliance Control (0x001b)
                    Output Cluster: Appliance Events And Alerts (0x0b02)
                    Output Cluster: Appliance Statistics (0x0b03)
                    Output Cluster: Power Profile (0x001a)
                    Output Cluster: Simple Metering (0x0702)
                    Output Cluster: Unknown (0x0a06)
                    Output Cluster: Unknown (0x0a08)
                    Output Cluster: Partition (0x0016)
        */
    static const uint8_t SimpleDesc[] = { 0x00, 0x00, 0x26, 0x01, 0x04, 0x01, 0x50, 0x00, 0x14, 0x06,
                                          0x00, 0x00, 0x03, 0x00, 0x16, 0x00, 0x03, 0x07, 0x0a, 0x00,
                                          0x02, 0x07, 0x09, 0x00, 0x0b, 0x1b, 0x00, 0x02, 0x0b, 0x03,
                                          0x0b, 0x1a, 0x00, 0x02, 0x07, 0x06, 0x0a, 0x08, 0x0a, 0x16,
                                          0x00 };
       
        /*
        Simple Descriptor
            Endpoint: 1s
            Profile: Home Automation (0x0104)
            Application Device: Unknown (0x0053)
            Application Version: 0x0000
            Input Cluster Count: 5
            Input Cluster List
                Input Cluster: Basic (0x0000)
                Input Cluster: Identify (0x0003)
                Input Cluster: Key Establishment (0x0800)
                Input Cluster: On/Off (0x0006)
                Input Cluster: Simple Metering (0x0702)
            Output Cluster Count: 2
            Output Cluster List
                Output Cluster: Key Establishment (0x0800)
                Output Cluster: Time (0x000a)
        */
    static const uint8_t SimpleDesc_OnOff[] = { 0xa4, 0x50, 0x16, 0x01, 0x04, 0x01, 0x53, 0x00, 0x00, 0x05, 
                                                0x00, 0x00, 0x03, 0x00, 0x00, 0x08, 0x06, 0x00, 0x02, 0x07,
                                                0x02, 0x00, 0x08, 0x0a, 0x00};

        printf("DeviceObject::LoadData\n");
        Simple_Desc_rsp(static_cast<uint8_t>(sizeof(SimpleDesc)), SimpleDesc);
        Simple_Desc_rsp(static_cast<uint8_t>(sizeof(SimpleDesc_OnOff)), SimpleDesc_OnOff);                            
    }
    Iterator EndPoints() const {
		return (Iterator(_endPoints));
	}

    private:
    std::list<Core::ProxyType< const Element > > _endPoints;
 
 };

int _tmain(int argc, _TCHAR**)
#else
int main(int argc, const char* argv[])
#endif
{
    fprintf(stderr, "%s build: %s\n"
                    "licensed under LGPL2.1\n", argv[0], __TIMESTAMP__);
#ifdef __WIN32__
#pragma warning(disable : 4355)
#endif
    
    DeviceObject deviceObject;
    DeviceObject::Iterator eps = deviceObject.EndPoints();
    printf("Number of EndPoints - %u\n",eps.Count());
    while (eps.Next()){
        printf("\n\nEndpoint: %u\nProfile: 0x%04X\nApplication Device: 0x%04X\nApplication Version: 0x%04X\n",
                eps.EndPoint(),eps.Profile(),eps.Device(),eps.Version());
        DeviceObject::Element::Iterator cii = eps.InClusters();
        printf("Input Cluster List\n");
        while(cii.Next()){
            printf("\tInput Cluster: 0x%04X\n",cii.Cluster());
        }
        printf("Output Cluster List\n");
        DeviceObject::Element::Iterator coi = eps.OutClusters();
        while(coi.Next()){
            printf("\tInput Cluster: 0x%04X\n",coi.Cluster());
        }
    }
          
    WPEFramework::Core::Singleton::Dispose();
    printf("\nClosing down!!!\n");
    return 0;
}

