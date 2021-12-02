#include "mbed.h"
#include "dht22.h"
#include "MiCS6814_GasSensor.h"
#include "mbed.h"
#include "EthernetInterface.h"
#include "NTPClient.h"
#include "OAuth4Tw.h"
#include "SDFileSystem.h"
#include <sstream>
#include <string.h>

#define        COV_RATIO                       0.2            //ug/mmm / mv
#define        NO_DUST_VOLTAGE                 500            //mv

// Sets up serial ports for sensors
Serial host(USBTX, USBRX);
Serial pc(USBTX, USBRX);
Serial uart(USBTX, USBRX);

// Initializes Dust Sensor
DigitalOut myled(LED1), iled(D12);
AnalogIn analog_value(A5);
float density, voltage;

// Initializes temperature and humidity sensor
DHT22 dht22(PTB23);

// Initializes Gas Sensor
#if defined(TARGET_LPC1768)
MiCS6814_GasSensor sensor(p28, p27);
#else
MiCS6814_GasSensor sensor(I2C_SDA, I2C_SCL);
#endif

// Initializes SD Card system
SDFileSystem sd(PTE3, PTE1, PTE2, PTE4, "sd");

// Initializes ethernet interface
#ifndef USE_FIXEDIP
EthernetInterface eth;
#else
EthernetNetIf eth(
  IpAddr(192,168,0,210), //IP Address
  IpAddr(255,255,255,0), //Network Mask
  IpAddr(192,168,0,1), //Gateway
  IpAddr(192,168,0,1)  //DNS
);
#endif

// Initializes ntp pool client
NTPClient ntp;

// Initializes OAuth for posting tweets to system's Twitter account
OAuth4Tw oa4t("oDvBslhcek3Gg36nkD202xuHK", // Consumer key
              "R44jJ2ncs2eH1jfHEDvoH1LyaJoaSrvhx4hKvphNZ10WfKENca", // Consumer secret
              "1353392011789926400-lnSoVdF1h9X6AaLyzVUYPaPdNOtn2z", // Access token
              "duV643F76N0o8Xp1RRaJkkdISmIoe3xfxZN9ixVWjsMSi"); // Access token secret

#if defined(TARGET_LPC1768)
#define RESPONSE_BUFFER_SIZE 512
#elif defined(TARGET_K64F) || defined(TARGET_LPC4088)
#define RESPONSE_BUFFER_SIZE 4096
#else
#error not tested platform.
#endif

char response_buffer[RESPONSE_BUFFER_SIZE];
HTTPText response(response_buffer, sizeof(response_buffer));

// Structure for storing data values
struct DataValues {
    float temperature;
    float humidity;
    float density;
    float nh3;
    float co;
    float no2;
    float c3h8;
    float c4h10;
    float ch4;
    float h2;
    float c2h5oh;
};

// Functions used
time_t updateTime();
void EnvTweet(string buildingName, float index, float temperature, float humidity, float density, float co, float avgSmallVOC, float avgLargeVOC);
void example_getUserData();
float calcInfectivity(DataValues values);

// Constant Values
const int TweetIntervalSec = 60;
const string buildingName = "University Library Entrance";

// Initializes function for dust sensor
static int _filter(int m)
{
  static int flag_first = 0, _buff[10], sum;
  const int _buff_max = 10;
  int i;
  
  if(flag_first == 0)
  {
    flag_first = 1;

    for(i = 0, sum = 0; i < _buff_max; i++)
    {
      _buff[i] = m;
      sum += _buff[i];
    }
    return m;
  }
  else
  {
    sum -= _buff[0];
    for(i = 0; i < (_buff_max - 1); i++)
    {
      _buff[i] = _buff[i + 1];
    }
    _buff[9] = m;
    sum += _buff[9];
    
    i = sum / 10.0;
    return i;
  }
}

int main()
{   
    printf("Initializing Sensor Module...\n");
    
    struct DataValues measurements;
    
    uart.baud(9600);
    iled = 0;
    DHT22_data_t dht22_data;
    
    pc.baud(9600);

    // Connects device to internet
    eth.init(); //Use DHCP
    printf("Initialized, MAC: %s\n", eth.getMACAddress());

    int ret;
    while ((ret = eth.connect()) != 0) {
        printf("Error eth.connect() - ret = %d\n", ret);
    }

    printf("Connected, IP: %s, MASK: %s, GW: %s\n",
           eth.getIPAddress(), eth.getNetworkMask(), eth.getGateway());
    
    // Extracts building name from file
    /*string buildingName = "";
    char c;
    
    FILE *fname = fopen("/sd/BuildingName.txt", "r+");
        if(fname == NULL) {
            error("Could not open file for read\n");
        }
    
    while(1) {
      c = fgetc(fname);
      if( feof(fname) ) {
         break ;
      }
      buildingName.append(1, c);
   }
   
   fclose(fname);
   
   printf("Building Name is %s", buildingName);
   
   // Extracts tweet/measurement interval from file
   string interval = "";
   int TweetIntervalSec;
    
    FILE *finterval = fopen("/sd/Interval.txt", "r");
        if(finterval == NULL) {
            error("Could not open file for read\n");
        }
    
    while(1) {
      c = fgetc(finterval);
      if( feof(finterval) ) {
         break ;
      }
      interval.append(1, c);
   }
   
   fclose(finterval);
   
   stringstream geek(interval);
   geek >> TweetIntervalSec;
   
   printf("Sensor Interval is %i seconds", TweetIntervalSec);*/
    
    while (true) {
    
        // Read sensor data from DHT22 and dust sensor
        dht22.read(&dht22_data);
        
        float temperature = dht22_data.temp / 10.0f;
        float humidity = dht22_data.humidity / 10.0f;
        
        iled = 1;
        wait_us(280);
        voltage = analog_value.read() * 3300 * 11;
        iled = 0;
        
        voltage = _filter(voltage);
        
        if(voltage >= NO_DUST_VOLTAGE)
        {
            voltage -= NO_DUST_VOLTAGE;
            
            density = voltage * COV_RATIO;
        }
        else
            density = 0;
        
        // Print measurements to serial port
        pc.printf("\n");
        host.printf("Temperature: %2.2f    Humidity: %2.2f%%\r\n", temperature, humidity);
        pc.printf("NH3: %.2f ppm, CO: %.2f ppm, NO2: %.2f ppm, C3H8: %.2f ppm \r\n", sensor.getGas(NH3), sensor.getGas(CO), sensor.getGas(NO2), sensor.getGas(C3H8));
        pc.printf("C4H10: %.2f ppm, CH4: %.2f ppm, H2: %.2f ppm, C2H5OH: %.2f ppm \r\n", sensor.getGas(C4H10), sensor.getGas(CH4), sensor.getGas(H2), sensor.getGas(C2H5OH));
        uart.printf("The current dust concentration is: %4.1f ug/m3\r\n", density);
        pc.printf("\n");
        
        measurements.temperature = temperature;
        measurements.humidity = humidity;
        measurements.density = density;
        measurements.nh3 = sensor.getGas(NH3);
        measurements.co = sensor.getGas(CO);
        measurements.no2 = sensor.getGas(NO2);
        measurements.c3h8 = sensor.getGas(C3H8);
        measurements.c4h10 = sensor.getGas(C4H10);
        measurements.ch4 = sensor.getGas(CH4);
        measurements.h2 = sensor.getGas(H2);
        measurements.c2h5oh = sensor.getGas(C2H5OH);
        
        // Calculate infectivity index
        float index = calcInfectivity(measurements);
        pc.printf("Index: %1.2f", index);
        pc.printf("\n");
        
        float avgSmallVOC = (measurements.nh3 + measurements.no2 + measurements.h2 + measurements.c2h5oh) / 4.0;
        float avgLargeVOC = (measurements.c3h8 + measurements.c4h10 + measurements.ch4) / 3.0;
        
        printf("Sending Tweet...\n");
        
        // requires accurate time, for OAuth Authorization.
        time_t current = updateTime();
        
        // Sends tweet
        EnvTweet(buildingName, index, temperature, humidity, density, measurements.co, avgSmallVOC, avgLargeVOC);
        
        // Saves data to SD card
        /*printf("Saving Data...\n");
        
        FILE *fp = fopen("/sd/SensorData.txt", "a");
        if(fp == NULL) {
            error("Could not open file for write\n");
        } 
        fprintf(fp, "%s - %s - Index: %2.2f, Temperature: %2.2f, Humidity: %2.2f, Dust Concentration: %4.1f ug/m3, NH3: %.2f ppm, CO: %.2f ppm, NO2: %.2f ppm, C3H8: %.2f ppm, C4H10: %.2f ppm, CH4: %.2f ppm, H2: %.2f ppm, C2H5OH: %.2f ppm\n", buildingName, ctime(&current), index, measurements.temperature, measurements.humidity, measurements.density, measurements.nh3, measurements.co, measurements.no2, measurements.c3h8, measurements.c4h10, measurements.ch4, measurements.h2, measurements.c2h5oh);
        fclose(fp);
        
        printf("Data Saved!\n");*/

        // Wait interval for next time.
        for (int t=0; t<TweetIntervalSec; t++) {
            myled = 1;
            wait(0.2);
            myled = 0;
            wait(0.8);
        }
        
        myled = !myled;
    }
}

// Updates current time for OAuth request
time_t updateTime()
{
    printf("Trying to update time...\n");

    time_t ctTime;
    NTPResult result;

    while (1) {
        result = ntp.setTime("uk.pool.ntp.org");
        //result = ntp.setTime("pool.ntp.org", NTP_DEFAULT_PORT, 2000);

        if (result == NTP_OK) {
            time(&ctTime);
            printf("Time is set to (UTC): %s\n", ctime(&ctTime));
            break;
        }

        switch (result) {
            case NTP_CONN:      ///<Connection error
                printf("Connection error\n");
                break;
            case NTP_TIMEOUT:   ///<Connection timeout
                printf("Connection timeout\n");
                break;
            case NTP_PRTCL:     ///<Protocol error
                printf("Protocol error\n");
                break;
            case NTP_DNS:       ///<Could not resolve name
                printf("Could not resolve name\n");
                break;
            default:
                printf("Error result=%d\n", result);
                break;
        }

        wait(5);
    }
}

// Posts tweet to system's Twitter account
void EnvTweet(string buildingName, float index, float temperature, float humidity, float density, float co, float avgSmallVOC, float avgLargeVOC)
{           
    const char url[] = "https://api.twitter.com/1.1/statuses/update.json"
                       "?status=%s";
    char url2[240];
    char status[240];
    
    time_t ctTime;
    time(&ctTime);
    
    sprintf(status,"%s - Index: %2.2f, Temp: %2.2f C, Humidity: %2.2f%%, Dust: %4.1f ug per cubic m, CO: %.2f ppm, Small VOC: %.2f ppm, Large VOC: %.2f ppm", buildingName, index, temperature, humidity, density, co, avgSmallVOC, avgLargeVOC);

    snprintf(url2, sizeof(url2), url, status);

    HTTPResult result = oa4t.post(url2, &response);

    if (result == HTTP_OK) {
        printf("POST success.\n%s\n", response_buffer);
    } else {
        printf("POST error. (result = %d)\n", result);
    }
}

// Used to extract data from system's Twitter account
void example_getUserData()
{
    const char url[] = "https://api.twitter.com/1.1/users/show.json"
                       "?screen_name=twitter";

    HTTPResult result = oa4t.get(url, &response);

    if (result == HTTP_OK) {
        printf("GET success.\n%s\n", response_buffer);
    } else {
        printf("GET error. (result = %d)\n", result);
    }
}

// Calculates infectivity index using weighted average
float calcInfectivity(DataValues values)
{
    float temp = values.temperature;
    float humidity = values.humidity;
    float density = values.density;
    float nh3 = values.nh3;
    float co = values.co;
    float no2 = values.no2;
    float c3h8 = values.c3h8;
    float c4h10 = values.c4h10;
    float ch4 = values.ch4;
    float h2 = values.h2;
    float c2h5oh = values.c2h5oh;
    
    int tempIndex = (((temp - 18.00) * (4.00)) / (30.00 - 18.00)) + 1.00;
    if(tempIndex > 5.00) {
        tempIndex = 5.00;
    }
    else if(tempIndex < 1.00) {
        tempIndex = 1.00;
    }
        
    int humidIndex = (((humidity - 40.00) * (4.00)) / (100.00 - 40.00)) + 1.00;
    if(humidIndex > 5.00) {
        humidIndex = 5.00;
    }
    else if(tempIndex < 1.00) {
        humidIndex = 1.00;
    }
        
    int densityIndex = (((density - 50.0) * (4.0)) / (200.0 - 50.0)) + 1.00;
    if(densityIndex > 5.00) {
        densityIndex = 5.00;
    }
    else if(tempIndex < 1.00) {
        densityIndex = 1.00;
    }
        
    int coIndex = (((co) * (4.00)) / (10.00 - 0.00)) + 1.00;
    if(coIndex > 5.00) {
        coIndex = 5.00;
    }
    else if(coIndex < 1.00) {
        coIndex = 1.00;
    }
        
    float avgSmallVOC = (nh3 + no2 + h2 + c2h5oh) / 4.0;
    int smallVOCIndex = (((avgSmallVOC) * (4.00)) / (2.00)) + 1.00;
    if(smallVOCIndex > 5.00) {
        smallVOCIndex = 5.00;
    }
    else if(smallVOCIndex < 1.00) {
        smallVOCIndex = 1.00;
    }
        
    float avgLargeVOC = (c3h8 + c4h10 + ch4) / 3.0;
    int largeVOCIndex = (((avgLargeVOC) * (4.00)) / (1000)) + 1.00;
    if(largeVOCIndex > 5.00) {
        largeVOCIndex = 5.00;
    }
    else if(largeVOCIndex < 1.00) {
        largeVOCIndex = 1.00;
    }
    
    float weightedAvg = densityIndex*.25 + humidIndex*.25 + tempIndex*.25 + coIndex*.125 + ((smallVOCIndex + largeVOCIndex) / 2)*.125;
    
    return weightedAvg;
}
    