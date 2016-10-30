
#include <PulseSensorBPM.h>
#include <SPI.h>
#include <WiFi101.h>
#include <WiFiSSLClient.h>
#include <MQTTClient.h>


const boolean HAS_A_REF = false; //BUG? analogReference(EXTERNAL) causes a compile error on Arduino 101.
const int PIN_INPUT = A0;
const int PIN_BLINK = 13;        // Pin 13 is the on-board LED
const int PIN_FADE = 3;          // must be a pin that supports PWM.

const unsigned long MICROS_PER_READ = 2 * 1000L;


const boolean REPORT_JITTER_AND_HANG = false;
const long OFFSET_MICROS = 1L;  // NOTE: must be non-negative

unsigned long wantMicros;
long minJitterMicros;
long maxJitterMicros;

// time (value of micros()) when we last reported jitter.
unsigned long lastReportMicros;

byte samplesUntilReport;
const byte SAMPLES_PER_SERIAL_SAMPLE = 20;

// PWM steps per fade step.  More fades faster; less fades slower.
const int PWM_STEPS_PER_FADE = 12;

int fadePWM;

PulseSensorBPM pulseDetector(PIN_INPUT, MICROS_PER_READ / 1000L);
/*use this class if you connect using SSL
 * WiFiSSLClient net;
*/
WiFiClient net;
MQTTClient client;

unsigned long lastMillis = 0;

void connect() {
  Serial.print("checking wifi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  /*
    client.connect("clientID", "username", "password")
    IBM Bluemix 
    clientID = "d:<WatsonIOTOrganizationID>:<typedevice>:<iddevice>"
    username is aways: "use-token-auth"
    password is: Token auth provided by Bluemix

    Example:
    client.connect("d:iqwckl:arduino:oxigenarbpm","use-token-auth","90wT2?a*1WAMVJStb1")
    
    Documentation: 
    https://console.ng.bluemix.net/docs/services/IoT/iotplatform_task.html#iotplatform_task
  */
  Serial.print("\nconnecting...");
  while (!client.connect("clientID","use-token-auth","password")) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nconnected!");

}

void setup() {
  Serial.begin(115200);
  WiFi.begin("<YOUR WIFI SSID>", "<PASSWORD WIFI>");
  /*
    client.begin("<Address Watson IOT>", 1883, net);
    Address Watson IOT: <WatsonIOTOrganizationID>.messaging.internetofthings.ibmcloud.com
    Example:
    client.begin("iqwckl.messaging.internetofthings.ibmcloud.com", 1883, net);
  */
  client.begin("<Address Watson IOT>", 1883, net);

  connect();
  // Set up the I/O pins
  
  if (HAS_A_REF) {
    //BUG? Causes a compile error on Arduino 101: analogReference(EXTERNAL);
  }
  // PIN_INPUT is set up by the pulseDetector constructor.
  pinMode(PIN_BLINK, OUTPUT);
  digitalWrite(PIN_BLINK, LOW);
  pinMode(PIN_FADE, OUTPUT);
  fadePWM = 0;
  analogWrite(PIN_FADE, fadePWM);   // sets PWM duty cycle

  // Setup our reporting and jitter measurement.
  samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;
  lastReportMicros = 0L;
  resetJitter();

  // wait one sample interval before starting to search for pulses.
  wantMicros = micros() + MICROS_PER_READ;
}

void loop() {
  client.loop();
  
  unsigned long nowMicros = micros();
  if ((long) (wantMicros - nowMicros) > 1000L) {
    return;  // we have time to do other things
  }

  if ((long) (wantMicros - nowMicros) > 3L + OFFSET_MICROS) {
    delayMicroseconds((unsigned int) (wantMicros - nowMicros) - OFFSET_MICROS);
    nowMicros = micros();    
  }

  long jitterMicros = (long) (nowMicros - wantMicros);
  if (minJitterMicros > jitterMicros) {
    minJitterMicros = jitterMicros;
  }
  if (maxJitterMicros < jitterMicros) {
    maxJitterMicros = jitterMicros;
  }

  if (REPORT_JITTER_AND_HANG
      && (long) (nowMicros - lastReportMicros) > 60000000L) {
    lastReportMicros = nowMicros;
    
    Serial.print(F("Jitter (min, max) = "));
    Serial.print(minJitterMicros);
    Serial.print(F(", "));
    Serial.print(maxJitterMicros);
    Serial.println();
    
    resetJitter();

    //hang because our prints are incompatible with the Processing Sketch
    for (;;) { }
  }
  
  wantMicros = nowMicros + MICROS_PER_READ;
  boolean QS = pulseDetector.readSensor();

  if (pulseDetector.isPulse()) {
    digitalWrite(PIN_BLINK, HIGH);
  } else {
    digitalWrite(PIN_BLINK, LOW);
  }

  if (QS) {
    fadePWM = 255;  // start fading on the start of each beat.
    analogWrite(PIN_FADE, fadePWM);
  }


  if (--samplesUntilReport == (byte) 0) {
    samplesUntilReport = SAMPLES_PER_SERIAL_SAMPLE;

    Serial.print('S');

     // publish a message roughly every  10 second.
     if(millis() - lastMillis > 10000) {
        if(!client.connected()) {
          connect();
        }
        lastMillis = millis();
        client.publish("iot-2/evt/bpm/fmt/json", "{\"name\":\"oxigenar\",\"bpm\":\"" + String(pulseDetector.getSignal())+"\"}");
     }
    
    Serial.println(pulseDetector.getSignal());

    // Coincidentally, fade the LED a bit.
    fadePWM -= PWM_STEPS_PER_FADE;
    if (fadePWM < 0) {
      fadePWM = 0;
    }
    analogWrite(PIN_FADE, fadePWM);
    
  }

  // Every beat, report the heart rate and inter-beat-interval
  if (QS) {
    Serial.print('B');
    Serial.println(pulseDetector.getBPM());
    Serial.print('Q');
    Serial.println(pulseDetector.getIBI());
  }

}

void resetJitter() {
  // min = a number so large that any value will be smaller than it;
  // max = a number so small that any value will be larger than it.
  minJitterMicros = 60 * 1000L;
  maxJitterMicros = -1;
}
void messageReceived(String topic, String payload, char * bytes, unsigned int length) {
  Serial.print("incoming: ");
  Serial.print(topic);
  Serial.print(" - ");
  Serial.print(payload);
  Serial.println();
}
